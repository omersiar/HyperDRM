#ifndef HYPERDRM_HANDOVER_H
#define HYPERDRM_HANDOVER_H
#include "playback.h"
#ifndef PICCAP_BINARY
#define PICCAP_BINARY "/media/developer/apps/usr/palm/services/org.webosbrew.piccap.service/hyperion-webos"
#endif
#define HANDOVER_PREF "/media/developer/apps/usr/palm/services/org.webosbrew.histogramhyperion.service/handover.json"
static PlaybackJson playback_json;
static bool handover_enabled,piccap_owned,piccap_busy,media_busy,piccap_connected;
static PlaybackProtection protection;
static uint64_t protection_at,next_media_probe,next_piccap_probe,handover_cooldown,piccap_action_at;
typedef enum {SOURCE_HISTOGRAM,SOURCE_PICCAP_STARTING,SOURCE_PICCAP,SOURCE_PICCAP_STOPPING} CaptureSource;
static CaptureSource capture_source;
static void *service_handle;
static bool (*call_once)(void*,const char*,const char*,Callback,void*,unsigned long*,LSError*);
static bool (*cancel_call)(void*,unsigned long,LSError*);
static unsigned long piccap_token,media_token;
static uint64_t piccap_deadline,media_deadline;
static unsigned clear_samples;
static bool piccap_request(int command);
static inline bool piccap_installed(void){return access(PICCAP_BINARY,X_OK)==0;}
static bool handover_save(bool enabled){
 const char *data=enabled?"{\"piccapHandover\":true}\n":"{\"piccapHandover\":false}\n";
 int fd=open(HANDOVER_PREF ".tmp",O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);if(fd<0)return false;
 ssize_t n=write(fd,data,strlen(data));bool ok=n==(ssize_t)strlen(data) && fsync(fd)==0;
 if(close(fd)!=0)ok=false;
 if(ok)ok=rename(HANDOVER_PREF ".tmp",HANDOVER_PREF)==0;
 if(!ok)unlink(HANDOVER_PREF ".tmp");
 return ok;
}
static void handover_load(void){
 int fd=open(HANDOVER_PREF,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);if(fd<0)return;
 char text[128];ssize_t n=read(fd,text,sizeof(text)-1);close(fd);
 if(n>0){text[n]=0;void *root=playback_json.parse(text);handover_enabled=playback_true(&playback_json,root,"piccapHandover");if(root)playback_json.put(root);}
}
static bool media_reply(void *h,void *m,void *ctx){
 (void)h;(void)ctx;media_busy=false;media_token=0;
 protection=playback_classify(&playback_json,payload(m));protection_at=milliseconds();
 if(protection==PLAYBACK_CLEAR){if(clear_samples<2)clear_samples++;}else clear_samples=0;
 return true;
}
/* Commands: 0 status, 1 start, 2 stop. Confirm stopped status before VT reuse. */
static bool piccap_reply(void *h,void *m,void *ctx){
 (void)h;int command=(int)(intptr_t)ctx;piccap_busy=false;piccap_token=0;
 void *root=playback_json.parse(payload(m));bool ok=playback_true(&playback_json,root,"returnValue");
 if(command==0){
  void *running=playback_field(&playback_json,root,"isRunning");
  if(ok && running && playback_json.type(running)==1){
   bool active=playback_json.boolean(running);
   piccap_connected=playback_true(&playback_json,root,"connected");
   if(active){if(capture_source==SOURCE_PICCAP_STARTING)capture_source=SOURCE_PICCAP;}
   else if(capture_source!=SOURCE_PICCAP_STARTING || milliseconds()-piccap_action_at>=2000){
    if(capture_source!=SOURCE_PICCAP_STOPPING)handover_cooldown=milliseconds()+10000;
    piccap_owned=false;capture_source=SOURCE_HISTOGRAM;piccap_connected=false;
    if(!retries.paused)retries.due=milliseconds();
   }
  }else if(!piccap_installed()){piccap_owned=false;capture_source=SOURCE_HISTOGRAM;piccap_connected=false;}
 }else if(command==1 && !ok){capture_source=SOURCE_PICCAP_STOPPING;handover_cooldown=milliseconds()+10000;}
 if(root)playback_json.put(root);
 next_piccap_probe=milliseconds()+500;
 if(command==2)piccap_request(0);
 return true;
}
static bool piccap_request(int command){
 if(piccap_busy)return false;
 const char *uri=command==1?"luna://org.webosbrew.piccap.service/start":command==2?"luna://org.webosbrew.piccap.service/stop":"luna://org.webosbrew.piccap.service/status";
 LSError e;error_init(&e);
 piccap_busy=call_once(service_handle,uri,"{}",piccap_reply,(void*)(intptr_t)command,&piccap_token,&e);
 piccap_deadline=milliseconds()+3000;
 error_free(&e);if(command)piccap_action_at=milliseconds();return piccap_busy;
}
/* Return true while PicCap owns capture or a stop confirmation is pending. */
static bool handover_tick(void){
 uint64_t now=milliseconds();
 if(media_busy && now>=media_deadline){LSError e;error_init(&e);cancel_call(service_handle,media_token,&e);error_free(&e);media_busy=false;media_token=0;protection=PLAYBACK_UNKNOWN;clear_samples=0;}
 if(piccap_busy && now>=piccap_deadline){LSError e;error_init(&e);cancel_call(service_handle,piccap_token,&e);error_free(&e);piccap_busy=false;piccap_token=0;capture_source=SOURCE_PICCAP_STOPPING;next_piccap_probe=0;}
 if(handover_enabled && piccap_installed() && !retries.paused && playback_json.lib && !media_busy && now>=next_media_probe){
  LSError e;error_init(&e);
  media_busy=call_once(service_handle,"luna://com.webos.media/getActivePipelines","{}",media_reply,NULL,&media_token,&e);
  media_deadline=now+3000;
  error_free(&e);next_media_probe=now+1000;
 }
 bool clear=handover_enabled && target_piccap_local(&target) && piccap_installed() && !retries.paused && !retries.blocked &&
  protection==PLAYBACK_CLEAR && clear_samples>=2 && now-protection_at<3000 && now>=handover_cooldown;
 if(capture_source==SOURCE_HISTOGRAM && clear){
  stop_capture();retries.due=0;piccap_owned=true;capture_source=SOURCE_PICCAP_STARTING;
  if(!piccap_request(1)){capture_source=SOURCE_PICCAP_STOPPING;handover_cooldown=now+10000;}
 }
 if(piccap_owned && !clear && capture_source!=SOURCE_PICCAP_STOPPING){capture_source=SOURCE_PICCAP_STOPPING;next_piccap_probe=0;}
 if(piccap_owned && !piccap_busy && now>=next_piccap_probe){
  if(capture_source==SOURCE_PICCAP_STOPPING)piccap_request(2);else piccap_request(0);
  next_piccap_probe=now+1000;
 }
 return piccap_owned;
}
#endif
