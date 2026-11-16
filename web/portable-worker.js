import createBand from './native/bbswing.js';
import {ConcertQueue} from './concert.js';
let band,port,pcm,concert,played=0,visualTime=performance.now();
const history=[];
const externalCallBank={"file": "./calls.bank", "bytes": 7793413, "sha256": "b71e9eb5badb2edadb2e603b70e1e1debf8737b345821fe7047ea79d5d332eb0"};
const externalArtBank={"file": "./art.bank", "bytes": 81920, "sha256": "05a66dee28cdc3d4ba4e5696236ee069b4473695803dd0d775e0224bb7c09403"};
const externalTimbreBank={"file": "./timbres.bank", "bytes": 81920, "sha256": "37cef61ad3d458bfdcfddced93dea3bb41bc530dde6d49067fd35cea8320e4de"};
const ready=createBand().then(async module=>{band=module;
 if(externalTimbreBank){
  let ptr=0;
  try{
   const response=await fetch(new URL(externalTimbreBank.file,import.meta.url));if(!response.ok)throw Error('Acoustic bank unavailable');
   const bytes=new Uint8Array(await response.arrayBuffer());if(bytes.length!==externalTimbreBank.bytes)throw Error('Acoustic bank length mismatch');
   const digest=Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256',bytes)),x=>x.toString(16).padStart(2,'0')).join('');
   if(digest!==externalTimbreBank.sha256)throw Error('Acoustic bank hash mismatch');
   ptr=band._malloc(bytes.length);if(!ptr)throw Error('Acoustic bank allocation failed');band.HEAPU8.set(bytes,ptr);
   if(band._sw_timbre_bank_boot(ptr,bytes.length,0)!==1)throw Error('Acoustic bank rejected');
  }catch(error){console.warn('Using synth backup voices:',error.message);}
  finally{if(ptr)band._free(ptr);}
 }
 if(externalCallBank){
  const response=await fetch(new URL(externalCallBank.file,import.meta.url));if(!response.ok)throw Error('Reviewed sample bank unavailable');
  const bytes=new Uint8Array(await response.arrayBuffer());if(bytes.length!==externalCallBank.bytes)throw Error('Sample bank length mismatch');
  const digest=Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256',bytes)),x=>x.toString(16).padStart(2,'0')).join('');
  if(digest!==externalCallBank.sha256)throw Error('Sample bank hash mismatch');
  const ptr=band._malloc(bytes.length);if(!ptr)throw Error('Sample bank allocation failed');band.HEAPU8.set(bytes,ptr);
  if(!band._sw_host_calls_bank(ptr,bytes.length,16*1024*1024)){band._free(ptr);throw Error('Sample bank rejected');}
  // Keep immutable WASM storage alive for the lifetime of this worker.
 }
 if(externalArtBank){
  let pointer=0;
  try {
   const response=await fetch(new URL(externalArtBank.file,import.meta.url));if(!response.ok)throw Error('Art bank unavailable');
   const bytes=new Uint8Array(await response.arrayBuffer());if(bytes.length!==externalArtBank.bytes)throw Error('Art bank length mismatch');
   const digest=Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256',bytes)),x=>x.toString(16).padStart(2,'0')).join('');
   if(digest!==externalArtBank.sha256)throw Error('Art bank hash mismatch');
   pointer=band._malloc(bytes.length);if(!pointer)throw Error('Art bank allocation failed');band.HEAPU8.set(bytes,pointer);
   if(!band._sw_host_art_bank(pointer,bytes.length))throw Error('Art bank rejected');
   pointer=0; // immutable storage stays alive for this worker's lifetime
  }catch(error){band._sw_host_art_bank(0,0);console.warn('Using no-bank Showbill:',error.message);}
  finally{if(pointer)band._free(pointer);}
 }
 band._sw_host_reset(301026);band._sw_host_command(35,1);band._sw_host_panel_enable(1);pcm=band._malloc(4096*2*4);
 const catalog=await fetch(new URL('./classics-index.json',import.meta.url),{cache:'no-store'}).then(r=>r.ok?r.json():{}).catch(()=>({}));
 const calls=externalCallBank?await fetch(new URL('./call-index.json',import.meta.url),{cache:'no-store'}).then(r=>r.ok?r.json():{}).catch(()=>({})):{};
 concert=new ConcertQueue({read:rawState,send:(c,v)=>band._sw_host_command(c,v),songs:catalog.songs,calls:calls.calls});
 postMessage({ready:true});});
ready.catch(error=>postMessage({error:error.message}));
function rawState(){const current=JSON.parse(band.UTF8ToString(band._sw_host_state()));if(externalTimbreBank)current.timbreBankStatus=band._sw_timbre_bank_status();return current;}
function state(){const current=rawState();current.concert=concert.status();return current;}
function animate(){const now=performance.now();band._sw_host_visual_advance(Math.min(1000,Math.floor(now-visualTime)));visualTime=now;}
function snapshot(){const current=state(),ptr=band._sw_host_screen();history.push({frame:current.frames,pixels:band.HEAPU16.slice(ptr/2,ptr/2+57600),bobFrame:current.bobFrame,callActive:current.callActive,title:current.title,panelVisible:current.panel?.showbillPanel??1,concert:current.concert});if(history.length>32)history.shift();}
function render(){animate();for(let i=0;i<4;i++){concert.beforeRender();snapshot();band._sw_host_render(pcm+i*1024*8,1024);concert.afterRender(1024);}const samples=band.HEAPF32.slice(pcm/4,pcm/4+8192);port.postMessage({pcm:samples},[samples.buffer]);}
function screen(){
 animate();let chosen;if(port){for(const item of history){if(((played-item.frame)|0)>=0)chosen=item;else break;}}
 const current=state(),rgb=chosen?.pixels.slice()??(()=>{const ptr=band._sw_host_screen();return band.HEAPU16.slice(ptr/2,ptr/2+57600);})();
 // The shared R36 renderer owns the whole screen, including the common header.
 const rgba=new Uint8ClampedArray(57600*4);for(let i=0;i<rgb.length;i++){const pixel=rgb[i],p=i*4;rgba[p]=(pixel>>11&31)*255/31;rgba[p+1]=(pixel>>5&63)*255/63;rgba[p+2]=(pixel&31)*255/31;rgba[p+3]=255;}
 return {rgba,frame:chosen?.frame??current.frames,bobFrame:chosen?.bobFrame??current.bobFrame,concert:chosen?.concert??current.concert};
}
onmessage=async({data})=>{try{await ready;if(data.type==='audio'){port=data.port;port.onmessage=({data:stats})=>{if(stats.played!==undefined)played=stats.played;if(stats.need){postMessage({stats});render();}};render();return;}let result;if(data.type==='command'){concert.command(data.command,data.value);result=state();}else if(data.type==='concert'){concert.setEnabled(data.enabled);result=state();}else if(data.type==='panel'){concert.panel((k,i,v)=>band._sw_host_panel_control(k,i,v),data.kind,data.index,data.value);result=state();}else if(data.type==='state'){result=state();}else if(data.type==='screen'){const {rgba,frame,bobFrame,concert:view}=screen();postMessage({id:data.id,result:rgba.buffer,visualFrame:frame,bobFrame,playedFrame:played,concert:view},[rgba.buffer]);return;}postMessage({id:data.id,result});}catch(error){postMessage({id:data.id,error:error.message});}};
