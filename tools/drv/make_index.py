#!/usr/bin/env python3
"""يولّد drivers/index.json من الملفات المبنية drivers/build/*.drv (الاسم والإصدار والحجم وsha256).
تقرؤه واجهة «درايفرات» من المتصفح؛ مسار file نسبي لمكان index.json."""
import glob, hashlib, json, os, struct
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
out = []
for f in sorted(glob.glob(os.path.join(ROOT, 'drivers', 'build', '*.drv'))):
    b = open(f, 'rb').read()
    if b[:4] != b'ESPD': continue
    out.append({'name': b[24:40].rstrip(b'\0').decode(), 'version': b[40:48].rstrip(b'\0').decode(),
                'api': struct.unpack_from('<H', b, 6)[0], 'size': len(b), 'sha256': hashlib.sha256(b).hexdigest(),
                'file': 'build/' + os.path.basename(f)})
json.dump(out, open(os.path.join(ROOT, 'drivers', 'index.json'), 'w'), indent=2, ensure_ascii=False)
print(len(out), 'driver(s) ->', 'drivers/index.json')
