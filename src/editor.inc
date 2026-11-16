/* SPDX-License-Identifier: GPL-3.0-only
 * BB-BOB R34 foreground adapter for the shared Big Beat Editor protocol.
 * Include after bb_base_hw.inc so it shares the framed USB SysEx queue.
 */
#include "editor_gen.h"
#include "bob_identity.h"

enum {
 ED_BPM=0x2001,ED_SWING=0x2002,ED_TUNE_SELECT=0x2101,ED_KEY=0x2103,ED_FORM=0x2104,
 ED_PLAYER_BASE=0x2200,ED_MODERN=0x2220,ED_HIPHOP=0x2221,ED_AIR=0x2222,ED_GLOW=0x2223,
 ED_SWELL=0x2224,ED_FIDDLE=0x2225,ED_TEXTURE=0x2226,ED_AUTO_CALLS=0x2227,
 ED_SOURCE_SOLOS=0x2228,ED_SPOKEN=0x2229
};
enum { ED_ACTION_TUNE=0xfeu };
typedef struct {
 uint8_t connected,tx_open,domain,staged_valid,kind,target;
 uint16_t tx_id,value;
 uint32_t revision;
 uint8_t reply[160];uint16_t reply_len;
} BobEditor;
static BobEditor editor;

static uint16_t ed_read14(const uint8_t *p){return (uint16_t)(((uint16_t)(p[0]&127u)<<7)|(p[1]&127u));}
static uint32_t ed_read28(const uint8_t *p){return (uint32_t)(p[0]&127u)<<21|(uint32_t)(p[1]&127u)<<14|(uint32_t)(p[2]&127u)<<7|(p[3]&127u);}
static uint32_t ed_crc_byte(uint32_t crc,uint8_t value){crc^=value;for(unsigned i=0;i<8;i++)crc=(crc>>1)^(0xEDB88320u&(uint32_t)-(int32_t)(crc&1u));return crc;}
static uint32_t ed_crc(const uint8_t *p,unsigned n){uint32_t crc=0xffffffffu;while(n--)crc=ed_crc_byte(crc,*p++);return crc^0xffffffffu;}
static void ed_begin(uint8_t type,uint16_t seq){static const uint8_t h[]={0xf0,BB_ED_MFR_0,BB_ED_MFR_1,BB_ED_MFR_2,BB_ED_DEVICE,BB_ED_FAMILY};editor.reply_len=0;for(unsigned i=0;i<sizeof h;i++)editor.reply[editor.reply_len++]=h[i];editor.reply[editor.reply_len++]=type;editor.reply[editor.reply_len++]=(uint8_t)(seq>>7);editor.reply[editor.reply_len++]=(uint8_t)(seq&127u);}
static int ed_byte(uint8_t value){if(value>127u||editor.reply_len>=sizeof editor.reply-1u)return 0;editor.reply[editor.reply_len++]=value;return 1;}
static void ed14(uint16_t value){ed_byte((uint8_t)(value>>7));ed_byte((uint8_t)(value&127u));}
static void ed32(uint32_t value){ed_byte((uint8_t)((value>>28)&15u));ed_byte((uint8_t)((value>>21)&127u));ed_byte((uint8_t)((value>>14)&127u));ed_byte((uint8_t)((value>>7)&127u));ed_byte((uint8_t)(value&127u));}
static void ed_end(void){editor.reply[editor.reply_len++]=0xf7u;if(ota_wire_send(editor.reply,editor.reply_len))editor.reply_len=0;}
static void ed_result(uint8_t type,uint16_t seq,uint8_t result){ed_begin(type,seq);ed_byte(result);ed_end();}

typedef struct{uint8_t bytes[80];uint16_t length;} BobSnapshot;
static int ed_put8(BobSnapshot *s,uint8_t value){if(s->length>=sizeof s->bytes)return 0;s->bytes[s->length++]=value;return 1;}
static int ed_put16(BobSnapshot *s,uint16_t value){return ed_put8(s,(uint8_t)(value>>8))&&ed_put8(s,(uint8_t)value);}
static int ed_put32(BobSnapshot *s,uint32_t value){return ed_put8(s,(uint8_t)(value>>24))&&ed_put8(s,(uint8_t)(value>>16))&&ed_put8(s,(uint8_t)(value>>8))&&ed_put8(s,(uint8_t)value);}
static int ed_section(BobSnapshot *s,uint8_t scope){if(!ed_put8(s,scope)||!ed_put8(s,0)||!ed_put8(s,0))return -1;return (int)s->length-2;}
static void ed_section_end(BobSnapshot *s,int at){uint16_t n=(uint16_t)(s->length-(unsigned)at-2u);s->bytes[at]=(uint8_t)(n>>8);s->bytes[at+1]=(uint8_t)n;}
static uint8_t ed_percent(float value){int v=(int)(value*100.f+.5f);return (uint8_t)(v<0?0:v>100?100:v);}
static int ed_snapshot(BobSnapshot *s){
 int at;s->length=0;if(!ed_put32(s,editor.revision))return 0;
 at=ed_section(s,1);if(at<0||!ed_put16(s,(uint16_t)(band.bpm*10u))||!ed_put8(s,(uint8_t)band.swing))return 0;ed_section_end(s,at);
 at=ed_section(s,9);if(at<0||!ed_put8(s,band.playing)||!ed_put16(s,band.bar)||!ed_put8(s,band.bar<band.song.count?band.song.bars[band.bar].section:0))return 0;ed_section_end(s,at);
 at=ed_section(s,0x20);if(at<0||!ed_put8(s,band.song.classic_id)||!ed_put8(s,band.classic_mode?(uint8_t)(band.classic_choice+1u):0)||!ed_put8(s,(uint8_t)sw_classic_ready_count())||!ed_put8(s,band.key_next)||!ed_put8(s,band.form_next))return 0;ed_section_end(s,at);
 at=ed_section(s,0x21);if(at<0||!ed_put8(s,band.players))return 0;ed_section_end(s,at);
 at=ed_section(s,0x22);if(at<0||!ed_put8(s,ed_percent(band.modern_target))||!ed_put8(s,ed_percent(band.hiphop_target))||!ed_put8(s,ed_percent(band.air))||!ed_put8(s,ed_percent(band.glow))||!ed_put8(s,ed_percent(band.swell))||!ed_put8(s,band.fiddle_colour)||!ed_put8(s,band.texture)||!ed_put8(s,band.auto_calls)||!ed_put8(s,band.source_solos)||!ed_put8(s,band.spoken))return 0;ed_section_end(s,at);
 return 1;
}
static unsigned ed_pack7(const uint8_t *in,unsigned n,uint8_t *out,unsigned capacity){unsigned at=0,pos=0;while(pos<n){unsigned count=n-pos>7u?7u:n-pos;uint8_t msb=0;if(at+count+1u>capacity)return 0;for(unsigned i=0;i<count;i++)if(in[pos+i]&128u)msb|=(uint8_t)(1u<<i);out[at++]=msb;for(unsigned i=0;i<count;i++)out[at++]=in[pos+i]&127u;pos+=count;}return at;}
static void ed_send_snapshot(uint16_t seq){BobSnapshot snapshot;uint8_t packed[96];if(!ed_snapshot(&snapshot)){ed_result(BB_ED_MSG_REJECT,seq,BB_ED_RESULT_REFUSED);return;}unsigned n=ed_pack7(snapshot.bytes,snapshot.length,packed,sizeof packed);if(!n){ed_result(BB_ED_MSG_REJECT,seq,BB_ED_RESULT_REFUSED);return;}ed_begin(BB_ED_MSG_SNAPSHOT_CHUNK,seq);ed14(0);ed14(1);for(unsigned i=0;i<n;i++)ed_byte(packed[i]);ed32(ed_crc(snapshot.bytes,snapshot.length));ed_end();ed_begin(BB_ED_MSG_SNAPSHOT_END,seq);ed_end();}

static int ed_map(uint8_t scope,uint8_t target,uint16_t field,uint16_t value){
 editor.kind=editor.target=0;editor.value=value;
 if(scope==7u&&field==ED_BPM&&value>=800u&&value<=2200u&&value%10u==0u)editor.kind=SW_TEMPO;
 else if(scope==7u&&field==ED_SWING&&value>=50u&&value<=68u)editor.kind=SW_SWING;
 else if(scope==0x20u&&field==ED_TUNE_SELECT&&value<=sw_classic_slots()&&(!value||sw_classic_available(value-1u)))editor.kind=ED_ACTION_TUNE;
 else if(scope==0x20u&&field==ED_KEY&&value<12u)editor.kind=SW_KEY;
 else if(scope==0x20u&&field==ED_FORM&&value<3u)editor.kind=SW_FORM;
 else if(scope==0x21u&&target<8u&&field==ED_PLAYER_BASE+target&&value<2u){editor.kind=SW_PLAYER;editor.target=target;}
 else if(scope==0x22u&&field>=ED_MODERN&&field<=ED_TEXTURE&&value<=100u)editor.kind=(uint8_t)(field==ED_MODERN?SW_MODERN:field==ED_HIPHOP?SW_HIPHOP:field==ED_AIR?SW_AIR:field==ED_GLOW?SW_GLOW:field==ED_SWELL?SW_SWELL:field==ED_FIDDLE?SW_FIDDLE_COLOUR:SW_TEXTURE);
 else if(scope==0x22u&&field>=ED_AUTO_CALLS&&field<=ED_SPOKEN&&value<2u)editor.kind=(uint8_t)(field==ED_AUTO_CALLS?SW_AUTO_CALL:field==ED_SOURCE_SOLOS?SW_SOURCE_SOLOS:SW_CUE);
 else return 0;return 1;
}
static uint8_t ed_apply(void){
 uint32_t start=critical_begin();
 if(editor.kind==SW_PLAYER){uint8_t on=(band.players>>editor.target)&1u;if(on!=editor.value)sw_command(&band,SW_PLAYER,editor.target);}
 else if(editor.kind==ED_ACTION_TUNE){
  band.pending_ready=0;band.classic_request=0;panel.request=0;
  if(editor.value){band.classic_mode=1;sw_command(&band,SW_CLASSIC_SELECT,(int32_t)editor.value-1);}
  else{band.classic_mode=0;panel.seed=band.song.seed*1664525u+1013904223u;panel.request=1;}
 }else{
  int32_t value=editor.kind==SW_TEMPO?(int32_t)(editor.value/10u):(int32_t)editor.value;
  sw_command(&band,editor.kind,value);
  if((editor.kind==SW_KEY||editor.kind==SW_FORM)&&!band.classic_mode){band.pending_ready=0;band.classic_request=0;panel.seed=band.song.seed;panel.request=3;}
 }
 critical_end(start);return BB_ED_RESULT_QUEUED;
}
static void ed_rx(const uint8_t *data,unsigned length){
 if(length<3u)return;uint8_t type=data[0];uint16_t seq=ed_read14(data+1);const uint8_t *p=data+3;unsigned n=length-3u;
 if(type==BB_ED_MSG_HELLO){if(!n||p[0]!=BB_ED_PROTOCOL_VERSION){ed_result(BB_ED_MSG_REJECT,seq,BB_ED_RESULT_NOT_SUPPORTED);return;}ed_begin(BB_ED_MSG_HELLO_REPLY,seq);ed_byte(BB_ED_PROTOCOL_VERSION);ed_byte(BB_ED_PRODUCT_BB_BOB);ed14(SW_BOB_REVISION);ed32(0x424f4231u);ed_byte(112);ed_byte(0x45);ed14(63);ed14(0);ed14(0);ed_end();editor.connected=1;return;}
 if(type==BB_ED_MSG_SNAPSHOT_BEGIN){ed_send_snapshot(seq);return;}
 if(type==BB_ED_MSG_PING){ed_begin(BB_ED_MSG_PONG,seq);ed_end();return;}
 if(type==BB_ED_MSG_TX_BEGIN){if(!editor.connected||n<7u||editor.tx_open){ed_result(BB_ED_MSG_REJECT,seq,BB_ED_RESULT_REFUSED);return;}uint32_t base=ed_read28(p);if(base&&base!=editor.revision){ed_begin(BB_ED_MSG_CONFLICT,seq);ed32(editor.revision);ed_end();return;}editor.domain=p[4];editor.tx_id=ed_read14(p+5);editor.tx_open=1;editor.staged_valid=0;ed_result(BB_ED_MSG_ACK,seq,BB_ED_RESULT_OK);return;}
 if(type==BB_ED_MSG_SET){if(!editor.tx_open||editor.staged_valid||n<6u||p[0]!=editor.domain||!ed_map(p[0],p[1],ed_read14(p+2),ed_read14(p+4))){ed_result(BB_ED_MSG_REJECT,seq,BB_ED_RESULT_UNKNOWN_FIELD);return;}editor.staged_valid=1;ed_result(BB_ED_MSG_APPLIED,seq,BB_ED_RESULT_OK);return;}
 if(type==BB_ED_MSG_TX_COMMIT){if(!editor.tx_open||!editor.staged_valid||n<2u||ed_read14(p)!=editor.tx_id){ed_result(BB_ED_MSG_REJECT,seq,BB_ED_RESULT_REFUSED);return;}uint8_t result=ed_apply();editor.revision++;editor.tx_open=editor.staged_valid=0;ed_result(BB_ED_MSG_ACK,seq,result);return;}
 if(type==BB_ED_MSG_TX_ABORT){editor.tx_open=editor.staged_valid=0;ed_result(BB_ED_MSG_ACK,seq,BB_ED_RESULT_OK);return;}
 if(type==BB_ED_MSG_PLAY||type==BB_ED_MSG_STOP){uint8_t want=type==BB_ED_MSG_PLAY;if((uint8_t)band.playing!=want){uint32_t start=critical_begin();sw_command(&band,SW_PLAY,0);critical_end(start);}ed_result(BB_ED_MSG_ACK,seq,BB_ED_RESULT_OK);return;}
 if(type==BB_ED_MSG_SETTLE){ed_begin(BB_ED_MSG_SETTLE_REPLY,seq);ed32(editor.revision);ed_byte(editor.tx_open);ed_end();return;}
 ed_result(BB_ED_MSG_REJECT,seq,BB_ED_RESULT_NOT_SUPPORTED);
}
static void bob_editor_init(void){memset(&editor,0,sizeof editor);editor.revision=1;}
static void bob_editor_service(void){const uint8_t *frame;uint32_t length;if(ota_frame_get(&frame,&length)&&length>=8u&&frame[0]==BB_ED_MFR_0&&frame[1]==BB_ED_MFR_1&&frame[2]==BB_ED_MFR_2&&frame[3]==BB_ED_DEVICE&&frame[4]==BB_ED_FAMILY){ed_rx(frame+5u,(unsigned)(length-5u));ota_frame_done();}}
