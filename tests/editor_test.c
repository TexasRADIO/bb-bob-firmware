/* Host contract for the BB-BOB shared-editor foreground adapter. */
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "swing.h"
#include "panel.h"

static sw_engine band;
static sw_panel panel;
static uint8_t incoming[128];
static uint32_t incoming_length,pending;
static uint8_t sent[4][180];
static uint32_t sent_length[4],sent_count;

static uint32_t critical_begin(void){return 0;}
static void critical_end(uint32_t start){(void)start;}
static int ota_frame_get(const uint8_t **p,uint32_t *n){if(!pending)return 0;*p=incoming;*n=incoming_length;return 1;}
static void ota_frame_done(void){pending=0;}
static int ota_wire_send(const uint8_t *p,uint32_t n){assert(sent_count<4&&n<=sizeof sent[0]);memcpy(sent[sent_count],p,n);sent_length[sent_count++]=n;return 1;}
uint32_t sw_classic_slots(void){return 63;}
uint32_t sw_classic_available(uint32_t index){return index<63;}
uint32_t sw_classic_ready_count(void){return 63;}
void sw_command(sw_engine *e,uint32_t command,int32_t value){
 switch(command){
 case SW_PLAY:e->playing=!e->playing;break;case SW_TEMPO:e->bpm=(uint16_t)value;break;
 case SW_SWING:e->swing=(uint16_t)value;break;case SW_KEY:e->key_next=(uint8_t)value;break;
 case SW_FORM:e->form_next=(uint8_t)value;break;case SW_PLAYER:e->players^=(uint8_t)(1u<<value);break;
 case SW_MODERN:e->modern_target=value*.01f;break;case SW_HIPHOP:e->hiphop_target=value*.01f;break;
 case SW_AIR:e->air=value*.01f;break;case SW_GLOW:e->glow=value*.01f;break;
 case SW_SWELL:e->swell=value*.01f;break;case SW_FIDDLE_COLOUR:e->fiddle_colour=(uint8_t)value;break;
 case SW_TEXTURE:e->texture=(uint8_t)value;break;case SW_AUTO_CALL:e->auto_calls=(uint8_t)value;break;
 case SW_SOURCE_SOLOS:e->source_solos=(uint8_t)value;break;case SW_CUE:e->spoken=(uint8_t)value;break;
 case SW_CLASSIC_SELECT:e->classic_choice=(uint8_t)value;e->classic_request=1;break;default:break;
 }
}

#include "../src/editor.inc"

static void receive(uint8_t type,uint16_t seq,const uint8_t *payload,uint32_t length){
 static const uint8_t prefix[]={0,0x22,0x24,0x35,0x05};
 sent_count=0;
 memcpy(incoming,prefix,sizeof prefix);incoming[5]=type;incoming[6]=(uint8_t)(seq>>7);incoming[7]=(uint8_t)(seq&127u);
 memcpy(incoming+8,payload,length);incoming_length=8+length;pending=1;sent_count=0;bob_editor_service();
}
static void transaction(uint8_t scope,uint8_t target,uint16_t field,uint16_t value){
 uint8_t begin[]={(uint8_t)((editor.revision>>21)&127u),(uint8_t)((editor.revision>>14)&127u),
                  (uint8_t)((editor.revision>>7)&127u),(uint8_t)(editor.revision&127u),scope,0,9};
 uint8_t set[]={scope,target,(uint8_t)(field>>7),(uint8_t)(field&127u),(uint8_t)(value>>7),(uint8_t)(value&127u)};
 const uint8_t commit[]={0,9};
 receive(BB_ED_MSG_TX_BEGIN,3,begin,sizeof begin);assert(sent_count==1&&sent[0][6]==BB_ED_MSG_ACK);
 receive(BB_ED_MSG_SET,4,set,sizeof set);assert(sent_count==1&&sent[0][6]==BB_ED_MSG_APPLIED);
 receive(BB_ED_MSG_TX_COMMIT,5,commit,sizeof commit);assert(sent_count==1&&sent[0][6]==BB_ED_MSG_ACK);
}
int main(void){
 memset(&band,0,sizeof band);memset(&panel,0,sizeof panel);band.bpm=168;band.swing=59;band.players=0x3f;
 band.air=.24f;band.glow=.5f;band.swell=.28f;band.texture=70;band.auto_calls=band.source_solos=band.spoken=1;
 bob_editor_init();
 {const uint8_t hello[]={0};receive(BB_ED_MSG_HELLO,1,hello,sizeof hello);assert(sent_count==1&&sent[0][6]==BB_ED_MSG_HELLO_REPLY&&sent[0][10]==BB_ED_PRODUCT_BB_BOB);assert((((uint32_t)sent[0][11]<<7)|sent[0][12])==SW_BOB_REVISION);}
 receive(BB_ED_MSG_SNAPSHOT_BEGIN,2,0,0);assert(sent_count==2&&sent[0][6]==BB_ED_MSG_SNAPSHOT_CHUNK&&sent[1][6]==BB_ED_MSG_SNAPSHOT_END);
 transaction(7,0,ED_BPM,1230);assert(band.bpm==123);
 transaction(0x21,6,ED_PLAYER_BASE+6,1);assert((band.players&(1u<<6))!=0);
 receive(BB_ED_MSG_PLAY,6,0,0);assert(band.playing==1);receive(BB_ED_MSG_PLAY,7,0,0);assert(band.playing==1);
 {const uint8_t foreign[]={0x7d,0x42,0x42,1};memcpy(incoming,foreign,sizeof foreign);incoming_length=sizeof foreign;pending=1;bob_editor_service();assert(pending==1);}
 return 0;
}
