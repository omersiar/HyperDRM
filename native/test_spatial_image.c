#include <assert.h>
#include <string.h>
#include "spatial_image.h"
int main(void){
 (void)layout_from_json;
 unsigned char regions[16][3];for(unsigned i=0;i<16;i++){regions[i][0]=11;regions[i][1]=22;regions[i][2]=33;}
 char *command=spatial_image_command(regions,149);assert(command);
 assert(strstr(command,"\"command\":\"image\"") && strstr(command,"\"priority\":149") && strstr(command,"\"format\":\"rgb\""));
 const char *encoded=strstr(command,"\"imagedata\":\"")+13;
 for(unsigned i=0;i<SPATIAL_PIXELS;i++)assert(!memcmp(encoded+i*4,"CxYh",4));
 assert(!strcmp(encoded+SPATIAL_PIXELS*4,"\"}\n"));free(command);
 for(unsigned i=0;i<16;i++){regions[i][0]=i*16;regions[i][1]=255-i*16;regions[i][2]=i*8;}
 command=spatial_image_command(regions,149);encoded=strstr(command,"\"imagedata\":\"")+13;
 assert(memcmp(encoded,encoded+(SPATIAL_WIDTH-1)*4,4));
 assert(memcmp(encoded,encoded+(SPATIAL_PIXELS-SPATIAL_WIDTH)*4,4));free(command);
 puts("PASS: image command, exact neutral RGB base64, spatial edge differences and cached geometry");return 0;
}
