#ifndef HYPERDRM_RECEIVER_STREAM_H
#define HYPERDRM_RECEIVER_STREAM_H
#ifndef RECEIVER_LAYOUT_REVISION
#define RECEIVER_LAYOUT_REVISION 0
#endif
/* Independent raw LED subscription: bounded, nonblocking, before receiver
 * color correction. Old result and newer data envelopes are both accepted. */
typedef struct {int fd,stage;uint64_t deadline,retry_at,updated;unsigned sequence,count,revision,layout_revision;LedBlend *mapping;char *data,*request;size_t used,sent,length;unsigned char colors[16][3];bool valid;} ReceiverStream;
static ReceiverStream stream={.fd=-1};
static void stream_close(uint64_t now){if(stream.fd>=0)close(stream.fd);free(stream.mapping);free(stream.data);free(stream.request);stream=(ReceiverStream){.fd=-1,.retry_at=now+5000};}
static bool stream_queue(const char *line){
 if(stream.fd<0 || stream.request)return false;
 stream.request=strdup(line);if(!stream.request)return false;stream.length=strlen(line);stream.sent=0;return true;
}
static void stream_start(uint64_t now){
 stream.fd=target_open(&target);if(stream.fd<0){stream.retry_at=now+5000;return;}
 stream.data=malloc(262145);if(!stream.data){stream_close(now);return;}stream.stage=1;stream.deadline=now+3000;stream.revision=calibration_revision();stream.layout_revision=RECEIVER_LAYOUT_REVISION;
}
static bool stream_colors(PlaybackJson *j,void *root,uint64_t now){
 void *body=playback_field(j,root,"result");if(!body)body=playback_field(j,root,"data");
 void *leds=playback_field(j,body,"leds");
 if(!leds || j->type(leds)!=5)return false;
 if(j->length(leds)!=(size_t)stream.count*3){stream.revision=0;return false;}
 uint64_t sums[16][3]={{0}},weights[16]={0};
 for(unsigned i=0;i<stream.count;i++){
  unsigned values[3];for(unsigned c=0;c<3;c++){void *v=j->at(leds,i*3+c);if(!v || j->type(v)!=3)return false;char *end;long n=strtol(j->string(v),&end,10);if(*end || n<0 || n>255)return false;values[c]=(unsigned)n;}
  LedBlend b=stream.mapping[i];unsigned w[2]={65535-b.weight,b.weight},r[2]={b.a,b.b};
  for(unsigned k=0;k<2;k++){weights[r[k]]+=w[k];for(unsigned c=0;c<3;c++)sums[r[k]][c]+=(uint64_t)values[c]*w[k];}
 }
 unsigned char mean[3]={0};uint64_t total=0,all[3]={0};
 for(unsigned r=0;r<16;r++){total+=weights[r];for(unsigned c=0;c<3;c++)all[c]+=sums[r][c];}
 if(!total)return false;
 for(unsigned c=0;c<3;c++)mean[c]=(unsigned char)(all[c]/total);
 for(unsigned r=0;r<16;r++)for(unsigned c=0;c<3;c++)stream.colors[r][c]=weights[r]?(unsigned char)(sums[r][c]/weights[r]):mean[c];
 stream.valid=true;stream.updated=now;stream.sequence++;return true;
}
static void stream_tick(PlaybackJson *j,uint64_t now,bool enabled){
 if(!enabled){if(stream.fd>=0)stream_close(now);return;}
 if(stream.fd>=0 && (stream.revision!=calibration_revision() || stream.layout_revision!=RECEIVER_LAYOUT_REVISION)){stream_close(now);stream.retry_at=now;}
 if(stream.fd<0){if(now>=stream.retry_at)stream_start(now);return;}
 if(stream.stage<4 && now>=stream.deadline){stream_close(now);return;}
 if(stream.stage==1){
  struct pollfd p={stream.fd,POLLOUT,0};int error=0;socklen_t size=sizeof(error);if(poll(&p,1,0)<=0)return;
  if(getsockopt(stream.fd,SOL_SOCKET,SO_ERROR,&error,&size)!=0 || error){stream_close(now);return;}
  char line[384];if(target.token[0]){snprintf(line,sizeof(line),"{\"command\":\"authorize\",\"subcommand\":\"login\",\"token\":\"%s\"}\n",target.token);stream.stage=2;}
  else {snprintf(line,sizeof(line),"{\"command\":\"serverinfo\"}\n");stream.stage=3;}
  if(!stream_queue(line)){stream_close(now);return;}
 }
 if(stream.request){
  ssize_t n=send(stream.fd,stream.request+stream.sent,stream.length-stream.sent,MSG_NOSIGNAL);
  if(n<0 && (errno==EAGAIN || errno==EWOULDBLOCK || errno==EINTR))return;
  if(n<=0){stream_close(now);return;}stream.sent+=(size_t)n;
  if(stream.sent==stream.length){free(stream.request);stream.request=NULL;}
 }
 if(stream.used==262144){stream_close(now);return;}
 size_t available=262144-stream.used;if(available>65536)available=65536;
 ssize_t n=recv(stream.fd,stream.data+stream.used,available,0);
 if(n<0 && (errno==EAGAIN || errno==EWOULDBLOCK || errno==EINTR))return;
 if(n<=0){stream_close(now);return;}stream.used+=(size_t)n;stream.data[stream.used]=0;
 for(unsigned messages=0;messages<8;messages++){
  char *end=strchr(stream.data,'\n');if(!end)break;*end=0;void *root=j->parse(stream.data);
  if(stream.stage==2){if(!playback_true(j,root,"success")){if(root)j->put(root);stream_close(now);return;}stream.stage=3;stream_queue("{\"command\":\"serverinfo\"}\n");stream.deadline=now+3000;}
  else if(stream.stage==3){
   LedBlend *mapping=NULL;unsigned count=0;if(!layout_from_json(stream.data,&mapping,&count)){if(root)j->put(root);stream_close(now);return;}
   stream.mapping=mapping;stream.count=count;stream.stage=4;stream_queue("{\"command\":\"ledcolors\",\"subcommand\":\"ledstream-start\",\"interval\":100}\n");
  }else if(playback_is(j,root,"command","ledcolors-ledstream-update"))stream_colors(j,root,now);
  if(root)j->put(root);
  size_t consumed=(size_t)(end-stream.data)+1;memmove(stream.data,stream.data+consumed,stream.used-consumed);stream.used-=consumed;stream.data[stream.used]=0;
 }
}
#endif
