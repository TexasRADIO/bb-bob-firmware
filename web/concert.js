// Emulator set-list transport. All waits consume rendered audio frames;
// this has no page timer, new speech, native DSP state or firmware writes.
export const concertCallIds = [
 'call-ae415653562565c4', 'call-b26292a41c47d587',
 'call-5f018764138b7fe9', 'call-60105bad90d9079d',
 'call-8726afefe323112c', 'call-62614eabb802cc23',
 'call-5da0e2568300b332', 'call-a57f1e90e8e247e5',
];
const interlude = phase => ['ringout','call','breath'].includes(phase);
export class ConcertQueue {
 constructor({read,send,songs=[],calls=[]}) {
  this.read=read;this.send=send;
  this.songs=songs.map((song,index)=>({...song,index})).filter(song=>song.playable);
  this.calls=calls.map((call,index)=>({...call,index})).filter(call=>
   concertCallIds.includes(call.id)&&call.family==='encouragement'&&
   !call.cue_target_characters?.length&&call.samples/call.rate>=3&&call.samples/call.rate<=6);
  this.enabled=true;this.armed=false;this.phase='idle';this.remaining=0;
  this.next=null;this.call=null;this.completed=0;this.lastCall=-1;
  this.bag=[];this.random=0x63b0b;this.wasPlaying=false;this.sourceId=0;
 }
 get available(){return this.songs.length>1;}
 nextSong(state){
  const at=this.songs.findIndex(song=>song.index+1===state.classicId);
  return at<0?null:this.songs[(at+1)%this.songs.length];
 }
 stopCall(){
  const s=this.read();
  if(this.phase==='call'&&this.call?.index===s.call&&s.callActive){
   // Existing cue-off cancels a call. Restore the user's Leon-call switch.
   this.send(19,0);this.send(19,s.spoken);
  }
 }
 cancel(){this.stopCall();this.armed=false;this.phase='idle';this.remaining=0;this.call=null;this.wasPlaying=false;}
 arm(s){
  if(!this.enabled||!this.available||!s.classicMode||s.padMode||!s.classicId||!s.playing)return;
  this.armed=true;this.phase='song';this.sourceId=s.classicId;this.wasPlaying=true;this.next=this.nextSong(s);
 }
 setEnabled(enabled){
  this.enabled=!!enabled;
  if(!this.enabled)this.cancel();else if(!this.armed)this.arm(this.read());
 }
 command(command,value=0){
  const s=this.read();
  // During the interval, PLAY means rest the show, not replay the old song.
  if(command===0&&this.armed&&interlude(this.phase)){this.cancel();return;}
  if(interlude(this.phase)&&[19,23].includes(command)){
   if(!value&&this.phase==='call'){this.stopCall();this.call=null;this.phase='breath';this.remaining=15435;}
   this.send(command,value);return;
  }
  const changesMusic=[1,25,30,34,35].includes(command);
  const manualCall=[2,20,21,24,26,27,38,40].includes(command);
  if(command===0||changesMusic||(command===16&&value===0)||
     (interlude(this.phase)&&manualCall))this.cancel();
  this.send(command,value);
  const after=this.read();
  if((command===0&&!s.playing)||(changesMusic&&s.playing&&after.playing))this.arm(after);
 }
 panel(control,kind,index,value){
  if(kind===0&&index===12&&value&&this.armed&&interlude(this.phase)){this.cancel();return;}
  const before=this.read();
  if(kind===4&&value===0)this.cancel();
  if(kind===0&&value&&[7,13].includes(index))this.cancel();
  control(kind,index,value);
  const after=this.read();
  if(kind===0&&index===12&&value){
   if(!after.playing)this.cancel();else if(!before.playing||!this.armed)this.arm(after);
  }
 }
 chooseCall(){
  if(!this.calls.length)return null;
  if(!this.bag.length){
   this.bag=this.calls.slice();
   for(let i=this.bag.length-1;i>0;i--){
    this.random=(Math.imul(this.random,1664525)+1013904223)>>>0;
    const j=this.random%(i+1);[this.bag[i],this.bag[j]]=[this.bag[j],this.bag[i]];
   }
   if(this.bag.length>1&&this.bag.at(-1).index===this.lastCall)
    [this.bag[0],this.bag[this.bag.length-1]]=[this.bag.at(-1),this.bag[0]];
  }
  const call=this.bag.pop();this.lastCall=call.index;return call;
 }
 beforeRender(){
  if(!this.armed)return;
  const s=this.read();
  if(!this.enabled||!s.classicMode||s.padMode||!s.classicId){this.cancel();return;}
  if(this.phase==='song'){
   if(s.playing){this.next=this.nextSong(s);this.sourceId=s.classicId;}
   this.wasPlaying=!!s.playing;return;
  }
  // A physical song confirmation or other external transport wins.
  if(s.playing||s.classicId!==this.sourceId){this.cancel();return;}
  if(this.remaining>0)return;
  if(this.phase==='ringout'){
   if(s.callActive)return; // Never cut an existing user/ending call.
   this.call=s.autoBob&&s.spoken?this.chooseCall():null;
   if(this.call){this.send(24,this.call.index);this.phase='call';return;}
   this.phase='breath';this.remaining=15435;return;
  }
  if(this.phase==='call'){
   if(s.callActive)return;
   this.phase='breath';this.remaining=15435;return;
  }
  if(this.phase==='breath'&&this.next){
   const index=this.next.index;
   this.send(34,index);
   const ready=this.read();
   if(ready.classicId!==index+1||ready.playing){this.cancel();return;}
   this.send(0,0);this.completed++;this.call=null;this.arm(this.read());
  }
 }
 afterRender(frames){
  if(!this.armed)return;
  const s=this.read();
  if(this.phase==='song'&&this.wasPlaying&&!s.playing){
   if(s.bar>=s.bars&&s.classicMode&&!s.padMode&&this.next){
    this.phase='ringout';this.remaining=39690;this.sourceId=s.classicId;
   }else this.cancel();
  }else if(interlude(this.phase))this.remaining=Math.max(0,this.remaining-frames);
  this.wasPlaying=!!s.playing;
 }
 status(){
  const s=this.read(),next=this.armed?this.next:this.nextSong(s);
  return {enabled:this.enabled,available:this.available,armed:this.armed,phase:this.phase,
   nextIndex:next?.index??null,nextTitle:next?.title??'',completed:this.completed,
   callId:this.call?.id??'',callSeconds:this.call?this.call.samples/this.call.rate:0};
 }
}
