// استقبال الدرايفرات من GitHub (يجلبها المتصفح لا الجهاز) وتثبيتها على الجهاز.
// الفهرس drivers/build/index.json يُنتجه tools/drv/build_index.py ويحوي لكل درايفر:
//   name, version, api, size, mem, sha256, file (نسبي لمكان الفهرس), info, methods[{name,sig}]
// التحقق الحاسم من التوقيع يتم على الجهاز (/drv/install)؛ فحص sha256 هنا اختياري لكشف التنزيل التالف مبكرًا.
import { authHeaders } from './api.js';

export const REPO = 'TNProgrames/ESP32-C3-WEBUI';
export const DEFAULT_INDEX = `https://raw.githubusercontent.com/${REPO}/main/drivers/build/index.json`;

export async function listInstalled() {
  const r = await fetch('/drv/list', { headers: authHeaders() });
  if (!r.ok) throw new Error('HTTP ' + r.status);
  return r.json();
}

// يجلب الفهرس ويتحقق من شكله
export async function fetchIndex(url) {
  const r = await fetch(url, { cache: 'no-store' });
  if (!r.ok) throw new Error('HTTP ' + r.status);
  const j = await r.json();
  if (j?.version !== 1 || !Array.isArray(j.drivers)) throw new Error('bad index format');
  return j.drivers.filter((d) => d && typeof d.name === 'string' && typeof d.file === 'string');
}

// حالة الدرايفر مقابل المثبّت: new | same | upd
export function compare(entry, installed) {
  const i = installed.find((x) => x.name === entry.name);
  return !i ? 'new' : (i.version === entry.version ? 'same' : 'upd');
}

async function sha256Hex(buf) {
  if (!globalThis.crypto?.subtle) return null;       // الصفحات على http غير آمنة ولا توفّر crypto.subtle
  const h = await crypto.subtle.digest('SHA-256', buf);
  return [...new Uint8Array(h)].map((b) => b.toString(16).padStart(2, '0')).join('');
}

// ينزّل ملف .drv ويفحص الحجم وsha256 (إن أمكن)
export async function download(entry, indexUrl) {
  const r = await fetch(new URL(entry.file, indexUrl).href, { cache: 'no-store' });
  if (!r.ok) throw new Error('HTTP ' + r.status);
  const buf = await r.arrayBuffer();
  if (entry.size && buf.byteLength !== entry.size) throw new Error('size mismatch');
  const h = await sha256Hex(buf);
  if (h && entry.sha256 && h !== entry.sha256) throw new Error('sha256 mismatch');
  return buf;
}

async function post(path, body) {
  const r = await fetch(path, { method: 'POST', headers: { ...authHeaders(), 'Content-Type': 'application/octet-stream' }, body });
  let j = null; try { j = await r.json(); } catch {}
  if (r.status === 401) { location.reload(); return { ok: false, error: 'unauthorized' }; }
  return { ok: r.ok && !!j?.ok, error: j?.error || (r.ok ? '' : 'HTTP ' + r.status) };
}

// يرسل ملف .drv (ArrayBuffer) للجهاز؛ الجهاز يتحقق من التوقيع قبل الحفظ
export const installToDevice = (buf) => post('/drv/install', buf);
export const removeFromDevice = (name) => post('/drv/remove?name=' + encodeURIComponent(name), '');

// تنزيل من المتجر ثم تثبيت
export async function installFromStore(entry, indexUrl) {
  return installToDevice(await download(entry, indexUrl));
}
