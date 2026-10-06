#define _DEFAULT_SOURCE
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include "target.h"
int main(void){
 PlaybackJson j={0};assert(playback_json_init(&j));TargetConfig t=target;
 assert(target_valid(&t));assert(target_piccap_local(&t));
 assert(target_parse(&j,"{\"host\":\"10.1.0.91\",\"port\":19445,\"receiver\":\"HyperHDR\",\"token\":\"test-token\"}",&t));
 assert(t.port==19445 && !strcmp(t.receiver,"HyperHDR") && !strcmp(t.token,"test-token"));assert(!target_piccap_local(&t));
 assert(target_parse(&j,"{\"host\":\"::1\",\"port\":65535,\"receiver\":\"Hyperion\"}",&t));assert(!strcmp(t.token,"test-token"));
 const char *bad[]={"{}","[]","{\"host\":\"localhost\",\"port\":19444,\"receiver\":\"HyperHDR\"}","{\"host\":\"127.0.0.1\",\"port\":0,\"receiver\":\"HyperHDR\"}","{\"host\":\"127.0.0.1\",\"port\":65536,\"receiver\":\"HyperHDR\"}","{\"host\":\"127.0.0.1\",\"port\":19444.5,\"receiver\":\"HyperHDR\"}","{\"host\":\"127.0.0.1\",\"port\":\"19444\",\"receiver\":\"HyperHDR\"}","{\"host\":\"127.0.0.1\",\"port\":19444,\"receiver\":\"Other\"}","{\"host\":\"127.0.0.1\",\"port\":19444,\"receiver\":\"HyperHDR\",\"token\":\"bad\\\"token\"}"};
 for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++){TargetConfig before=t;assert(!target_parse(&j,bad[i],&t));assert(!memcmp(&t,&before,sizeof(t)));}
 const char *file="/tmp/hyperdrm-target-test.json";assert(access(file,F_OK)!=0);assert(access("/tmp/hyperdrm-target-test.json.tmp",F_OK)!=0);
 assert(target_save(file,&t));struct stat st;assert(stat(file,&st)==0 && (st.st_mode&0777)==0600);
 TargetConfig restored=target;assert(target_load(&j,file,&restored));assert(!strcmp(restored.host,"::1") && restored.port==65535 && !strcmp(restored.token,"test-token"));
 assert(target_parse(&j,"{\"host\":\"127.0.0.1\",\"port\":19444,\"receiver\":\"HyperHDR\",\"clearToken\":true}",&t));assert(!t.token[0]);assert(target_piccap_local(&t));unlink(file);
 int server=socket(AF_INET,SOCK_STREAM,0);assert(server>=0);struct sockaddr_in a={.sin_family=AF_INET,.sin_addr={htonl(INADDR_LOOPBACK)}};
 assert(bind(server,(void*)&a,sizeof(a))==0 && listen(server,1)==0);socklen_t length=sizeof(a);assert(getsockname(server,(void*)&a,&length)==0);t.port=ntohs(a.sin_port);
 int client=target_connect(&t);assert(client>=0);int accepted=accept(server,NULL,NULL);assert(accepted>=0);close(accepted);close(client);close(server);
 assert(target_connect(&t)<0);
 puts("PASS: target validation, token retention/removal, private persistence, IPv6 parsing, bounded TCP connect and refusal");return 0;
}
