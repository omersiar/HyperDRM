#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "blackbars.h"
int main(void){
 unsigned black[16]={0};unsigned char color[16][3];memset(color,100,sizeof(color));
 assert(letterbox_candidate(black,color)==0);
 black[3]=black[11]=black[12]=66;black[1]=55;black[5]=black[9]=33;
 assert(letterbox_candidate(black,color)==1);
 black[1]=black[5]=black[9]=0;assert(letterbox_candidate(black,color)==-1);
 for(unsigned i=0;i<16;i++)black[i]=100;
 memset(color,0,sizeof(color));assert(letterbox_candidate(black,color)==-1);
 memset(black,0,sizeof(black));assert(letterbox_candidate(black,color)==-1);
 puts("PASS: full frame, measured letterbox, one edge only, black frame, dark frame");
}
