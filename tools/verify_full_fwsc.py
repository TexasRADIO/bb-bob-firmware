#!/usr/bin/env python3
"""Independently decode a full-install FWSC against its compiled app/loader."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'vendor/fm1/tools'))
import lz4blk
from identity import check as check_identity

def crc(data):
 value=0
 for byte in data:
  value^=byte<<8
  for _ in range(8):value=((value<<1)^(0x1021 if value&0x8000 else 0))&65535
 return value

def decode(data,key=65535):
 out=bytearray()
 for byte in data:
  out.append(byte^(key&255));key=((key<<1)^(0x1021 if key&0x8000 else 0))&65535
 return bytes(out)

def verify(package,target):
 raw=package.read_bytes();meta=json.loads(Path(str(package)+'.json').read_text())
 sha=lambda x:hashlib.sha256(x).hexdigest()
 assert sha(raw)==meta['sha256']
 logical=bytearray();identity=''
 for i in range(20):
  logical+=raw[i*48:i*48+47];marker=raw[i*48+47]
  if marker!=125:identity+=chr((marker-i-1)&255)
 logical+=raw[960:]
 assert identity==meta['product']==check_identity()['product_id']
 header=decode(logical[:64]);assert crc(header[2:])==struct.unpack_from('<H',header)[0]
 total,count=struct.unpack_from('<IH',header,4);assert total==len(logical) and count==2 and header[16:22]==b'AC791N'
 entries=logical[64:64+count*80];assert crc(entries)==struct.unpack_from('<H',header,2)[0]
 payload={}
 for i in range(count):
  entry=decode(entries[i*80:(i+1)*80]);typ,index,checksum,_,offset,size,padded=struct.unpack_from('<HHHHIII',entry)
  name=entry[64:].split(b'\0')[0].decode();data=bytes(logical[offset:offset+size])
  assert index==i and padded>=size and len(data)==size and crc(data)==checksum
  payload[name]=data
 def ota_header(blob):
  hc,dc,a,b,attr,_,_,name=struct.unpack('<HHIIBBH16s',blob[:32])
  assert crc(blob[2:32])==hc and name==b'usb_hid_ota.bin\0'
  return dc,a,b,attr
 ota=payload['ota.bin'];checksum,offset,length,attr=ota_header(ota)
 assert offset==32 and length==len(ota)-32 and attr==0x41 and crc(ota[32:])==checksum
 checksum,length,address,attr=ota_header(ota[32:]);assert address==0x01c0a800 and attr==0 and length<=0x14000
 at=64;loader=bytearray()
 while at<len(ota):
  compressed,plain=struct.unpack_from('<II',ota,at);at+=8
  assert 0<compressed<=4096 and 0<plain<=4096 and at+compressed<=len(ota)
  block=lz4blk.decompress(ota[at:at+compressed]);assert len(block)==plain
  loader+=block;at+=compressed
 assert len(loader)==length and crc(loader)==checksum
 assert loader==(target/'loader.bin').read_bytes() and sha(loader)==meta['loader_sha256']
 encrypted=payload['flash.bin'][0x4000:];area=bytearray()
 for off in range(0,len(encrypted),32):area+=decode(encrypted[off:off+32],0x980f^(off>>2))
 entry=area[32:64];assert crc(entry[2:])==struct.unpack_from('<H',entry)[0]
 checksum,offset,size=struct.unpack_from('<HII',entry,2);assert offset==0x120 and size==0x8dfbc
 app=area[offset:offset+size];assert crc(app)==checksum
 expected=(target/'bbswing.bin').read_bytes()
 assert app[:len(expected)]==expected and all(byte==255 for byte in app[len(expected):])
 assert sha(expected)==meta['app_sha256']==json.loads((target/'REPORT.json').read_text())['app_sha256']
 return {'passed':True,'product':identity,'app_sha256':sha(expected),'package_sha256':sha(raw),'embedded_app_exact':True,'loader_exact':True,'all_crcs':True,'padding_verified':True,'device_contacted':False}

if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--package',type=Path,required=True);p.add_argument('--target',type=Path,required=True)
 a=p.parse_args();print(json.dumps(verify(a.package,a.target),indent=2))
