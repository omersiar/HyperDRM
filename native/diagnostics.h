#ifndef HYPERDRM_DIAGNOSTICS_H
#define HYPERDRM_DIAGNOSTICS_H
/* An independent nonblocking connection. Tests never send colors or acquire
 * capture. Work per timer iteration is bounded to keep output responsive. */
typedef enum {TEST_IDLE,TEST_CONNECT,TEST_SEND,TEST_LOGIN,TEST_LAYOUT,TEST_DONE,TEST_FAILED} TestStage;
static struct {
 int fd;TestStage stage,after_send;TargetConfig target;
 uint64_t started,deadline,elapsed;unsigned leds;
 char request[384],error[128];size_t sent,used;char *response;
} connection_test={.fd=-1};
static const char *test_stage_name(void){
 static const char *names[]={"Not tested","Connecting","Sending request","Checking authentication","Reading LED layout","Passed","Failed"};
 return names[connection_test.stage];
}
static bool test_busy(void){return connection_test.stage>=TEST_CONNECT && connection_test.stage<=TEST_LAYOUT;}
static void test_close(void){if(connection_test.fd>=0)close(connection_test.fd);connection_test.fd=-1;free(connection_test.response);connection_test.response=NULL;memset(connection_test.request,0,sizeof(connection_test.request));memset(connection_test.target.token,0,sizeof(connection_test.target.token));}
static void test_finish(const char *error,uint64_t now){
 test_close();connection_test.elapsed=now-connection_test.started;
 connection_test.stage=error?TEST_FAILED:TEST_DONE;
 snprintf(connection_test.error,sizeof(connection_test.error),"%s",error?error:"");
}
static void test_request(bool login,uint64_t now){
 if(login)snprintf(connection_test.request,sizeof(connection_test.request),"{\"command\":\"authorize\",\"subcommand\":\"login\",\"token\":\"%s\"}\n",connection_test.target.token);
 else snprintf(connection_test.request,sizeof(connection_test.request),"{\"command\":\"serverinfo\"}\n");
 connection_test.sent=connection_test.used=0;connection_test.after_send=login?TEST_LOGIN:TEST_LAYOUT;
 connection_test.stage=TEST_SEND;connection_test.deadline=now+3000;
}
static bool test_begin(const TargetConfig *target,uint64_t now){
 if(test_busy())return false;
 test_close();memset(&connection_test,0,sizeof(connection_test));connection_test.fd=-1;
 connection_test.target=*target;connection_test.started=now;
 connection_test.response=malloc(2*1024*1024+1);
 if(!connection_test.response){test_finish("Cannot allocate connection test buffer",now);return true;}
 connection_test.fd=target_open(target);
 if(connection_test.fd<0){test_finish("Cannot open receiver connection",now);return true;}
 connection_test.stage=TEST_CONNECT;connection_test.deadline=now+3000;return true;
}
static void test_tick(PlaybackJson *json,uint64_t now){
 if(!test_busy())return;
 if(now>=connection_test.deadline){test_finish("Receiver timed out",now);return;}
 int fd=connection_test.fd;
 if(connection_test.stage==TEST_CONNECT){
  struct pollfd p={fd,POLLOUT,0};int error=0;socklen_t size=sizeof(error);
  if(poll(&p,1,0)<=0)return;
  if(getsockopt(fd,SOL_SOCKET,SO_ERROR,&error,&size)!=0 || error){test_finish("Receiver connection refused or unreachable",now);return;}
  test_request(connection_test.target.token[0]!=0,now);return;
 }
 if(connection_test.stage==TEST_SEND){
  size_t length=strlen(connection_test.request);
  ssize_t n=send(fd,connection_test.request+connection_test.sent,length-connection_test.sent,MSG_NOSIGNAL);
  if(n<0 && (errno==EAGAIN || errno==EWOULDBLOCK || errno==EINTR))return;
  if(n<=0){test_finish("Receiver closed the test connection",now);return;}
  connection_test.sent+=(size_t)n;
  if(connection_test.sent==length){connection_test.stage=connection_test.after_send;connection_test.deadline=now+3000;memset(connection_test.request,0,sizeof(connection_test.request));}
  return;
 }
 size_t available=2*1024*1024-connection_test.used;if(available>65536)available=65536;
 if(!available){test_finish("Receiver response exceeds 2 MiB",now);return;}
 ssize_t n=recv(fd,connection_test.response+connection_test.used,available,0);
 if(n<0 && (errno==EAGAIN || errno==EWOULDBLOCK || errno==EINTR))return;
 if(n<=0){test_finish("Receiver closed the test connection",now);return;}
 connection_test.used+=(size_t)n;connection_test.response[connection_test.used]=0;
 char *end=strchr(connection_test.response,'\n');if(!end)return;*end=0;
 void *root=json->parse(connection_test.response);bool success=playback_true(json,root,"success");if(root)json->put(root);
 if(!success){test_finish(connection_test.stage==TEST_LOGIN?"Receiver authentication failed":"Receiver rejected serverinfo; check authorization",now);return;}
 if(connection_test.stage==TEST_LOGIN){test_request(false,now);return;}
 LedBlend *mapping=NULL;unsigned count=0;
 if(!layout_from_json(connection_test.response,&mapping,&count)){test_finish("Receiver returned an invalid or empty LED layout",now);return;}
 free(mapping);connection_test.leds=count;test_finish(NULL,now);
}
#endif
