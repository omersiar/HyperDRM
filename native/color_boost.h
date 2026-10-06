#ifndef COLOR_BOOST_H
#define COLOR_BOOST_H
/* HSV saturation x2 and value x1.33, preserving hue and clamping value. */
static inline void boost_color(const unsigned char in[3],unsigned char out[3]){
 unsigned lo=in[0],hi=in[0];
 for(unsigned c=1;c<3;c++){if(in[c]<lo)lo=in[c];if(in[c]>hi)hi=in[c];}
 unsigned chroma=hi-lo;
 unsigned peak=(hi*133+50)/100;if(peak>255)peak=255;
 if(!chroma){for(unsigned c=0;c<3;c++)out[c]=(unsigned char)peak;return;}
 unsigned expanded=chroma*2;if(expanded>hi)expanded=hi;
 for(unsigned c=0;c<3;c++){
  unsigned value=hi-expanded+((in[c]-lo)*expanded+chroma/2)/chroma;
  out[c]=(unsigned char)((value*peak+hi/2)/hi);
 }
}
#endif
