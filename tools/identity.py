#!/usr/bin/env python3
"""One approved product identity shared by display and release packaging."""
import argparse,json,pathlib,re
ROOT=pathlib.Path(__file__).resolve().parents[1]
def load(root=ROOT):
 data=json.loads((root/'identity.json').read_text());product=data['product_id']
 assert data['schema']=='bb-bob-identity/v1'
 assert data['display_name']=='BB-BOB' and data['family_id']=='92361'
 assert isinstance(data['revision'],int) and data['revision']>0
 assert data['version']==f"R{data['revision']}"
 assert data['display_version']==data['display_name']+' '+data['version']
 if 'package_name' in data:assert re.fullmatch(r'BB-BOB-r'+str(data['revision'])+r'-[a-z0-9-]+\.fwsc',data['package_name']),'Unsafe or mismatched package filename'
 assert product!='FM-1_900','Vendor default product is forbidden'
 assert not re.fullmatch(r'FM-1_92200[0-9]{3}',product),'BB1 product family is forbidden'
 assert re.fullmatch(r'FM-1_92361[0-9]{3}',product),'Only the reserved numeric BB-BOB family is approved'
 assert len(product)<=20 and product.isascii()
 assert all((ord(c)+i+1)&255!=0x7d for i,c in enumerate(product)),'Identity marker collision'
 return data
def header(data):
 return ('/* SPDX-License-Identifier: GPL-3.0-only; generated from identity.json. */\n'
         '#ifndef SW_BOB_IDENTITY_H\n#define SW_BOB_IDENTITY_H\n'+
         ''.join('#define '+key+' '+json.dumps(data[field])+'\n' for key,field in [
          ('SW_BOB_DISPLAY_NAME','display_name'),('SW_BOB_VERSION','version'),
          ('SW_BOB_DISPLAY_VERSION','display_version'),('SW_BOB_PRODUCT_ID','product_id')])+
         '#define SW_BOB_REVISION '+str(data['revision'])+'u\n'+
         'extern char sw_bob_app_sha_prefix[9];\n'
         'void sw_bob_hash_application(const unsigned char *, unsigned int, void (*)(void));\n'
         '#endif\n')
def check(root=ROOT):
 data=load(root);assert (root/'src/bob_identity.h').read_text()==header(data),'Identity header drift: run tools/identity.py --write'
 base=(root/'src/bb_base_hw.inc').read_text()
 assert '#define BB_PACKAGE_ID SW_BOB_PRODUCT_ID' in base,'Firmware handshake must use the generated BB-BOB product identity'
 assert 'FM-1_92200105' not in base,'Legacy BB1 identity remains in the BB-BOB firmware handshake'
 return data
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--write',action='store_true');a=p.parse_args();data=load()
 if a.write:(ROOT/'src/bob_identity.h').write_text(header(data))
 else:check()
 print(data['display_version']+' | '+data['product_id'])
