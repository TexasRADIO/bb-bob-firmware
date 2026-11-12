#!/usr/bin/env python3
"""Build the isolated FM-1 application; never contacts USB or installs firmware."""
import argparse,hashlib,json,os,pathlib,re,subprocess,sys
ROOT=pathlib.Path(__file__).resolve().parents[1]
import identity
identity.check()
p=argparse.ArgumentParser();p.add_argument('--inside',action='store_true');p.add_argument('--diagnostics',action='store_true',help='Separate offline diagnostic image with resettable probes; base-machine retains bank/Showbill, legacy excludes art; not an installer');p.add_argument('--half-frames',type=int,choices=[128,256],default=128);p.add_argument('--pack-leon',action='store_true',help='Offline opt-in lossless Leon storage candidate; target CPU cost is not qualified');p.add_argument('--timbre-bank',action='store_true',help='Offline opt-in acoustic PCM bank/cache candidate; no installer');p.add_argument('--frozen-cue-sha256',help='Explicitly compile the selected output folder leon_pcm.h after matching this SHA; never regenerate/reuse it implicitly.')
p.add_argument('--base-machine',action='store_true',help='Offline P0r5 USB/SAFE/TRS candidate with the acoustic bank; separate output, no installer')
p.add_argument('--peak-context',action='store_true',help='Whole-half context/freeze/remote-start diagnostic, no detailed render probes')
p.add_argument('--ram-audio',action='store_true',help='Explicit comparative RAM render candidate; default keeps render code in XIP')
p.add_argument('--output-dir',type=pathlib.Path,help='Relative build output; preserves earlier qualification artifacts')
args=p.parse_args()
if args.peak_context and (not args.base_machine or args.diagnostics):p.error('--peak-context requires base-machine without detailed diagnostics')
if args.base_machine:
 if args.pack_leon:p.error('Base-machine candidate retains raw-Leon configuration')
 args.timbre_bank=True
if args.half_frames!=128 and not (args.diagnostics or args.base_machine and args.peak_context):p.error('Alternate half size requires an explicit diagnostic or peak-context base-machine build')
manifest=json.loads((ROOT/'vendor/fm1/IMPORT_MANIFEST.json').read_text())
for row in manifest['files']:
 assert hashlib.sha256((ROOT/'vendor/fm1'/row['destination']).read_bytes()).hexdigest()==row['sha256'],row['destination']
if not args.inside:
 toolchain=os.environ.get('JIELI_TOOLCHAIN')
 if not toolchain: raise SystemExit('Set JIELI_TOOLCHAIN to the unpacked PI32 toolchain.')
 mounts=['-v',f'{ROOT}:/work']
 # Retain the checkout's absolute build symlink inside the container too.
 # Mount only its verified destination, rather than the surrounding drive.
 if (ROOT/'build').is_symlink():
  retained=(ROOT/'build').resolve(strict=True);mounts+=['-v',f'{retained}:{retained}']
 subprocess.run(['docker','run','--rm','--platform','linux/amd64',*mounts,'-v',f'{toolchain}:/opt/jieli:ro','-w','/work','fm1-timing-runtime:bookworm-python','python3','tools/build_target.py','--inside','--half-frames',str(args.half_frames),*(['--diagnostics'] if args.diagnostics else []),*(['--base-machine'] if args.base_machine else []),*(['--ram-audio'] if args.ram_audio else []),*(['--peak-context'] if args.peak_context else []),*(['--pack-leon'] if args.pack_leon else []),*(['--timbre-bank'] if args.timbre_bank else []),*(['--frozen-cue-sha256',args.frozen_cue_sha256] if args.frozen_cue_sha256 else []),*(['--output-dir',str(args.output_dir)] if args.output_dir else [])],check=True)
 sys.exit(0)
if args.output_dir:
 if args.output_dir.is_absolute() or '..' in args.output_dir.parts:p.error('--output-dir must stay inside the project')
 out=ROOT/args.output_dir
else:out=ROOT/('build/target-base-diagnostics-'+str(args.half_frames) if args.diagnostics else 'build/target-base-machine') if args.base_machine else ROOT/(('build/target-diagnostics-'+str(args.half_frames) if args.diagnostics else 'build/target')+('-timbre-bank' if args.timbre_bank else ''))
out.mkdir(parents=True,exist_ok=True)
if args.timbre_bank:
 subprocess.run([sys.executable,'tools/timbre_bank.py','--output',str(out)],cwd=ROOT,check=True)
from pack_leon import pack_header
leon_packing=pack_header(ROOT/'build/leon_clip.h',out/'leon_clip.h',out/'leon-clip-packing.json')
if not args.pack_leon:
 (out/'leon_clip.h').write_bytes((ROOT/'build/leon_clip.h').read_bytes())
 leon_packing={'schema':'bb-bob-leon-lossless-packing/v1','format':'raw/default; packed candidate requires --pack-leon','frames':leon_packing['frames'],'raw_pcm_bytes':leon_packing['frames'],'stored_pcm_bytes':leon_packing['frames'],'data_bytes_saved':0,'source_header_sha256':leon_packing['source_header_sha256'],'packed_header_sha256':hashlib.sha256((out/'leon_clip.h').read_bytes()).hexdigest(),'source_unchanged':True,'quantization_changed':False,'installed':False,'install_permitted':False,'candidate_forecast':leon_packing}
 (out/'leon-clip-packing.json').write_text(json.dumps(leon_packing,indent=2)+'\n')
# Hardware flash budget: all 103 calls use a separate low-bandwidth codec build.
bank=ROOT/'build/bob-calls-reviewed/manifest.json'
if args.frozen_cue_sha256:
 assert re.fullmatch('[0-9a-f]{64}',args.frozen_cue_sha256),'Expected SHA256 in lowercase hex'
 if not (out/'leon_pcm.h').exists():
  (out/'leon_pcm.h').write_bytes((ROOT/'build/leon_pcm.h').read_bytes())
 assert hashlib.sha256((out/'leon_pcm.h').read_bytes()).hexdigest()==args.frozen_cue_sha256,'Frozen target cue input mismatch'
 print('Using explicitly hash-verified frozen target cue input',flush=True)
elif bank.exists():subprocess.run([sys.executable,'tools/cue.py','--bank',str(bank),'--rate','3600','--compact','--output',str(out)],cwd=ROOT,check=True)
else:(out/'leon_pcm.h').unlink(missing_ok=True)  # Never compile a stale generated bank on a source-only build.
def run(tool,*items):
 result=subprocess.run(['/opt/jieli/'+tool,*map(str,items)],cwd=ROOT,capture_output=True,text=True)
 if result.returncode:sys.stderr.write(result.stdout+result.stderr);raise SystemExit(result.returncode)
 return result.stdout
flags=['-target','pi32v2','-mcpu=r3','-mfprev1','-Os','-ffp-contract=off','-ffunction-sections','-fno-builtin','-Wall','-Werror=int-conversion','-Werror=incompatible-pointer-types','-Wno-unused-function','-I/opt/jieli/pi32v2/include','-Isrc','-Ivendor/fm1/hal','-I'+str(out.relative_to(ROOT)),'-Ibuild','-DSW_TARGET_SCORE_SMALL=1','-DSW_WITH_CUE=1']
flags+=['-DSW_HALF_FRAMES='+str(args.half_frames)]
flags+=['-DSW_DIAGNOSTICS=1','-DSW_RENDER_PROFILE=1'] if args.diagnostics else ['-DSW_WITH_ART=1']
if args.base_machine and args.diagnostics:flags+=['-DSW_WITH_ART=1']
if args.timbre_bank:flags+=['-DSW_WITH_TIMBRE_BANK=1']
if args.base_machine:flags+=['-DSW_WITH_BASE_MACHINE=1','-DSW_BOB_FULL_SHA=1','-DSW_TARGET_PERIOD_CACHE=1','-DSW_TARGET_CALL_CACHE=1','-DSW_ACTIVE_VOICES=20']
if args.peak_context:flags+=['-DSW_PEAK_CONTEXT=1']
if args.ram_audio:flags+=['-DSW_TARGET_RAM_AUDIO=1']
call_index=out/'call-index.json' if (out/'call-index.json').exists() else ROOT/'build/call-index.json'
subprocess.run([sys.executable,'tools/showbill_calls.py',str(call_index),'tools/call-figure-assignments.json',str(out/'showbill_calls.h')],cwd=ROOT,check=True)
objects=[]
for name,source in [('crt0','vendor/fm1/crt0.S'),('fm1_vec','vendor/fm1/hal/fm1_vec.S'),('fm1_isr','vendor/fm1/hal/fm1_isr.S'),('swing','src/swing.c'),('display','src/display.c'),('rodeo','src/rodeo.c'),('panel','src/panel.c'),('showbill','src/showbill.c'),('bank','src/bank.c'),('art_bank','src/art_bank.c'),('bob_identity','src/bob_identity.c'),*([('bb_base','src/bb_base.c'),('bb_bank_writer','src/bb_bank_writer.c')] if args.base_machine else []),*([('timbre_bank','src/timbre_bank.c')] if args.timbre_bank else []),*([('diagnostics','src/diagnostics.c')] if args.diagnostics else []),('main','src/fm1_main.c')]:
 compile_flags=['-O2' if flag=='-Os' else flag for flag in flags] if name=='swing' else flags
 obj=out/(name+'.o');run('pi32v2/bin/clang',*compile_flags,'-c',source,'-o',obj);objects.append(obj)
 if source.endswith('.c'):run('pi32v2/bin/clang',*compile_flags,'-S',source,'-o',out/(name+'.s'))
 print('Compiled',name,flush=True)
elf=out/'bbswing.elf'
run('pi32v2/bin/ld','--gc-sections','-e','_start','-T','src/app.ld',*objects,'-o',elf)
symbols=run('common/bin/objdump','-t',elf);(out/'symbols.txt').write_text(symbols)
(out/'sections.txt').write_text(run('common/bin/objdump','-h',elf))
(out/'disassembly.txt').write_text(run('common/bin/objdump','-d',elf))
def sym(name):return int(re.search(r'^([0-9a-f]+).*\s'+re.escape(name)+r'$',symbols,re.M)[1],16)
assert sym('_rt_end')<=0x01C06000,'RAM audio path exceeds RAMTEXT'
if args.ram_audio:assert 0x01C00000<=sym('sw_render')<sym('_rt_end'),'sw_render was not linked into RAMTEXT'
else:assert 0x02000120<=sym('sw_render')<sym('_etext'),'sw_render was not linked into XIP'
assert 0x02000120<=sym('fm1_alnk0_irq')<0x02006000,'Audio ISR bridge is outside RAM-audio call range'
image=bytearray()
for section,load in [('.text',0x02000120),('.ram_text',sym('_rt_load')),('.data',sym('_data_load'))]:
 path=out/(section[1:]+'.bin');run('common/bin/objcopy','-O','binary','-j',section,elf,path)
 blob=path.read_bytes() if path.exists() else b''
 if blob:
  offset=load-0x02000120;assert offset>=len(image);image+=b'\xff'*(offset-len(image));image+=blob
image+=b'\xff'*(-len(image)%4)
assert sym('_start')==0x02000120 and image[:4]==bytes.fromhex('04818000')
assert len(image)<=0x8dfbc
assert len(image)==sym('_data_load')+sym('_data_end')-sym('_data_start')-0x02000120,'Runtime identity hash extent differs from binary'
ram=sym('_bss_end')-0x1c08000;pool=sym('_pool_end')-sym('_pool_start')
assert ram<=73728-4096 and pool<=368640-8192
path=out/'bbswing.bin';path.write_bytes(image)
report={'target':'FM-1 PI32r3/FPU','compiled':True,'linked':True,'app_bytes':len(image),'app_sha256':hashlib.sha256(image).hexdigest(),'ram_bytes':ram,'ram_headroom':73728-ram,'pool_bytes':pool,'pool_headroom':368640-pool,'entry_verified':True,'hardware_tested':False,'packaged':False,'installed':False,'install_permitted':False,'diagnostics_only':args.diagnostics,'base_machine':args.base_machine,'half_frames':args.half_frames,'art_enabled':not args.diagnostics or args.base_machine,'external_timbre_bank':args.timbre_bank,'timbre_bank':json.loads((out/'timbres.json').read_text()) if args.timbre_bank else None,'render_probes':args.diagnostics,'leon_clip_packing':leon_packing,'cue_input_mode':'explicit frozen SHA256' if args.frozen_cue_sha256 else 'regenerated bank' if bank.exists() else 'source fallback','sources':{str(f.relative_to(ROOT)):hashlib.sha256(f.read_bytes()).hexdigest() for f in [*sorted((ROOT/'src').glob('*.[ch]')),*sorted((ROOT/'src').glob('*.inc')),ROOT/'src/app.ld',(out/'leon_pcm.h' if bank.exists() or args.frozen_cue_sha256 else ROOT/'build/leon_pcm.h'),ROOT/'build/bob_art.h',ROOT/'build/leon_clip.h',out/'leon_clip.h',ROOT/'build/music_bank.h',ROOT/'build/classic_bank.h',ROOT/'vendor/fm1/IMPORT_MANIFEST.json',ROOT/'identity.json',out/'showbill_calls.h',call_index,ROOT/'tools/call-figure-assignments.json']}}
report['embedded_call_table_cache']=bool(args.base_machine)
report['peak_context']=args.peak_context
report['render_placement']='RAMTEXT' if args.ram_audio else 'XIP'
report['ramtext_bytes']=sym('_rt_end')-sym('_rt_start')
report['ramtext_headroom']=0x6000-report['ramtext_bytes']
report['analysis_artifacts']={f.name:hashlib.sha256(f.read_bytes()).hexdigest() for f in [elf,out/'symbols.txt',out/'sections.txt',out/'disassembly.txt',*sorted(out.glob('*.s'))]}
(out/'REPORT.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
loader=ROOT/'vendor/fm1/loader'
for name,source in [('loader_start',loader/'crt0_ldr.S'),('loader',loader/'loader.c')]:
 run('pi32v2/bin/clang',*flags,'-c',source,'-o',out/(name+'.o'))
run('pi32v2/bin/ld','--gc-sections','-e','_start','-T',loader/'loader.ld',out/'loader_start.o',out/'loader.o','-o',out/'loader.elf')
run('common/bin/objcopy','-O','binary','-j','.text',out/'loader.elf',out/'loader.bin')
loader_sections=run('common/bin/objdump','-h',out/'loader.elf')
for line in loader_sections.splitlines():
 row=line.split()
 if len(row)>4 and row[1].startswith('.') and int(row[2],16):assert int(row[3],16)<0x02000000,'Loader outside RAM'
print('Compiled fresh RAM-only update loader',flush=True)
