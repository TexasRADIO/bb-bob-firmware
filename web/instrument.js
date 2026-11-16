import {initPanel} from './panel.js';
const $=s=>document.querySelector(s),players=['STEEL','FIDDLE','RHYTHM GUITAR','PIANO','UPRIGHT BASS','DRUMS','CLARINET','SYNTH'],symbols=['≈','♬','≋','▥','●','◉','♮','∿'];
let updatePanel=()=>{},state={},context,node,analyser,worker,screenBusy=false,lastRevision=0,underruns=0,portable,customCall,callBank={calls:[]},lastCall='',hollerIndex=-1,callPage=0,scoreBank={parts:[]},classicBank={songs:[]};
fetch('./identity.json').then(r=>{if(!r.ok)throw Error('identity');return r.json()}).then(id=>{document.title=id.display_version+' · Bob Wills in your pocket';$('#instrument-identity').textContent=id.display_version+' · '+id.product_id;}).catch(()=>{$('#instrument-identity').textContent='BB-BOB · identity unavailable';});
const portableReady=document.documentElement.dataset.engine==='wasm'?import('./portable-transport.js').then(async({PortableBand})=>{portable=new PortableBand();await portable.ready;}):Promise.resolve();
const controls=[['Tempo',8,80,220,168],['Swing',9,50,68,59],['Now',10,0,100,0],['Air',11,0,100,24],['Glow',12,0,100,50],['Swell',13,0,100,28]];
players.forEach((p,i)=>{const b=document.createElement('button');b.className='player';b.id='player-'+i;b.setAttribute('aria-pressed',i<6?'true':'false');b.innerHTML=`<span class="symbol" aria-hidden="true">${symbols[i]}</span><span class="player-name">${p}</span>`;b.onclick=()=>command(7,i);$('#players').append(b);});
controls.forEach(([name,id,min,max,value])=>{const l=document.createElement('label');l.className='knob';l.innerHTML=`<span>${name}<output id="out-${id}">${value}</output></span><input aria-label="${name}" type="range" min="${min}" max="${max}" value="${value}" id="control-${id}">`;l.querySelector('input').oninput=e=>command(id,+e.target.value);$(id===10?'#now-control':'#knobs').append(l);});
document.querySelector('.top').after(document.querySelector('#fm1-panel'));
document.querySelector('.top .settings').after($('#fiddle-controls'));
$('#now-playing').after(document.querySelector('.mode-row'));document.querySelector('.mode-row').after($('#classics-library'));
const nowViewport=matchMedia('(max-width:700px)');function placeNow(){const panel=$('#now-zone');if(nowViewport.matches)document.querySelector('.transport').after(panel);else $('#knobs').before(panel);const style=document.querySelector('#style-controls');if(style)panel.after(style);}nowViewport.addEventListener('change',placeNow);placeNow();
['C','C#','D','Eb','E','F','F#','G','Ab','A','Bb','B'].forEach((k,i)=>$('#key').add(new Option(k,i,i===2,i===2)));
const stylePanel=document.createElement('section');stylePanel.id='style-controls';stylePanel.className='fiddle-controls';
stylePanel.innerHTML='<h2>FUNK</h2><p>Choose how the band plays. Then → Now chooses its sound.</p><label class="knob"><span>FUNK <output id="hiphop-value">0%</output></span><input id="hiphop-control" aria-label="Funk style" type="range" min="0" max="100" value="0"></label><p>BAND → KNOB 3 on FM-1. Keeps recall Funk and Then → Now together.</p>';
$('#now-zone').after(stylePanel);$('#hiphop-control').oninput=e=>command(42,+e.target.value);
$('#key').onchange=()=>command(14,+$('#key').value);$('#form').onchange=()=>command(15,+$('#form').value);
for(let i=0;i<12;i++){const b=document.createElement('button');b.textContent=['C','C#','D','Eb','E','F','F#','G','Ab','A','Bb','B'][i];b.setAttribute('aria-label','Play steel note '+b.textContent);b.onpointerdown=async()=>{await audio();await command(17,60+i);};$('#keys').append(b);}
document.querySelectorAll('[data-command]').forEach(b=>b.onclick=()=>command(+b.dataset.command));
async function command(command,value=0){await portableReady;if(portable){state=await portable.command(command,value);}else{const r=await fetch('/api/command',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({command,value})});if(!r.ok)throw Error('Control failed');state=await r.json();}update();return state;}
async function panelControl(kind,index,value=0){await portableReady;if(portable)state=await portable.request('panel',{kind,index,value});else{const r=await fetch('/api/panel',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({kind,index,value})});if(!r.ok)throw Error('Panel control failed');state=await r.json();}update();return state;}
async function setConcert(enabled){await portableReady;if(portable){state=await portable.concert(enabled);update();}return state;}
$('#concert-enabled').onchange=()=>setConcert($('#concert-enabled').checked);
function concertView(){return state.concert?.armed?(portable?.visual?.concert??state.concert):state.concert;}
updatePanel=initPanel({control:panelControl,audio,getState:()=>state,getCalls:()=>callBank.calls});
async function audio(){await portableReady;if(!context){context=new AudioContext({sampleRate:44100,latencyHint:'interactive'});if(context.sampleRate!==44100)throw Error('This listening surface needs 44.1 kHz output');await context.audioWorklet.addModule(new URL('./worklet.js',import.meta.url));node=new AudioWorkletNode(context,'band-output',{outputChannelCount:[2]});analyser=context.createAnalyser();analyser.fftSize=256;node.connect(analyser).connect(context.destination);if(portable){portable.attach(node.port,stats=>{underruns=stats.underruns??0;});}else{worker=new Worker(new URL('./stream-worker.js',import.meta.url));worker.onmessage=e=>{if(e.data.underruns!==undefined)underruns=e.data.underruns;if(e.data.error)$('#audio').textContent=e.data.error;};worker.postMessage({port:node.port},[node.port]);}}await context.resume();}
$('#play').onclick=async()=>{try{await audio();await command(0);}catch(e){$('#status').textContent=e.message;$('#status').classList.add('error');}};
document.querySelectorAll('[data-modern]').forEach(b=>b.onclick=()=>command(10,+b.dataset.modern));
$('#new').onclick=async()=>{const next=(Math.imul((state.seed??301026)^Date.now(),1664525)+1013904223)>>>0;await command(1,next|0);};
$('#leon').onclick=async()=>{await audio();await command(2);};
$('#leon-record').onclick=async()=>{await audio();await command(26);};
$('#random-call').onclick=async()=>{await audio();await command(21);};
function recordedBand(bank){
 if(!bank.samples?.length)return;
 const crate=document.createElement('details');crate.className='call-info';crate.id='recorded-band';
 const title=document.createElement('summary');title.textContent='RECORDED BAND · '+bank.samples.length+' kept samples';
 const info=document.createElement('p');info.textContent='Intact instrumental features and rhythm textures, with their pickups and tails. Play at the original speed, over the band or while it rests.';
 const filter=document.createElement('select');filter.id='sample-class';filter.setAttribute('aria-label','Recorded sample type');for(const [v,t] of [['all','ALL SAMPLES'],['feature','LEAD FEATURES'],['backing','RHYTHM TEXTURES']])filter.add(new Option(t,v));
 const select=document.createElement('select');select.id='band-sample';select.setAttribute('aria-label','Recorded band sample');select.style.maxWidth='100%';
 const source=document.createElement('p');source.id='band-sample-source';
 const role=s=>({ensemble_rhythm:'Rhythm section',ensemble_piano:'Piano and rhythm',ensemble_band:'Full band'})[s.role]??s.role;
 const player=s=>s.character_id&&s.character_id!=='ensemble'?s.character_id.toUpperCase()+(s.character_assignment_status==='working_match'?' (working match)':''):'';
 const live=document.createElement('p');live.id='band-sample-status';live.setAttribute('aria-live','polite');live.textContent='Original speed';
 const play=document.createElement('button');play.id='band-sample-play';play.textContent='PLAY ORIGINAL';play.onclick=async()=>{await audio();await command(39,+select.value);await command(40,0);};
 const stop=document.createElement('button');stop.id='band-sample-stop';stop.textContent='STOP SAMPLE';stop.onclick=()=>command(41,0);
 const detail=()=>{const s=bank.samples[+select.value];source.textContent=s?`${s.source_title} · ${role(s)} · ${player(s)?player(s)+' · ':''}${s.duration_seconds.toFixed(1)}s`:'No sample';};
 const fill=()=>{select.replaceChildren();for(const [i,s] of bank.samples.entries())if(filter.value==='all'||filter.value===s.class)select.add(new Option(`${role(s)} · ${s.source_title}${player(s)?' · '+player(s):''}`,i));detail();if(select.value!=='')command(39,+select.value);};
 filter.onchange=fill;select.onchange=()=>{detail();command(39,+select.value);};
 crate.append(title,info,filter,select,source,play,stop,live);$('#leon-record').closest('.gestures').after(crate);fill();
}
function callPads(){const pages=Math.ceil(callBank.calls.length/16);callPage=Math.max(0,Math.min(pages-1,callPage));$('#bob-pads').replaceChildren();$('#calls-page').textContent=`Calls ${callPage*16+1}–${Math.min((callPage+1)*16,callBank.calls.length)} / ${callBank.calls.length}`;$('#calls-prev').disabled=callPage===0;$('#calls-next').disabled=callPage===pages-1;for(let i=callPage*16;i<Math.min((callPage+1)*16,callBank.calls.length);i++){const call=callBank.calls[i],b=document.createElement('button');b.className='bob-pad';b.dataset.call=i;const label=document.createElement('strong');label.textContent=(i+1)+' '+(call.pad_label??call.id);const words=document.createElement('span');words.textContent=call.label;b.append(label,words);b.onpointerdown=async()=>{await audio();await command(24,i);};b.onkeydown=e=>{if(e.repeat)return;if(e.code==='Enter'||e.code==='Space'){e.preventDefault();e.stopPropagation();b.onpointerdown();}};$('#bob-pads').append(b);}}
$('#calls-prev').onclick=()=>{callPage--;callPads();command(28,callPage);};$('#calls-next').onclick=()=>{callPage++;callPads();command(28,callPage);};
$('#yeah').onclick=async()=>{await audio();await command(27);};
fetch('./call-index.json').then(r=>{if(!r.ok)throw Error('Call bank unavailable');return r.json();}).then(bank=>{callBank=bank;recordedBand(bank);for(const [i,call] of bank.calls.entries())if(call.pool_flags&1)$('#holler-choice').add(new Option(call.label+' · '+call.title,i));callPads();
 if(bank.pool_counts){const row=document.createElement('div');row.className='gestures';row.id='reviewed-call-families';
 for(const [pool,label] of [[3,'AH!'],[4,'ALL RIGHT!'],[5,'BAND / SOLO CALL'],[6,'UH-HUH'],[7,'SWING OUT']]){
  const b=document.createElement('button');b.textContent=label;b.disabled=!Number(bank.pool_counts[String(pool)]);if(b.disabled)b.style.opacity='.45';b.title=b.disabled?'No reviewed cuts assigned to this family yet':bank.pool_counts[String(pool)]+' reviewed calls';b.onclick=async()=>{await audio();await command(38,pool);};row.append(b);
 }$('#bob-pads').before(row);}
 $('#call-status').textContent=bank.historic?`${bank.calls.length} recorded calls ready`:'Synthetic call stand-ins';$('#call-bank-info').textContent=bank.review_status+'. Call-family buttons shuffle their own reviewed pools; Random Bob shuffles all kept calls.';}).catch(()=>{$('#call-status').textContent='Call details unavailable';});
function scoreInfo(){const p=scoreBank.parts[+$('#score-choice').value];if(p)$('#score-source').textContent=`${p.checked?'PRINT CHECKED':'PROVISIONAL · '+p.quality_status} · printed p.${p.printed_page} / PDF p.${p.pdf_page}. ${(p.issues?.length??0)?'Timing/part flags: '+(p.issues?.length??0)+'. ':''}${p.scope}${p.body_quarters?' ·32quarters, with a pickup; complete-source features need room before the ending. Exact take and human audio review remain separate.':''}`;}
fetch('./score-index.json').then(r=>r.json()).then(bank=>{scoreBank=bank;bank.parts.forEach((p,i)=>$('#score-choice').add(new Option(`${p.checked?'✓ ':''}${p.player} · ${p.title}`,i)));scoreInfo();});
$('#score-feature').onclick=async()=>{await audio();await command(33);};
$('#score-choice').onchange=()=>{scoreInfo();command(29,+$('#score-choice').value);};$('#score-preview').onclick=async()=>{await audio();await command(30);};$('#fiddle-colour').oninput=e=>command(37,+e.target.value);$('#source-solos').onchange=e=>command(31,+e.target.checked);$('#texture').oninput=e=>command(32,+e.target.value);
$('#fiddle-preview').onclick=async()=>{await audio();await command(36,1);};
document.querySelectorAll('[data-fiddle]').forEach(b=>b.onclick=async()=>{
 await command(37,+b.dataset.fiddle);
 if(!state.playing){await audio();await command(36,1);}
});
$('#holler').onclick=async()=>{await audio();if(customCall){const voice=context.createBufferSource(),gain=context.createGain();voice.buffer=customCall;gain.gain.value=.35;voice.connect(gain).connect(analyser);voice.start();}else if(hollerIndex<0)await command(20);else await command(24,hollerIndex);};
$('#holler-choice').onchange=()=>{hollerIndex=+$('#holler-choice').value;customCall=null;$('#clip-status').textContent='Using the record bank';};
$('#call-clip').onchange=async()=>{const file=$('#call-clip').files[0];if(!file)return;if(file.size>20*1024*1024){$('#clip-status').textContent='Choose a clip under 20 MB';return;}try{await audio();customCall=await context.decodeAudioData(await file.arrayBuffer());$('#clip-status').textContent=file.name+' · this browser only';}catch{$('#clip-status').textContent='Could not decode that audio clip';}};
$('#mode-band').onclick=async()=>{await command(25,0);await command(35,0);};
$('#mode-classics').onclick=()=>command(35,1);
$('#mode-bob').onclick=()=>command(25,1);
$('#brand-title').onclick=()=>command(22);
$('#auto-bob').onchange=()=>command(23,$('#auto-bob').checked?1:0);
$('#speech').onchange=()=>command(19,$('#speech').checked?1:0);
fetch('./classics-index.json').then(r=>r.json()).then(bank=>{classicBank=bank;$('#classic-song').replaceChildren();$('#classic-coverage').replaceChildren();for(const [i,song] of bank.songs.entries()){if(!song.playable)continue;const option=new Option(song.title,i);option.disabled=!song.playable;$('#classic-song').add(option);const row=document.createElement('li');row.className=song.playable?'ready':'';row.textContent=song.title+' · '+(song.playable?'Playable full head':'Complete head in preparation');$('#classic-coverage').append(row);}$('#classic-count').textContent=`${bank.playable} tunes`;});
$('#classic-song').onchange=()=>command(34,+$('#classic-song').value);
function update(){
 updatePanel(state);
 $('#classics-library').hidden=!state.classicMode||!!state.padMode;$('#form').closest('label').hidden=!!state.classicMode;$('#mode-classics').setAttribute('aria-pressed',String(!!state.classicMode&&!state.padMode));
 $('#now-playing').textContent=state.classicId?state.classicTitle:'A new Western swing tune.';
 if(state.classicMode&&document.activeElement!==$('#classic-song'))$('#classic-song').value=state.classicChoice;
 const classic=classicBank.songs[state.classicId?state.classicId-1:state.classicChoice];
 const evidence=classic?.source?.evidence_bars;const coverage=classic?.source?.coverage_label??(classic?.source_body_bar_quarters?`${classic.written_bars} written bars · shortened bar retained`:evidence?`${classic.expanded_bars} bars (${evidence.direct_public_notation} written, ${evidence.recording_transcription} from recording)`:`${classic?.expanded_bars} bars (${classic?.written_bars} written, repeats expanded)`);
 $('#classic-source').textContent=classic?.playable?`${coverage} · ${classic.source?.creator??"Western swing"}. Melody on ${classic.lead_instrument??'fiddle'}; original BB-BOB band arrangement. ${classic.adlib_bars?`${classic.adlib_bars} printed slash bars get an original ad-lib lead; those notes are not a transcription. `:''}${classic.source?.recording_relation??'Exact Wills take remains unverified.'}`:'Complete melody and arrangement in preparation.';

 $('#classic-downloads').hidden=!classic?.exports_available;
 if(classic?.exports_available){$('#classic-midi').href=classic.exports.midi;$('#classic-xml').href=classic.exports.musicxml;}

 $('#leon-record').disabled=!state.leonRecordAvailable;$('#leon-record').textContent=state.leonRecordPending?'LEON’S RECORD · NEXT BAR':state.leonRecord?'LEON’S RECORD · PLAYING':'LEON’S RECORD · 2-BAR SPOTLIGHT';
 $('#players').hidden=!!state.padMode;$('#bob-face').hidden=!state.padMode;for(const id of ['knobs','now-zone','style-controls','band-gestures','keys','sound-check','fiddle-controls'])$('#'+id).hidden=!!state.padMode;$('#bob-pads').hidden=!state.padMode;$('#mode-band').setAttribute('aria-pressed',String(!state.padMode&&!state.classicMode));$('#mode-bob').setAttribute('aria-pressed',String(!!state.padMode));
 $('#bob-pages').hidden=!state.padMode;$('#score-feature').disabled=!state.playing||!scoreBank.parts[+$('#score-choice').value]?.automatic;
 $('#score-preview').textContent=state.scorePlaying?'STOP WRITTEN SOLO':'PLAY WRITTEN SOLO';
 document.querySelectorAll('.bob-pad').forEach(b=>b.setAttribute('aria-pressed',String(!!state.callActive&&+b.dataset.call===state.call)));
 $('#brand-title').setAttribute('aria-label',state.title?'Return to band display':'Show Bob title screen');
 const sampleStatus=$('#band-sample-status');if(sampleStatus){const s=callBank.samples?.[state.samplePlaying];sampleStatus.textContent=state.sampleActive&&s?`Playing ${s.source_title} · ${(state.samplePosition/s.rate).toFixed(1)}s / ${s.duration_seconds.toFixed(1)}s`:'Original speed · ready';}
 const callToken=state.call+':'+state.callActive;if(callToken!==lastCall){lastCall=callToken;if(state.callActive)$('#call-status').textContent=(state.historicCalls?'On the record: ':'Stand-in: ')+(callBank.calls[state.call]?.label??'Bandleader call');}
 if(document.activeElement!==$('#auto-bob'))$('#auto-bob').checked=state.autoBob!==0;
 if(document.activeElement!==$('#key'))$('#key').value=state.nextKey;
 if(document.activeElement!==$('#speech'))$('#speech').checked=state.spoken!==0;
 $('#play').textContent=state.concert?.armed?'■ REST THE SHOW':state.playing?'■ REST THE BAND':'▶ PLAY THE BAND';
 $('#status').textContent=(state.leonRecordPending?'Leon’s record at the next bar · ':state.leonRecord?'Leon’s recorded spotlight · ':state.pending?'New tune at the next bar · ':state.leonPending?'Leon at the next bar · ':state.solo?'Leon takes it · ':'')+`${state.section} · bar ${Math.min(state.bar+1,state.bars)} of ${state.bars}`;
 const concert=state.concert,view=concertView(),between=concert?.armed&&['ringout','call','breath'].includes(view?.phase);
 $('#concert-panel').hidden=!portable||!state.classicMode||!!state.padMode;
 $('#concert-enabled').disabled=!concert?.available;$('#concert-enabled').checked=!!concert?.enabled;
 $('#concert-next').textContent=(between?view?.nextTitle:concert?.nextTitle)||'Set list unavailable';
 $('#concert-panel').classList.toggle('between',!!between);$('#concert-bob').hidden=!between;
 $('#concert-phase').textContent=!concert?.available?'SET LIST UNAVAILABLE':!concert.enabled?'ONE SONG':between?(view.phase==='call'?'BOB HAS THE FLOOR':'BETWEEN TUNES'):concert.armed?'ON STAGE':'SET LIST READY';
 $('#concert-caption').textContent=!concert?.enabled?'This tune will finish and rest.':between?(view.phase==='call'?'A word from Bob…':'The band is getting ready…'):state.autoBob&&state.spoken?'The full ending, a word from Bob, then the next tune.':'The full ending, then the next tune. Bob’s automatic calls are off.';
 document.querySelector('[data-command="6"]').textContent=concert?.armed?'FINISH THIS TUNE':'ONE LAST ROUND';
 if(between)$('#status').textContent=view.phase==='call'?'Bob has the floor · '+view.nextTitle+' is next':'Next up · '+view.nextTitle;
 $('#seed').textContent=state.classicId?`Named source arrangement · ${classic?.expanded_bars??''} bars · original BB-BOB band`:`Seed ${state.seed} · ${['AABA','Swing blues','Refrain and handoffs'][state.form]} · source cell ${(state.phraseCell??0)+1}`;
 const wired=['CHROME STEEL','SYNTH STRINGS','ELECTRIC GUITAR','KEYS','ACID BASS','DANCE DRUMS','BRASS','SYNTHS'];
 players.forEach((p,i)=>{const b=$('#player-'+i);b.setAttribute('aria-pressed',String(!!(state.players&(1<<i))));b.querySelector('.player-name').textContent=state.modern>=35?wired[i]:(i===1&&state.fiddleColour>=50?'ANALOGUE FIDDLE':p);});
 document.querySelectorAll('#sound-buttons button').forEach((b,i)=>{if(i===4){b.textContent=state.modern>=50?'Acid bass':'Upright bass';b.setAttribute('aria-label','Hear '+b.textContent);}b.disabled=!(state.players&(1<<i));b.title=b.disabled?'Switch this player on to hear it':'Hear this player';});
 const colour=state.fiddleColour??0;if(document.activeElement!==$('#fiddle-colour'))$('#fiddle-colour').value=colour;$('#fiddle-colour-value').textContent=colour===0?'Acoustic bow':colour===100?'Synth bow':colour+'% synth';
 const nowFiddle=Math.round(.85*(state.nowApplied??state.modern??0));
 $('#fiddle-now-note').textContent=nowFiddle>colour?`Now is adding synth colour: the fiddle is currently ${nowFiddle}% synth. Move Then → Now back to hear your chosen tone.`:'Then → Now can add more synth colour across the band.';
 $('#fiddle-preview').disabled=!(state.players&2);$('#fiddle-preview').title=state.players&2?'Hear a held fiddle note with this tone':'Switch FIDDLE on to hear it';
 document.querySelectorAll('[data-fiddle]').forEach(b=>b.setAttribute('aria-pressed',String(+b.dataset.fiddle===colour)));
 const now=state.modern??0;
 const hiphop=state.hiphop??0;if(document.activeElement!==$('#hiphop-control'))$('#hiphop-control').value=hiphop;$('#hiphop-value').textContent=hiphop+'%';
 $('#now-zone').style.setProperty('--now',now+'%');
 $('#now-stage').textContent=now===0?'DANCE HALL':now<35?'WIRED':now<75?'ACID SWING':'DANCEFLOOR';
 $('#now-story').textContent=now===0?'Upright bass · acoustic rhythm · fiddle and steel':now<35?'Electric guitar comes forward · synth colour enters':now<75?'303-style acid bass · synth chords · offbeat guitar':'Sliding acid bass · synth arps and strings · driving dance drums';
 for(const b of document.querySelectorAll('[data-modern]'))b.setAttribute('aria-pressed',String(+b.dataset.modern===now));
 const values=[state.bpm,state.swing,state.modern,state.air,state.glow,state.swell];controls.forEach((c,i)=>{const el=$('#control-'+c[1]);if(document.activeElement!==el)el.value=values[i];$('#out-'+c[1]).textContent=values[i]+(c[1]===9?'%':'');});
 if(state.revision!==lastRevision){lastRevision=state.revision;$('#seed').dataset.revision=lastRevision;}
}
async function screen(){if(screenBusy)return;screenBusy=true;try{let rgba;if(portable){rgba=new Uint8ClampedArray(await portable.screen());}else{const r=await fetch('/api/screen');rgba=new Uint8ClampedArray(await r.arrayBuffer());}$('#screen').getContext('2d').putImageData(new ImageData(rgba,240,240),0,0);const portrait=$('#bob-face').getContext('2d');portrait.clearRect(0,0,76,81);if(!state.title)portrait.drawImage($('#screen'),164,29,76,81,0,0,76,81);const concertPortrait=$('#concert-bob').getContext('2d');concertPortrait.clearRect(0,0,76,81);if(!$('#concert-bob').hidden)concertPortrait.drawImage($('#screen'),164,29,76,81,0,0,76,81);}finally{screenBusy=false;}}
setInterval(async()=>{try{await portableReady;if(portable){state=await portable.state();}else{const r=await fetch('/api/state');state=await r.json();}update();}catch{ $('#audio').textContent=portable?'Instrument loading failed':'Native host disconnected';}},120);
setInterval(()=>screen().catch(()=>{}),70);
const meter=new Float32Array(256);function animate(){if(analyser){analyser.getFloatTimeDomainData(meter);const rms=Math.sqrt(meter.reduce((s,x)=>s+x*x,0)/meter.length);$('#meter').style.width=Math.min(100,rms*450)+'%';$('#audio').textContent=`Native C · 44.1 kHz · ${underruns?'stream gaps '+underruns:'streaming'}`;}requestAnimationFrame(animate);}animate();
document.addEventListener('keydown',e=>{if(e.repeat||['INPUT','SELECT','TEXTAREA'].includes(e.target.tagName))return;if(state.padMode&&e.key.length===1&&'12345678qwertyui'.includes(e.key.toLowerCase())){const i='12345678qwertyui'.indexOf(e.key.toLowerCase());if(i+callPage*16<callBank.calls.length)audio().then(()=>command(24,i+callPage*16));return;}if(e.code==='Space'){e.preventDefault();$('#play').click();}else if(e.key.toLowerCase()==='l')$('#leon').click();else if(e.key.toLowerCase()==='n')$('#new').click();else if(/^[1-8]$/.test(e.key))command(7,+e.key-1);else{const k='asdfghjkwert'.indexOf(e.key.toLowerCase());if(k>=0)audio().then(()=>command(17,60+k));}});
window.addEventListener('pagehide',()=>{worker?.terminate();portable?.close();context?.close();});
window.BBSWING={get state(){return state;},get audioContext(){return context;},get analyser(){return analyser;},get visual(){return portable?.visual;},command,panel:panelControl,concert:setConcert};

window.BBBOB=window.BBSWING;

['Picked steel','Fiddle','Rhythm guitar','Piano','Upright bass','Snare','Clarinet'].forEach((name,i)=>{const b=document.createElement('button');b.textContent=name;b.setAttribute('aria-label','Hear '+name);b.onclick=async()=>{await audio();await command(36,i);};$('#sound-buttons').append(b);});
