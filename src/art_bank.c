/* SPDX-License-Identifier: GPL-3.0-only */
#include "art_bank.h"
#include "bank_internal.h"
#define TAG 0x31545241u
static uint32_t u16(const uint8_t *p){return p[0]|((uint32_t)p[1]<<8);}
static uint32_t u32(const uint8_t *p){return u16(p)|(u16(p+2)<<16);}
static const uint8_t *entry(const uint8_t *b,uint32_t i){return b+64+i*48;}
static uint32_t image_slot(uint32_t id){
 if(id>=0x100&&id<=0x102)return 27+id-0x100;
 if(id>=0x200&&id<=0x201)return 30+id-0x200;
 uint32_t who=(id>>8)&255u,pose=id&255u;
 return (id&0xffff0000u)==0x10000u&&who>=1&&who<=9&&pose<3?(who-1)*3+pose:SW_ART_IMAGES;
}
static int row_ok(const uint8_t *b,const uint8_t *q){
 uint32_t w=u16(q+32),h=u16(q+34),len=u32(q+20),rows=2*(h+1);
 if(len<rows)return 0;
 const uint8_t *p=b+u32(q+16);uint32_t tokens=len-rows;
 if(u16(p)||u16(p+2*h)!=tokens)return 0;
 for(uint32_t y=0;y<h;y++) {uint32_t a=u16(p+2*y),end=u16(p+2*y+2),col=0;
  if(a>end||end>tokens)return 0;
  while(a<end) {col+=(p[rows+a++]>>5)+1u;if(col>w)return 0;}
  if(col!=w)return 0;
 }return 1;
}
uint32_t sw_art_bank_context_bytes(void){return sizeof(sw_art_bank);}
int sw_art_bank_open(sw_art_bank *out,const uint8_t *b,uint32_t n,void (*poll)(void)){
 static const uint8_t profile[8]={'B','B','A','R','T','0','1',0};
 if(!out)return 0;for(uint32_t i=0;i<sizeof *out;i++)((uint8_t *)out)[i]=0;
 if(!b||n!=SW_ART_BANK_BYTES)return 0;
 for(uint32_t i=0;i<8;i++)if(b[40+i]!=profile[i])return 0;
 for(uint32_t i=48;i<64;i++)if(b[i])return 0;
 sw_bank view;if(!sw_bank_open_art_envelope(&view,b,n,SW_ART_BANK_BYTES,poll)||view.count!=83||view.calls)return 0;
 uint32_t strings_end=64+83*48,last=strings_end;uint8_t seen[83]={0};
 for(uint32_t i=0;i<83;i++) {const uint8_t *q=entry(b,i);
  for(uint32_t k=36;k<=40;k+=4) {uint32_t end=u32(q+k);while(b[end])end++;if(++end>strings_end)strings_end=end;}
 }
 last=strings_end;
 for(uint32_t i=0;i<83;i++) {
  if(poll)poll();const uint8_t *q=entry(b,i);uint32_t id=u32(q+8),kind=u16(q),codec=u16(q+2),off=u32(q+16),len=u32(q+20),key;
  if(u32(q+12)!=TAG||off&1u||off<last)return 0;
  for(uint32_t z=last;z<off;z++)if(b[z]!=255)return 0;last=off+len;
  if(id>=0x30000&&id<=0x30005) {
   key=id-0x30000;if(kind!=SW_BANK_PALETTE||codec!=SW_BANK_RAW||u32(q+4)||u32(q+44)!=0xffffffffu)return 0;
  } else if(id>=0x40000&&id<0x4002d) {
   key=6+id-0x40000;if(kind!=SW_BANK_ART||codec!=SW_BANK_RLE5)return 0;
   uint32_t x=u16(q+4),y=u16(q+6);if(x+u16(q+32)>72||y+u16(q+34)>72)return 0;
  } else {
   uint32_t slot=image_slot(id);if(slot>=SW_ART_IMAGES||kind!=SW_BANK_ART||u32(q+4))return 0;
   key=51+slot;
   if(slot<27) {if(codec!=SW_BANK_LAYERS||len!=8||u16(q+32)!=72||u16(q+34)!=72||u32(q+44)!=0xffffffffu)return 0;}
   else if(codec!=SW_BANK_RLE5||(slot<30&&(u16(q+32)!=240||u16(q+34)!=240))||(slot>=30&&(u16(q+32)>120||u16(q+34)>120)))return 0;
  }
  if(seen[key]++)return 0;
  if(codec==SW_BANK_RLE5) {
   uint32_t pi=u32(q+44);if(pi>=83)return 0;const uint8_t *pal=entry(b,pi);
   uint32_t want=id>=0x40000?0x30000:id<0x200?0x30001+id-0x100:0x30004+id-0x200;
   if(u16(pal)!=SW_BANK_PALETTE||u32(pal+8)!=want||!row_ok(b,q))return 0;
  } else if(codec==SW_BANK_LAYERS) {
   const uint8_t *p=b+off;uint32_t count=u16(p);if(count<2||count>3)return 0;
   for(uint32_t j=0;j<3;j++) {uint32_t ref=u16(p+2+j*2);if(j>=count) {if(ref!=0xffff)return 0;continue;}
    if(ref>=83)return 0;const uint8_t *layer=entry(b,ref);
    if(u16(layer+2)!=SW_BANK_RLE5||u32(layer+8)<0x40000||u32(layer+8)>=0x4002d)return 0;
    for(uint32_t k=0;k<j;k++)if(ref==u16(p+2+k*2))return 0;
   }
  }
 }
 for(uint32_t i=0;i<83;i++)if(!seen[i])return 0;
 for(uint32_t i=last;i<n;i++)if(b[i]!=255)return 0;
 /* Publish the local view only after every structure/row/reference is valid. */
 out->bank=view;
 for(uint32_t i=0;i<83;i++) {const uint8_t *q=entry(b,i);uint32_t slot=image_slot(u32(q+8));if(slot>=SW_ART_IMAGES)continue;
  sb_image_t *im=&out->images[slot];im->width=(uint16_t)u16(q+32);im->height=(uint16_t)u16(q+34);im->bank_base=b;im->bank_entry=q;
  if(slot<27) {uint32_t who=slot/3+1,pose=slot%3;out->art.stage[who][pose]=im;out->art.corner[who][pose]=im;out->art.close[who][pose]=im;}
  else if(slot<30)out->art.bg[slot-27]=im;else if(slot==30)out->art.bob_idle=im;else out->art.bob_speak=im;
 }return 1;
}
static void box(sw_rectangle rect,void *context,int x,int y,int w,int h,uint16_t colour){
 if(x<0){w+=x;x=0;}if(y<0){h+=y;y=0;}if(x+w>240)w=240-x;if(y+h>240)h=240-y;
 if(w>0&&h>0)rect(x,y,w,h,colour,context);
}
static void plane(const uint8_t *b,const uint8_t *q,sw_rectangle rect,void *context,int x,int y,int scale){
 uint32_t h=u16(q+34),rows=2*(h+1);const uint8_t *p=b+u32(q+16),*runs=p+rows,*pal=b+u32(entry(b,u32(q+44))+16);
 x+=(int)u16(q+4)*scale;y+=(int)u16(q+6)*scale;
 for(uint32_t row=0;row<h;row++) {int yy=y+(int)row*scale;if(yy>=240||yy+scale<=0)continue;
  uint32_t at=u16(p+row*2),end=u16(p+row*2+2),col=0;
  while(at<end){uint8_t t=runs[at++];uint32_t px=t&31u,stop=col+(t>>5)+1u;
   while(at<end&&(runs[at]&31u)==px)stop+=(runs[at++]>>5)+1u;
   if(px)box(rect,context,x+(int)col*scale,yy,(int)(stop-col)*scale,scale,(uint16_t)u16(pal+px*2));col=stop;
  }
 }
}
void sw_art_bank_blit(const sb_image_t *im,sw_rectangle rect,void *context,int x,int y,int scale){
 if(!im||!rect||!im->bank_base||!im->bank_entry||scale<1||scale>3||x< -720||x>240||y< -720||y>240)return;
 const uint8_t *b=im->bank_base,*q=im->bank_entry;
 if(u16(q+2)==SW_BANK_RLE5)plane(b,q,rect,context,x,y,scale);
 else {const uint8_t *p=b+u32(q+16);for(uint32_t j=0;j<u16(p);j++)plane(b,entry(b,u16(p+2+j*2)),rect,context,x,y,scale);}
}
