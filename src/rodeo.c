/* SPDX-License-Identifier: GPL-3.0-only
 * PR86 r2 Rodeo glyphs; BB-BOB adds clipped drawing and compact key sizes. */
#include "rodeo.h"
#include "rodeo_atlas.h"
#include "rodeo_key_atlas.h"
#define GAP 2u
static void emit(sw_rectangle r,void *c,int64_t x,int64_t y,int w,int h,uint16_t ink) {
 if(!r||x>=240||y>=240||x+w<=0||y+h<=0||w<=0||h<=0)return;
 if(x<0){w+=(int)x;x=0;}if(y<0){h+=(int)y;y=0;}
 if(w>240-x)w=(int)(240-x);
 if(h>240-y)h=(int)(240-y);
 if(w>0&&h>0)r((int)x,(int)y,w,h,ink,c);
}
static int glyph(char ch) {
 if(ch>='a'&&ch<='z')ch=(char)(ch-32);
 for(uint32_t i=0;i<RODEO_GLYPHS;i++)if(rodeo_chars[i]==ch)return (int)i;
 return 36;
}
uint32_t sw_rodeo_height(void){return RODEO_H;}
int sw_rodeo_width(const char *s,int scale) {
 if(!s||scale<1||scale>4)return 0;
 int w=0;for(;*s;s++)w+=(int)(rodeo_data[rodeo_offset[glyph(*s)]]+GAP)*scale;
 return w?w-(int)GAP*scale:0;
}
int sw_rodeo_text(sw_rectangle r,void *c,int x,int y,const char *s,int scale,uint16_t ink) {
 if(!s||scale<1||scale>4)return 0;
 int64_t xx=x;int width=sw_rodeo_width(s,scale);
 for(;*s;s++) {
  const uint8_t *g=rodeo_data+rodeo_offset[glyph(*s)];uint32_t w=g[0],stride=(w+7u)/8u;g++;
  for(uint32_t row=0;row<RODEO_H;row++)for(uint32_t col=0;col<w;) {
   if(!(g[row*stride+col/8u]&(0x80u>>(col%8u)))){col++;continue;}
   uint32_t end=col;while(end<w&&(g[row*stride+end/8u]&(0x80u>>(end%8u))))end++;
   emit(r,c,xx+(int)col*scale,(int64_t)y+(int)row*scale,(int)(end-col)*scale,scale,ink);col=end;
  }
  xx+=(int)(w+GAP)*scale;
 }
 return width;
}
static const uint8_t KEY_LETTER[12]={2,2,3,4,4,5,5,6,0,0,1,1},KEY_ACC[12]={0,7,0,8,0,0,7,0,8,0,8,0};
static int key_glyph(sw_rectangle r,void *c,int64_t x,int64_t y,uint32_t i,uint16_t ink,int cap,int draw) {
 const uint8_t *g=rodeo_key_data+rodeo_key_offset[i];uint32_t sw=g[0],sh=g[1],stride=(sw+7u)/8u;g+=2;
 int w=((int)sw*cap+13)/26,h=((int)sh*cap+13)/26;
 /* The supplied sharp is an exact 2x 5x7 glyph. Half-size accidentals retain
    both crossbars and the flat's stem/bowl beside the compact Rodeo letter. */
 if(i>=7&&cap<=16){w=((int)sw+1)/2;h=7;}
 if(draw)for(int row=0;row<h;row++)for(int col=0;col<w;) {
  uint32_t sy=(uint32_t)((2*row+1)*(int)sh/(2*h)),sx=(uint32_t)((2*col+1)*(int)sw/(2*w));
  if(!(g[sy*stride+sx/8u]&(0x80u>>(sx%8u)))){col++;continue;}
  int begin=col++;
  while(col<w){sx=(uint32_t)((2*col+1)*(int)sw/(2*w));if(!(g[sy*stride+sx/8u]&(0x80u>>(sx%8u))))break;col++;}
  emit(r,c,x+begin,y+row,col-begin,1,ink);
 }
 return w;
}
static int key_draw(sw_rectangle r,void *c,int x,int y,uint32_t pc,uint16_t ink,int cap,int draw) {
 if(cap<9||cap>26)return 0;
 pc%=12u;int w=key_glyph(r,c,x,y,KEY_LETTER[pc],ink,cap,draw);
 if(KEY_ACC[pc]){int gap=cap>=20?2:1;w+=gap+key_glyph(r,c,(int64_t)x+w+gap,(int64_t)y-gap,KEY_ACC[pc],ink,cap,draw);}
 return w;
}
int sw_rodeo_key(sw_rectangle r,void *c,int x,int y,uint32_t pc,uint16_t ink){return key_draw(r,c,x,y,pc,ink,26,1);}
int sw_rodeo_key_width(uint32_t pc){return key_draw(0,0,0,0,pc,0,26,0);}
int sw_rodeo_key_size(sw_rectangle r,void *c,int x,int y,uint32_t pc,int cap,uint16_t ink){return key_draw(r,c,x,y,pc,ink,cap,1);}
int sw_rodeo_key_width_size(uint32_t pc,int cap){return key_draw(0,0,0,0,pc,0,cap,0);}
