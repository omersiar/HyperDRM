#define _DEFAULT_SOURCE
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include "retry.h"
typedef struct {int unused;} LSError;
typedef bool (*Callback)(void*,void*,void*);
static RetryState retries;
static uint64_t clock_ms=10000;
static unsigned stopped,called,canceled;
static char last_uri[128];
static uint64_t milliseconds(void){return clock_ms;}
static void stop_capture(void){stopped++;}
static const char *payload(void *m){return m;}
static void error_init(LSError *e){(void)e;}
static void error_free(LSError *e){(void)e;}
#define PICCAP_BINARY "/bin/sh"
#include "handover.h"
static bool mock_call(void *h,const char *uri,const char *data,Callback cb,void *ctx,unsigned long *token,LSError *e){
 (void)h;(void)data;(void)cb;(void)ctx;(void)e;*token=++called;snprintf(last_uri,sizeof(last_uri),"%s",uri);return true;
}
static bool mock_cancel(void *h,unsigned long token,LSError *e){(void)h;(void)token;(void)e;canceled++;return true;}
static void answer(int cmd,bool running){
 char json[128];snprintf(json,sizeof(json),"{\"returnValue\":true,\"isRunning\":%s,\"connected\":true}",running?"true":"false");
 piccap_reply(NULL,json,(void*)(intptr_t)cmd);
}
int main(void){
 (void)handover_save;(void)handover_load;
 (void)retry_failure;(void)retry_take;(void)retry_update;
 assert(playback_json_init(&playback_json));call_once=mock_call;cancel_call=mock_cancel;
 handover_enabled=true;protection=PLAYBACK_PROTECTED;protection_at=clock_ms;next_media_probe=UINT64_MAX;
 assert(!handover_tick());assert(called==0 && stopped==0);
 protection=PLAYBACK_CLEAR;clear_samples=1;assert(!handover_tick());assert(called==0);
 clear_samples=2;assert(handover_tick());assert(stopped==1 && strstr(last_uri,"/start"));
 answer(1,false);clock_ms+=600;assert(handover_tick());assert(strstr(last_uri,"/status"));answer(0,true);
 assert(capture_source==SOURCE_PICCAP && piccap_connected);
 protection=PLAYBACK_PROTECTED;assert(handover_tick());assert(strstr(last_uri,"/stop"));
 answer(2,true);assert(strstr(last_uri,"/status"));assert(piccap_owned);answer(0,false);
 assert(!piccap_owned && capture_source==SOURCE_HISTOGRAM && retries.due==clock_ms);
 protection=PLAYBACK_CLEAR;protection_at=clock_ms;clear_samples=2;assert(handover_tick());answer(1,true);
 retry_pause(&retries);assert(handover_tick());assert(strstr(last_uri,"/stop"));answer(2,false);answer(0,false);
 assert(!piccap_owned && retries.due==0);assert(!handover_tick());
 retry_reset(&retries);assert(handover_tick());answer(1,true);handover_enabled=false;
 assert(handover_tick());answer(2,false);answer(0,false);assert(!piccap_owned);
 handover_enabled=true;protection_at=clock_ms;assert(handover_tick());
 clock_ms+=3100;assert(handover_tick());assert(canceled==1 && capture_source==SOURCE_PICCAP_STOPPING);
 assert(strstr(last_uri,"/stop"));answer(2,false);answer(0,false);
 assert(!piccap_owned);assert(!handover_tick());
 puts("PicCap handover tests passed");return 0;
}
