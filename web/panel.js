// Shared firmware input path; this surface is a control-map review, not a
// dimensional drawing of the case or a physical-unit acceptance result.
const whites=[0,2,4,6,7,9,11,12];
const printed=['OCT−','OCT+','FX','SEL','ENV','LFO','EDIT','GLO','HOME','SAVE','ARP','SEQ','PLAY','REC'];
const actions=['Octave down','Octave up','Fill','Band','Sound','Then / Now','Songs','Bob / next family','Dance Hall','Keeps','Leon solo','Play keys','Play / rest','New tune'];
const chairs=['Steel','Fiddle','Guitar','Piano','Upright','Clarinet','Synth'];
const band=['Steel','Fiddle','Guitar','Piano','Bass','Drums','Clarinet','Synth'];
const cues=['Fill','Break','Leon','Theme','Ending','All in','Auto Bob','Solos'];
const names=['C','C♯','D','E♭','E','F','F♯','G','A♭','A','B♭','B'];
export function initPanel({control,audio,getState,getCalls=()=>[]}) {
 const root=document.querySelector('#fm1-panel'),buttons=root.querySelector('.panel-buttons'),encoders=root.querySelector('.panel-encoders'),keys=root.querySelector('.panel-keys');
 let generation=0;const lifts=[];
 const dispatch=async(kind,index,value)=>{const current=generation;if((kind===0&&value)||kind===2&&value)await audio();if(current!==generation)return;return control(kind,index,value);};
 const bind=(el,down,up)=>{
  let held=false,queue=Promise.resolve();
  const press=()=>{if(held)return;held=true;queue=queue.then(down).catch(console.error);};
  const lift=()=>{if(!held)return;held=false;queue=queue.then(up).catch(console.error);};
  lifts.push(lift);
  el.onpointerdown=e=>{if(e.button>0||el.disabled)return;e.preventDefault();el.setPointerCapture(e.pointerId);press();};
  el.onpointerup=el.onpointercancel=e=>{e.preventDefault();lift();};
  el.onlostpointercapture=lift;
  el.onkeydown=e=>{if(!['Space','Enter'].includes(e.code))return;e.preventDefault();e.stopPropagation();if(!e.repeat)press();};
  el.onkeyup=e=>{if(!['Space','Enter'].includes(e.code))return;e.preventDefault();e.stopPropagation();lift();};
  el.oncontextmenu=e=>e.preventDefault();
 };
 printed.forEach((name,id)=>{const el=document.createElement('button');el.dataset.panelButton=id;el.innerHTML=`<strong>${name}</strong><small>${actions[id]}</small>`;el.setAttribute('aria-label',`FM-1 ${name}: ${actions[id]}`);bind(el,()=>dispatch(0,id,1),()=>control(0,id,0));(id<2?root.querySelector('.panel-octaves'):buttons).append(el);});
 ['SELECT','ALGORITHM','KNOB 1','KNOB 2','KNOB 3','KNOB 4','PRESETS'].forEach((name,id)=>{const el=document.createElement('div');el.className='panel-encoder';const label=document.createElement('strong');label.textContent=name;const value=document.createElement('output');value.dataset.encoderValue=id;const row=document.createElement('div');for(const [sign,steps] of [['−',-1],['+',1]]){const b=document.createElement('button');b.textContent=sign;b.setAttribute('aria-label',`FM-1 ${name} ${steps>0?'clockwise':'counterclockwise'}`);b.onclick=()=>control(1,id,steps);row.append(b);}el.append(label,value,row);encoders.append(el);});
 [0,1,6,2,3,4,5].forEach(id=>encoders.append(root.querySelector(`[data-encoder-value="${id}"]`).parentElement));
 root.querySelector('.panel-master').oninput=e=>control(4,0,+e.target.value);
 let white=0;
 for(let key=0;key<27;key++){
  const pitch=53+key,isBlack=[1,3,6,8,10].includes(pitch%12),el=document.createElement('button');el.dataset.panelKey=key;el.className=isBlack?'black':'white';el.style.left=`${(isBlack?white-.31:white)*100/16}%`;el.style.width=`${(isBlack?.62:1)*100/16}%`;if(!isBlack)white++;
  const label=document.createElement('span');el.append(label);bind(el,()=>dispatch(2,key,1),()=>control(2,key,0));keys.append(el);
 }
 const release=()=>{generation++;lifts.forEach(lift=>lift());return control(3,0,0);};window.addEventListener('blur',release);document.addEventListener('visibilitychange',()=>{if(document.hidden)release();});root.addEventListener('toggle',()=>{if(!root.open)release();});
 return function updatePanel(state) {
  const p=state.panel;if(!p)return;
  const family=['AH-HA','PLAYERS','YEAH','CHEERS','MORE'][p.family??0];
  root.querySelector('.panel-page').textContent=['Play keys','Band switches','Sound','Then / Now','Bob','Songs','Temporary Keeps','Dance Hall'][p.page];
  root.querySelector('.panel-guide').textContent=p.page===6?'SAVE keeps the selected slot; PLAY recalls it and leaves transport and volume alone. Eight temporary Keeps: lost at power-off. OCT only shifts the keyboard.':p.page===4?`${family}, page ${p.bank+1}. PRESETS pages through the calls; GLO jumps to the next family. First 24 keys play calls; last three: Leon, Ah-ha and Random.`:p.page===1?'The first eight white keys switch the band players. Knob 3 changes FUNK. SEQ opens Play Keys.':p.page===7?'Cues 1–8 lead the band. Other keys play your instrument. PRESETS queues the next tune at the next bar. LFO switches Then / Now.':p.page===5?'PRESETS moves the cursor. Browsing leaves the current tune and tempo alone; PLAY confirms. The first row generates a New Tune. Knob 2 changes key; knob 3 changes the original form.':`All 27 keys play ${chairs[p.voice]}. Hold to sustain; lift to release. SELECT changes the live instrument. OCT shifts its register.`;
  root.querySelectorAll('[data-panel-button]').forEach(el=>{const id=+el.dataset.panelButton;el.setAttribute('aria-pressed',String(!!(p.ledButtons&(1<<id))||!!(p.buttons&(1<<id))));});
  if(document.activeElement!==root.querySelector('.panel-master'))root.querySelector('.panel-master').value=p.master;
  const sound=p.page===2,forms=['AABA','Swing blues','Refrain'];
  const labels=[p.page===6?`Keep ${p.slot+1}`:chairs[p.voice],`Now ${state.modern}%`,sound?`Glow ${state.glow}`:`Tempo ${state.bpm}`,sound?`Swell ${state.swell}`:p.page===5?`Key ${names[state.nextKey]}`:`Swing ${state.swing}%`,sound?`Fiddle ${state.fiddleColour}%`:[5,7].includes(p.page)?forms[state.nextForm]:p.page===4?`Auto Bob ${state.autoBob?'on':'off'}`:p.page===1?`Funk ${state.hiphop??0}%`:`Glow ${state.glow}`,`Air ${state.air}`,p.page===4?`${family} · ${p.bank+1}`:p.page===6?`Keep ${p.slot+1}`:p.page===5?`Row ${p.cursor===0?'New Tune':p.cursor}`:p.page===7?'Next tune · next bar':'Song list'];
  labels.forEach((label,id)=>{const value=root.querySelector(`[data-encoder-value="${id}"]`);value.textContent=label;for(const b of value.parentElement.querySelectorAll('button')){b.disabled=false;b.title='';}});
  keys.querySelectorAll('button').forEach(el=>{
   const key=+el.dataset.panelKey,pitch=53+key+p.octave*12,whiteIndex=whites.indexOf(key);let label=names[pitch%12]+(Math.floor(pitch/12)-1),enabled=true;
   if([1,6,7].includes(p.page)){enabled=p.page===7||whiteIndex>=0;label=whiteIndex>=0?(p.page===1?band[whiteIndex]:p.page===6?`Keep ${whiteIndex+1}`:cues[whiteIndex]):p.page===7?names[pitch%12]+(Math.floor(pitch/12)-1):'';}
   if(p.page===4){const at=p.callSlots?.[key]??65535;enabled=key>=24||at!==65535;label=key===24?'Leon':key===25?'Ah-ha':key===26?'Random':enabled?(getCalls()[at]?.pad_label??String(at+1)):'';}
   el.disabled=!enabled;el.querySelector('span').textContent=label;el.setAttribute('aria-label',`FM-1 key ${key+1}: ${label||'unused on this page'}`);el.setAttribute('aria-pressed',String(!!(p.held&(1<<key))));el.dataset.lit=String(!!(p.ledKeys&(1<<key)));
  });
 };
}
