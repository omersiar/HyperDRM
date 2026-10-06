#define _DEFAULT_SOURCE
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <dlfcn.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/time.h>
#include <stdarg.h>
#include <time.h>
#include "histogram.h"
#include "blackbars.h"
#include "color_boost.h"
#include "interpolation.h"
#include "layout.h"
#include "autostart.h"
#include "retry.h"
#include "target.h"
#include "spatial_image.h"
#define SERVICE "org.webosbrew.histogramhyperion.service"
#define PRIORITY 149
typedef struct {int code;char *message;const char *file;int line;const char *func;void *padding;unsigned long magic;} LSError;
typedef bool (*Callback)(void*,void*,void*);
typedef struct {const char *name;Callback function;int flags;} Method;
static bool (*reply)(void*,void*,const char*,LSError*);
static const char *(*payload)(void*);
static bool (*error_init)(LSError*);
static void (*error_free)(LSError*);
static void *(*vt_init)(bool(*)(void));
static int (*vt_preset)(void*,int),(*vt_get)(void*,Histogram*),(*vt_finish)(void*);
static void *capture;
static char lg_diagnostic[256];
/* Process-local LG diagnostics; no changes to the loaded LG library. */
void printLog(const char *context,int level,const char *message_id,
              const char *file,const char *function,int line,const char *format,...) {
 (void)context;(void)message_id;(void)file;
 if(capture && level>3)return;
 if(format){va_list args;va_start(args,format);vsnprintf(lg_diagnostic,sizeof(lg_diagnostic),format,args);va_end(args);}
 fprintf(stderr,"LG %s:%d: ",function?function:"?",line);
 if(format){va_list args;va_start(args,format);vfprintf(stderr,format,args);va_end(args);}
 fputc('\n',stderr);
}
static int output=-1;
static unsigned long frames;
static unsigned char rgb[16][3];
static unsigned char sent_rgb[3];
static unsigned black_percent[16];
static unsigned capture_preset;
static unsigned letterbox_mode,settling;
static int pending_letterbox=-1;
static uint64_t candidate_since,next_border_probe;
static bool color_valid;
static unsigned led_count;
static LedBlend *led_blends;
static bool interpolation_enabled;
static RetryState retries;
static unsigned capture_recoveries;
static uint64_t recovery_started,last_recovery_ms;
static uint64_t milliseconds(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000+t.tv_nsec/1000000;}
static char last_error[256];
static volatile sig_atomic_t revoked,quitting;
static int guard_pipe=-1;
static pid_t guard_pid=-1;
#define DEV_MODE "/var/luna/preferences/vt_devmode"
/* LG's developer gate is enabled only during initialization. A separate
 * guardian removes our own marker even if this process aborts or is killed.
 * An existing marker is preserved, as are files replaced by another owner. */
static bool enter_developer_mode(void){
 int marker=open(DEV_MODE,O_CREAT|O_EXCL|O_WRONLY|O_CLOEXEC,0600);
 if(marker<0)return errno==EEXIST;
 struct stat owned;
 if(fstat(marker,&owned)!=0){close(marker);unlink(DEV_MODE);return false;}
 close(marker);int p[2];
 if(pipe(p)!=0){unlink(DEV_MODE);return false;}
 pid_t child=fork();
 if(child<0){close(p[0]);close(p[1]);unlink(DEV_MODE);return false;}
 if(child==0){
  close(p[1]);struct pollfd fd={p[0],POLLIN|POLLHUP,0};
  /* Bounded cleanup also covers an indefinitely blocked initialization. */
  poll(&fd,1,10000);close(p[0]);struct stat current;
  int rc=0;
  if(lstat(DEV_MODE,&current)==0 && current.st_dev==owned.st_dev && current.st_ino==owned.st_ino)rc=unlink(DEV_MODE);
  _exit(rc==0?0:1);
 }
 close(p[0]);guard_pipe=p[1];guard_pid=child;return true;
}
static bool leave_developer_mode(void){
 if(guard_pipe<0)return true;
 close(guard_pipe);guard_pipe=-1;int status;
 while(waitpid(guard_pid,&status,0)<0){if(errno!=EINTR)return false;}
 guard_pid=-1;return WIFEXITED(status)&&WEXITSTATUS(status)==0;
}
static void quit_signal(int s){(void)s;quitting=1;}
static bool policy(void){revoked=1;return true;}
static void set_error(const char *s){snprintf(last_error,sizeof(last_error),"%s",s);fprintf(stderr,"%s\n",s);}
static int send_line(const char *line){
 size_t left=strlen(line);const char *p=line;
 while(left){ssize_t n=send(output,p,left,MSG_NOSIGNAL);if(n<=0)return -1;p+=n;left-=n;}
 return 0;
}
static bool read_layout(void){
 const size_t capacity=2*1024*1024;char *data=malloc(capacity+1);if(!data)return false;
 bool complete=false;size_t used=0;
 if(send_line("{\"command\":\"serverinfo\"}\n")==0){
  while(used<capacity){ssize_t n=recv(output,data+used,capacity-used,0);if(n<=0)break;used+=(size_t)n;data[used]=0;char *end=strchr(data,'\n');if(end){*end=0;complete=true;break;}}
 }
 bool ok=complete && layout_from_json(data,&led_blends,&led_count);free(data);interpolation_enabled=ok;return ok;
}
static bool target_login(void){
 if(!target.token[0])return true;
 char request[384],answer[4096];size_t used=0;
 snprintf(request,sizeof(request),"{\"command\":\"authorize\",\"subcommand\":\"login\",\"token\":\"%s\"}\n",target.token);
 if(send_line(request)!=0)return false;
 while(used+1<sizeof(answer)){
  ssize_t n=recv(output,answer+used,sizeof(answer)-used-1,0);if(n<=0)return false;
  used+=(size_t)n;answer[used]=0;if(strchr(answer,'\n'))break;
 }
 PlaybackJson json={0};if(!playback_json_init(&json))return false;
 void *root=json.parse(answer);bool ok=playback_true(&json,root,"success");if(root)json.put(root);dlclose(json.lib);return ok;
}
static void release_capture(bool clear_output){
 free(led_blends);led_blends=NULL;led_count=0;interpolation_enabled=false;
 color_valid=false;memset(rgb,0,sizeof(rgb));memset(sent_rgb,0,sizeof(sent_rgb));
 if(output>=0){if(clear_output && frames){char line[80];snprintf(line,sizeof(line),"{\"command\":\"clear\",\"priority\":%d}\n",PRIORITY);send_line(line);}close(output);output=-1;}
 if(capture){vt_finish(capture);capture=NULL;}
}
static void stop_capture(void){release_capture(true);}
static bool respond(void *h,void *m,const char *s){if(!h||!m)return true;LSError e;error_init(&e);bool ok=reply(h,m,s,&e);error_free(&e);return ok;}
#include "handover.h"
static bool start_failure(void *h,void *m,const char *message){
 retry_update(&retries,boot_enabled() && geteuid()==0,false,milliseconds());
 char json[512];snprintf(json,sizeof(json),"{\"returnValue\":false,\"errorText\":\"%s\",\"retryScheduled\":%s,\"retryBlocked\":%s}",message,retries.due?"true":"false",retries.blocked?"true":"false");
 return respond(h,m,json);
}
static bool start(void *h,void *m,void *ctx){
 (void)ctx;const char *p=m?payload(m):NULL;
 if(p && strcmp(p,"{}")!=0)return respond(h,m,"{\"returnValue\":false,\"errorText\":\"Start expects an empty object; use settings to configure the receiver\"}");
 if(m)retry_reset(&retries);
 if(piccap_owned)return respond(h,m,"{\"returnValue\":true,\"running\":true,\"activeSource\":\"PicCap\"}");
 if(capture)return respond(h,m,"{\"returnValue\":true,\"running\":true}");
 frames=0;revoked=0;last_error[0]=0;lg_diagnostic[0]=0;
 output=target_connect(&target);
 if(output<0){stop_capture();set_error("Cannot connect to configured receiver IP and JSON port");return start_failure(h,m,"Configured receiver is unavailable");}
 if(!target_login()){stop_capture();set_error("Receiver token login failed; check the saved API token");return start_failure(h,m,"Receiver authentication failed");}
 if(!read_layout()){stop_capture();set_error("Cannot read receiver LED layout; check JSON port and API authorization");return start_failure(h,m,"Receiver layout unavailable; check API authorization");}
 if(!enter_developer_mode()){stop_capture();set_error("Developer initialization needs root. Elevate this own service through Homebrew Channel.");return start_failure(h,m,"Root access is required for temporary LG developer initialization");}
 capture=vt_init(policy);
 if(!leave_developer_mode()){stop_capture();retries.blocked=true;set_error("Developer marker cleanup failed; manual intervention required");return start_failure(h,m,"Developer marker cleanup failed");}
 if(!capture){stop_capture();set_error("LG refused histogram initialization");return start_failure(h,m,"LG refused histogram initialization. Another capture source may hold VT_CAPTURE.");}
 if(vt_preset(capture,0)!=0){stop_capture();set_error("LG histogram preset initialization failed");return start_failure(h,m,"Histogram preset failed");}
 capture_preset=letterbox_mode=settling=0;pending_letterbox=-1;next_border_probe=0;
 retries.due=0;retries.attempts=0;retries.quick_retries=0;
 if(recovery_started){last_recovery_ms=milliseconds()-recovery_started;recovery_started=0;}
 return respond(h,m,"{\"returnValue\":true,\"running\":true}");
}
static bool stop(void *h,void *m,void *ctx){(void)ctx;retry_pause(&retries);stop_capture();return respond(h,m,"{\"returnValue\":true,\"running\":false}");}
static bool autostart(void *h,void *m,void *ctx){
 (void)ctx;const char *p=m?payload(m):NULL;bool enabled;
 if(p && strcmp(p,"{\"enabled\":true}")==0)enabled=true;
 else if(p && strcmp(p,"{\"enabled\":false}")==0)enabled=false;
 else return respond(h,m,"{\"returnValue\":false,\"errorText\":\"Expected enabled boolean\"}");
 if(geteuid()!=0 || !set_boot(enabled))return respond(h,m,"{\"returnValue\":false,\"errorText\":\"Autostart could not be saved. Check own service root elevation and boot hook.\"}");
 if(enabled)retry_reset(&retries);
 retry_update(&retries,enabled,capture!=NULL,milliseconds());
 return respond(h,m,enabled?"{\"returnValue\":true,\"autostart\":true}":"{\"returnValue\":true,\"autostart\":false}");
}
static bool handover(void *h,void *m,void *ctx){
 (void)ctx;const char *p=payload(m);bool enabled;
 if(strcmp(p,"{\"enabled\":true}")==0)enabled=true;
 else if(strcmp(p,"{\"enabled\":false}")==0)enabled=false;
 else return respond(h,m,"{\"returnValue\":false,\"errorText\":\"Expected enabled boolean\"}");
 if(!playback_json.lib || geteuid()!=0)return respond(h,m,"{\"returnValue\":false,\"errorText\":\"Root service and JSON-C are required\"}");
 if(enabled && !piccap_installed())return respond(h,m,"{\"returnValue\":false,\"errorText\":\"PicCap is not installed\"}");
 if(!handover_save(enabled))return respond(h,m,"{\"returnValue\":false,\"errorText\":\"Could not save PicCap handover preference\"}");
 handover_enabled=enabled;protection=PLAYBACK_UNKNOWN;clear_samples=0;next_media_probe=0;
 return respond(h,m,"{\"returnValue\":true}");
}
static bool settings(void *h,void *m,void *ctx){
 (void)ctx;TargetConfig next=target;
 if(!playback_json.lib || !target_parse(&playback_json,payload(m),&next))return respond(h,m,"{\"returnValue\":false,\"errorText\":\"Enter a valid IPv4/IPv6 address, integer port (1-65535), receiver and optional API token\"}");
 if(!target_save(TARGET_PREF,&next))return respond(h,m,"{\"returnValue\":false,\"errorText\":\"Could not save receiver settings\"}");
 bool active=capture || piccap_owned || retries.due;
 stop_capture();target=next;last_error[0]=0;protection=PLAYBACK_UNKNOWN;clear_samples=0;
 if(active && !retries.paused && !retries.blocked)retries.due=milliseconds();
 return respond(h,m,"{\"returnValue\":true}");
}
static bool status(void *h,void *m,void *ctx){
 (void)ctx;char json[4096],safe[512],endpoint[80];size_t n=0;
 snprintf(endpoint,sizeof(endpoint),strchr(target.host,':')?"[%s]:%u":"%s:%u",target.host,target.port);
 for(size_t i=0;last_error[i] && n+2<sizeof(safe);i++){char c=last_error[i];if(c=='"'||c=='\\')safe[n++]='\\';if((unsigned char)c>=32)safe[n++]=c;}safe[n]=0;
 int used=snprintf(json,sizeof(json),"{\"returnValue\":true,\"version\":\"0.8.1\",\"running\":%s,\"colorValid\":%s,\"frames\":%lu,\"captureRecoveries\":%u,\"lastRecoveryMs\":%llu,\"endpoint\":\"%s\",\"host\":\"%s\",\"port\":%u,\"receiver\":\"%s\",\"tokenSet\":%s,\"piccapTargetCompatible\":%s,\"priority\":%d,\"uid\":%u,\"drmVerified\":false,\"mappingVerified\":false,\"outputColor\":[%u,%u,%u],\"error\":\"%s\",\"regions\":[",(capture||capture_source==SOURCE_PICCAP)?"true":"false",color_valid?"true":"false",frames,capture_recoveries,(unsigned long long)last_recovery_ms,endpoint,target.host,target.port,target.receiver,target.token[0]?"true":"false",target_piccap_local(&target)?"true":"false",PRIORITY,(unsigned)geteuid(),sent_rgb[0],sent_rgb[1],sent_rgb[2],safe);
 for(int i=0;i<16;i++)used+=snprintf(json+used,sizeof(json)-used,"%s[%u,%u,%u]",i?",":"",rgb[i][0],rgb[i][1],rgb[i][2]);
 used+=snprintf(json+used,sizeof(json)-used,"],\"ledCount\":%u,\"spatialOutput\":%s,\"blackPercent\":[",led_count,led_count?"true":"false");
 for(int i=0;i<16;i++)used+=snprintf(json+used,sizeof(json)-used,"%s%u",i?",":"",black_percent[i]);
 snprintf(json+used,sizeof(json)-used,"],\"preset\":%u,\"saturationGain\":1.0,\"brightnessGain\":1.0,\"spatialInterpolation\":%s,\"blackBarDetection\":true,\"autostart\":%s,\"retryScheduled\":%s,\"retryPaused\":%s,\"retryBlocked\":%s,\"retryAttempts\":%u,\"piccapInstalled\":%s,\"piccapHandover\":%s,\"piccapConnected\":%s,\"activeSource\":\"%s\",\"playbackProtection\":\"%s\",\"letterboxDetected\":%s}",capture_preset,interpolation_enabled?"true":"false",boot_enabled()?"true":"false",retries.due?"true":"false",retries.paused?"true":"false",retries.blocked?"true":"false",retries.attempts,piccap_installed()?"true":"false",handover_enabled?"true":"false",piccap_connected?"true":"false",piccap_owned?"PicCap":"HyperDRM",protection==PLAYBACK_PROTECTED?"protected":protection==PLAYBACK_CLEAR?"non-secure decoder":"unknown",capture&&letterbox_mode?"true":"false");return respond(h,m,json);
}
static int tick(void *unused){
 (void)unused;
 if(handover_tick())return 1;
 if(!capture){
  retry_update(&retries,boot_enabled() && geteuid()==0,false,milliseconds());
  if(retry_take(&retries,milliseconds())){
   start(NULL,NULL,NULL);
   if(!capture)retry_failure(&retries,milliseconds());
  }
  return 1;
 }
 if(revoked){set_error("LG revoked capture; recovering after source transition");capture_recoveries++;recovery_started=milliseconds();release_capture(false);retry_source_transition(&retries,milliseconds());return 1;}
 if(settling){settling--;return 1;}
 if(letterbox_mode && capture_preset==1 && milliseconds()>=next_border_probe){
  if(vt_preset(capture,0)!=0){set_error("Border probe preset failed");stop_capture();return 1;}
  capture_preset=0;settling=2;return 1;
 }
 Histogram hist[16]={0};int rc=vt_get(capture,hist);
 if(rc){set_error("LG histogram read failed; streaming stopped");stop_capture();return 1;}
 unsigned aggregate[3]={0};
 for(int i=0;i<16;i++){
  black_percent[i]=100;
  for(int c=0;c<3;c++){
   uint64_t total=0,black=0;
   for(int b=0;b<256;b++){total+=hist[i].bins[c][b];if(b<=12)black+=hist[i].bins[c][b];}
   unsigned fraction=total?(unsigned)(100*black/total):0;
   if(fraction<black_percent[i])black_percent[i]=fraction;
  }
 }
 for(int i=0;i<16;i++)for(int c=0;c<3;c++){
  rgb[i][c]=histogram_mean(hist[i].bins[c]);aggregate[c]+=rgb[i][c];
 }
 if(capture_preset==0){
  int candidate=letterbox_candidate(black_percent,rgb);uint64_t now=milliseconds();
  if(candidate<0)pending_letterbox=-1;
  else if(candidate!=pending_letterbox){pending_letterbox=candidate;candidate_since=now;}
  else if((unsigned)candidate!=letterbox_mode && now-candidate_since>=2000)letterbox_mode=(unsigned)candidate;
  if(letterbox_mode){
   if(vt_preset(capture,1)!=0){set_error("Letterbox preset failed");stop_capture();return 1;}
   capture_preset=1;settling=2;next_border_probe=now+2000;return 1;
  }
 }
 for(int c=0;c<3;c++)sent_rgb[c]=(unsigned char)(aggregate[c]/16);
 unsigned char boosted_mean[3];boost_color(sent_rgb,boosted_mean);memcpy(sent_rgb,boosted_mean,3);
 bool image_output=strcmp(target.receiver,"HyperHDR")==0;
 size_t capacity=256+(size_t)led_count*12;char *line=image_output?spatial_image_command(rgb,PRIORITY):malloc(capacity);
 if(!line){set_error("Color buffer allocation failed");stop_capture();return 1;}
 if(!image_output){
 size_t written=(size_t)snprintf(line,capacity,"{\"command\":\"color\",\"priority\":%d,\"duration\":1500,\"origin\":\"HyperDRM\",\"color\":[",PRIORITY);
 for(unsigned i=0;i<led_count;i++){
  unsigned char interpolated[3],boosted[3];LedBlend blend=led_blends[i];
  blend_color(rgb[blend.a],rgb[blend.b],blend.weight,interpolated);boost_color(interpolated,boosted);
  written+=(size_t)snprintf(line+written,capacity-written,"%s%u,%u,%u",i?",":"",boosted[0],boosted[1],boosted[2]);
 }
 snprintf(line+written,capacity-written,"]}\n");
 }
 int failed=send_line(line);free(line);
 if(failed){set_error("Receiver connection lost");stop_capture();return 1;}
 char answer[4096];size_t used=0;bool complete=false;
 while(used+1<sizeof(answer)){ssize_t len=recv(output,answer+used,sizeof(answer)-used-1,0);if(len<=0)break;used+=(size_t)len;answer[used]=0;if(strchr(answer,'\n')){complete=true;break;}}
 if(!complete){set_error("Receiver did not acknowledge color data");stop_capture();return 1;}
 if(!strstr(answer,"\"success\":true")&&!strstr(answer,"\"success\": true")){set_error("Receiver rejected the color command");stop_capture();return 1;}
 color_valid=true;frames++;return 1;
}
static void *symbol(void *lib,const char *name){void *s=dlsym(lib,name);if(!s){fprintf(stderr,"Missing symbol %s\n",name);exit(2);}return s;}
int main(void){
 signal(SIGTERM,quit_signal);signal(SIGINT,quit_signal);
 if(boot_enabled() && !set_boot(true))fprintf(stderr,"Own legacy boot hook migration failed\n");
 void *vt=dlopen("libvtcapture.so.1",RTLD_NOW),*ls=dlopen("libluna-service2.so.3",RTLD_NOW),*glib=dlopen("libglib-2.0.so.0",RTLD_NOW);
 if(!vt||!ls||!glib){fprintf(stderr,"Missing LG library: %s\n",dlerror());return 2;}
 vt_init=symbol(vt,"vtCapture_initHistogram");vt_preset=symbol(vt,"vtCapture_setPresetHistogram");vt_get=symbol(vt,"vtCapture_getHistogram");vt_finish=symbol(vt,"vtCapture_finalizeHistogram");
 call_once=symbol(ls,"LSCallOneReply");
 cancel_call=symbol(ls,"LSCallCancel");
 if(playback_json_init(&playback_json)){handover_load();target_load(&playback_json,TARGET_PREF,&target);}
 reply=symbol(ls,"LSMessageReply");payload=symbol(ls,"LSMessageGetPayload");error_init=symbol(ls,"LSErrorInit");error_free=symbol(ls,"LSErrorFree");
 bool (*reg)(const char*,void**,LSError*)=symbol(ls,"LSRegister");
 bool (*category)(void*,const char*,Method*,void*,void*,LSError*)=symbol(ls,"LSRegisterCategory");
 bool (*attach)(void*,void*,LSError*)=symbol(ls,"LSGmainAttach");
 bool (*unreg)(void*,LSError*)=symbol(ls,"LSUnregister");
 void *(*loop_new)(void*,int)=symbol(glib,"g_main_loop_new");
 int (*iterate)(void*,int)=symbol(glib,"g_main_context_iteration");
 unsigned (*timer)(unsigned,int(*)(void*),void*)=symbol(glib,"g_timeout_add");
 void (*loop_unref)(void*)=symbol(glib,"g_main_loop_unref");
 void *handle=NULL,*loop=loop_new(NULL,0);LSError e;error_init(&e);
 Method methods[]={{"start",start,0},{"stop",stop,0},{"status",status,0},{"autostart",autostart,0},{"handover",handover,0},{"settings",settings,0},{NULL,NULL,0}};
 if(!reg(SERVICE,&handle,&e)||!category(handle,"/",methods,NULL,NULL,&e)||!attach(handle,loop,&e)){fprintf(stderr,"Luna registration failed: %s\n",e.message?e.message:"unknown");error_free(&e);return 3;}
 service_handle=handle;
 if(!boot_enabled())retry_pause(&retries);
 timer(100,tick,NULL);
 while(!quitting){iterate(NULL,0);usleep(10000);}
 stop_capture();
 if(piccap_owned){
  handover_enabled=false;retry_pause(&retries);
  uint64_t deadline=milliseconds()+4000;
  while(piccap_owned && milliseconds()<deadline){handover_tick();iterate(NULL,0);usleep(10000);}
 }
 unreg(handle,&e);error_free(&e);loop_unref(loop);return 0;
}
