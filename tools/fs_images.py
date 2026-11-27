#!/usr/bin/env python3
"""يبني صور مساحات LittleFS الثلاث (fs_c/fs_d/fs_e) بأحجام partitions.csv ويكتب flash.txt بعناوين التفليش.
الاستعمال: python3 tools/fs_images.py <out_dir>   (يحتاج mklittlefs من حزمة PlatformIO: tool-mklittlefs)"""
import csv, glob, os, shutil, subprocess, sys, tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, 'fs_out')
os.makedirs(out, exist_ok=True)

cands = glob.glob(os.path.expanduser('~/.platformio/packages/tool-mklittlefs*/mklittlefs*')) + [shutil.which('mklittlefs') or '']
tool = next((c for c in cands if c and os.path.isfile(c) and os.access(c, os.X_OK)), None)
if not tool:
    sys.exit('mklittlefs not found')

parts = {}
for row in csv.reader(l for l in open(os.path.join(ROOT, 'partitions.csv')) if l.strip() and not l.lstrip().startswith('#')):
    r = [c.strip() for c in row]
    if len(r) >= 5 and r[0] in ('fs_c', 'fs_d', 'fs_e'):
        parts[r[0]] = (int(r[3], 0), int(r[4], 0))
if len(parts) != 3:
    sys.exit('partitions.csv must define fs_c, fs_d, fs_e')

src = {'fs_c': os.path.join(ROOT, 'data'),   # الواجهة: data/app/index.html.gz
       'fs_d': None, 'fs_e': None}           # الإعدادات والبيانات: تبدأ فارغة (تُنشأ افتراضياتها عند الإقلاع)
lines = ['# عناوين التفليش (esptool.py write_flash <العنوان> <الملف>)', '']
for name in ('fs_c', 'fs_d', 'fs_e'):
    off, size = parts[name]
    d = src[name] or tempfile.mkdtemp()
    img = os.path.join(out, name + '.bin')
    subprocess.check_call([tool, '-c', d, '-p', '256', '-b', '4096', '-s', str(size), img])
    lines.append(f'0x{off:X}  {name}.bin   (size 0x{size:X} = {size // 1024} KB)')
open(os.path.join(out, 'flash.txt'), 'w').write('\n'.join(lines) + '\n')
print('\n'.join(lines))
