export class PortableBand {
 constructor(){this.sequence=0;this.requests=new Map();this.worker=new Worker(new URL('./portable-worker.js',import.meta.url),{type:'module'});this.ready=new Promise((resolve,reject)=>{this.resolve=resolve;this.reject=reject;});this.worker.onmessage=({data})=>{if(data.ready)this.resolve();else if(data.stats)this.onstats?.(data.stats);else if(data.error){this.reject(Error(data.error));const request=this.requests.get(data.id);if(request){request.reject(Error(data.error));this.requests.delete(data.id);}}else if(data.id){if(data.visualFrame!==undefined)this.visual={frame:data.visualFrame,playedFrame:data.playedFrame,bobFrame:data.bobFrame,concert:data.concert};const request=this.requests.get(data.id);if(request){request.resolve(data.result);this.requests.delete(data.id);}}};this.worker.onerror=e=>this.reject(Error(e.message));}
 async request(type,extra={}){await this.ready;const id=++this.sequence;return new Promise((resolve,reject)=>{this.requests.set(id,{resolve,reject});this.worker.postMessage({id,type,...extra});});}
 concert(enabled){return this.request('concert',{enabled});}
 command(command,value){return this.request('command',{command,value});}
 state(){return this.request('state');}
 screen(){return this.request('screen');}
 attach(port,callback){this.onstats=callback;this.worker.postMessage({type:'audio',port},[port]);}
 close(){this.worker.terminate();}
}
