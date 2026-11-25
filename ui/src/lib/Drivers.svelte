<script>
  import { untrack } from 'svelte';
  import { api, apiRaw, authHeaders } from '../api.js';
  import { DEFAULT_INDEX, fetchStore, compare, installToDevice, removeFromDevice, installFromStore, installApp, getMode, setMode } from '../drvstore.js';
  import { t } from '../store.svelte.js';
  import { lab } from '../lab.svelte.js';

  let { go = () => {} } = $props();

  const STORE_KEY = 'drv-store-url';
  let list = $state([]);            // المثبّت على الجهاز
  let msg = $state('');
  let busy = $state(false);
  let file = $state(null);
  let storeUrl = $state(localStorage.getItem(STORE_KEY) || DEFAULT_INDEX);
  let store = $state([]);           // درايفرات المتجر
  let apps = $state([]);            // تطبيقات المتجر (سكريبت Lua + درايفره)
  let myScripts = $state([]);       // أسماء السكريبتات على الجهاز
  let mode = $state({ mode: 'auto', exec: false });
  let storeMsg = $state('');

  // نموذج الاختبار
  let sel = $state('');
  let sda = $state(8), scl = $state(9);
  let cfgText = $state('');
  let method = $state('');
  let args = $state([]);
  let box = $state();

  const cur = $derived(list.find((d) => d.name === sel));
  const mth = $derived(cur?.methods.find((m) => m.name === method));
  const sigs = $derived([...(mth?.sig || '')]);

  async function refresh() {
    try {
      list = await api('/drv/list');
      if (!list.some((d) => d.name === sel)) sel = list[0]?.name || '';
      const r = await api('/terminal/log?since=' + lab.seq);
      lab.seq = r.seq;
      if (r.text) { lab.out = (lab.out + r.text).slice(-6000); queueMicrotask(() => { if (box) box.scrollTop = box.scrollHeight; }); }
    } catch {}
  }
  $effect(() => { refresh(); const id = setInterval(refresh, 1500); return () => clearInterval(id); });

  // عند تغيير الدرايفر المختار فقط (لا عند كل تحديث للقائمة): الدالة الأولى وآخر إعداد محفوظ
  $effect(() => {
    const n = sel;
    untrack(() => {
      const d = list.find((x) => x.name === n);
      if (!d) return;
      method = d.methods[0]?.name || ''; args = [];
      cfgText = localStorage.getItem('drv-cfg-' + d.name) || '';
    });
  });
  $effect(() => { method; untrack(() => { args = []; }); });

  // ---- التثبيت والحذف (المنطق في drvstore.js) ----
  async function install(buf) {
    busy = true; msg = '';
    const r = await installToDevice(buf);
    msg = r.ok ? t('تم التثبيت') : t('تعذر التثبيت') + ': ' + r.error;
    busy = false; refresh();
    return r.ok;
  }
  async function upload() { if (file) await install(await file.arrayBuffer()); }
  async function remove(n) {
    if (!confirm(t('حذف الدرايفر') + ' ' + n + '؟')) return;
    const r = await removeFromDevice(n);
    msg = r.ok ? t('تم الحذف') : t('تعذر الحذف') + ': ' + r.error;
    refresh();
  }

  // ---- المتجر: يجلبه المتصفح لا الجهاز ----
  async function loadStore() {
    storeMsg = ''; store = []; apps = [];
    try {
      localStorage.setItem(STORE_KEY, storeUrl);
      const r = await fetchStore(storeUrl);
      store = r.drivers; apps = r.apps;
      if (!store.length && !apps.length) storeMsg = t('الفهرس فارغ');
    } catch (e) { storeMsg = t('تعذر جلب الفهرس (يلزم اتصال بالإنترنت)') + ': ' + (e.message || ''); }
  }
  async function loadScripts() { try { const f = await api('/script/files'); myScripts = (f || []).map((x) => x.name); } catch {} }
  async function loadMode() { try { mode = await getMode(); } catch {} }
  async function changeMode(m) {
    busy = true; msg = '';
    msg = (await setMode(m)) ? t('تم تغيير الوضع وأُعيد تحميل الدرايفرات') : t('تعذر تغيير الوضع');
    busy = false; await loadMode(); refresh();
  }
  async function getApp(a) {
    busy = true; msg = '';
    try {
      const r = await installApp(a, store, list, storeUrl);
      msg = r.ok ? t('تم تثبيت التطبيق') : t('تعذر التثبيت') + ': ' + r.error;
    } catch (e) { msg = t('تعذر التنزيل') + ': ' + (e.message || ''); }
    busy = false; refresh(); loadScripts();
  }
  async function runApp(a) {
    try { await apiRaw('/script/runfile?name=' + encodeURIComponent(a.name)); msg = t('تم التشغيل'); } catch { msg = t('تعذر التشغيل'); }
  }
  async function editApp(a) {
    try {
      const r = await fetch('/api/read?path=' + encodeURIComponent('/scripts/' + a.name + '.lua'), { headers: authHeaders() });
      lab.name = a.name; lab.code = await r.text(); lab.asLib = false; lab.showPage = false; go('lab');
    } catch { msg = t('تعذر فتح الملف'); }
  }
  $effect(() => { untrack(() => { loadStore(); loadScripts(); loadMode(); }); });

  async function fromStore(d) {
    busy = true; msg = '';
    try {
      const r = await installFromStore(d, storeUrl);
      msg = r.ok ? t('تم التثبيت') : t('تعذر التثبيت') + ': ' + r.error;
    } catch (e) { msg = t('تعذر التنزيل') + ': ' + (e.message || ''); }
    busy = false; refresh();
  }

  // ---- توليد Lua للاختبار ----
  function lstr(s) { return '"' + String(s).replace(/\\/g, '\\\\').replace(/"/g, '\\"').replace(/\n/g, '\\n').replace(/\r/g, '').replace(/[\x00-\x1f]/g, (c) => '\\' + c.charCodeAt(0)) + '"'; }
  function lval(v) {
    v = v.trim();
    if (/^-?\d+$/.test(v) || /^0x[0-9a-fA-F]+$/.test(v) || v === 'true' || v === 'false') return v;
    return lstr(v);
  }
  function cfgLua() {
    const parts = [];
    for (const line of cfgText.split('\n')) {
      const m = line.match(/^\s*([A-Za-z_]\w*)\s*=\s*(.+?)\s*$/);
      if (m) parts.push(m[1] + ' = ' + lval(m[2]));
    }
    return '{' + parts.join(', ') + '}';
  }
  function argLua(c, i) {
    const v = args[i];
    switch (c.toLowerCase()) {
      case 'i': return String(parseInt(v === undefined || v === '' ? '0' : v, 10) || 0);
      case 'b': return v ? 'true' : 'false';
      default: return lstr(v ?? '');
    }
  }
  function usedArgs() {
    // الاختيارية الفارغة (من النهاية) تُحذف
    let n = sigs.length;
    while (n > 0 && sigs[n - 1] === sigs[n - 1].toUpperCase() && (args[n - 1] === undefined || args[n - 1] === '' || args[n - 1] === false)) n--;
    return n;
  }
  function genCode() {
    const a = sigs.slice(0, usedArgs()).map((c, i) => argLua(c, i)).join(', ');
    return `-- اختبار ${cur.name}:${method} (مولَّد من تبويب درايفرات)\n` +
      `i2c.begin(${Number(sda) || 8}, ${Number(scl) || 9}, 100000)\n` +
      `local d, err = drv.open(${lstr(cur.name)}, ${cfgLua()})\n` +
      `if not d then print("open:", err) i2c.stop() return end\n` +
      `print("${method}:", d:${method}(${a}))\n` +
      `d:close()\ni2c.stop()`;
  }
  async function runTest() {
    if (!cur || !method) return;
    localStorage.setItem('drv-cfg-' + cur.name, cfgText);
    busy = true;
    try {
      await fetch('/script/run?name=drvtest', { method: 'POST', headers: { ...authHeaders(), 'Content-Type': 'application/octet-stream' }, body: genCode() });
    } catch { msg = t('تعذر التشغيل (تحقق من الاسم أو الحجم)'); }
    busy = false;
  }
  function toLab() { lab.name = 'drvtest'; lab.code = genCode(); lab.asLib = false; lab.showPage = false; go('lab'); }

  const tr = { i: 'int', s: 'str', b: 'bool', d: 'data' };
</script>

<div class="glass">
  <h2 style="margin-top:0">🛒 {t('متجر التطبيقات')}</h2>
  <p style="color:var(--muted);font-size:13px;margin:0 0 10px">
    {t('درايفرات موقّعة تُحمَّل عند الإقلاع. سكريبت Lua يستعملها بـ drv.open(...) ولا يلمس العتاد مباشرة.')}
  </p>
  {#if !list.length}<div style="color:var(--muted);font-size:13px">{t('لا توجد درايفرات مثبّتة')}</div>{/if}
  {#each list as d (d.name)}
    <div style="padding:8px 0;border-bottom:1px solid var(--border)">
      <div class="row" style="justify-content:space-between">
        <span><b>{d.name}</b> <span style="color:var(--muted);font-size:12px">v{d.version} · {d.size}B · RAM {d.mem}B · {d.mode === 'native' ? t('المعالج') : 'VM'}{d.users ? ' · ' + t('مفتوح') : ''}</span></span>
        <button class="btn ghost" title={t('حذف')} disabled={busy || d.users > 0} onclick={() => remove(d.name)}>🗑</button>
      </div>
      <div style="color:var(--muted);font-size:12px">{d.info}</div>
      <div style="font-size:12px;direction:ltr;text-align:left;margin-top:2px">{d.methods.map((m) => m.name + '(' + [...m.sig].map((c) => tr[c.toLowerCase()] + (c === c.toUpperCase() ? '?' : '')).join(', ') + ')').join(' · ')}</div>
    </div>
  {/each}
  {#if msg}<div style="margin-top:8px;font-size:13px">{msg}</div>{/if}
</div>

<div class="glass">
  <h3 style="margin:0 0 8px">📱 {t('التطبيقات')}</h3>
  <p style="color:var(--muted);font-size:12px;margin:0 0 8px">{t('التطبيق سكريبت Lua جاهز مع الدرايفر الذي يحتاجه؛ يُثبَّت الاثنان معًا.')}</p>
  {#if !apps.length}<div style="color:var(--muted);font-size:13px">{storeMsg || t('لا توجد تطبيقات (اضغط جلب)')}</div>{/if}
  {#each apps as a (a.name)}
    {@const has = myScripts.includes(a.name)}
    <div class="net" style="display:block;margin-bottom:8px">
      <div class="row" style="justify-content:space-between">
        <b>{a.title || a.name}</b>
        <span style="font-size:11px;color:var(--muted)">🔌 {a.driver}{list.some((d) => d.name === a.driver) ? ' ✓' : ''}</span>
      </div>
      <div style="color:var(--muted);font-size:12px;margin:4px 0 8px">{a.desc}</div>
      <div class="row" style="gap:6px">
        <button class="btn" disabled={busy} onclick={() => getApp(a)}>{has ? t('تحديث') : t('تثبيت')}</button>
        {#if has}
          <button class="btn green" onclick={() => runApp(a)}>▶ {t('تشغيل')}</button>
          <button class="btn ghost" onclick={() => editApp(a)}>✏ {t('تعديل')}</button>
        {/if}
      </div>
    </div>
  {/each}
</div>

<div class="glass">
  <h3 style="margin:0 0 8px">⚙️ {t('وضع تشغيل الدرايفرات')}</h3>
  <select value={mode.mode} disabled={busy} onchange={(e) => changeMode(e.currentTarget.value)}>
    <option value="auto">{t('تلقائي (المعالج إن أمكن وإلا المفسّر)')}</option>
    <option value="vm">{t('المفسّر VM (معزول، أبطأ)')}</option>
    <option value="native">{t('المعالج (سريع، يتطلب ذاكرة تنفيذ)')}</option>
  </select>
  <div style="color:var(--muted);font-size:12px;margin-top:6px">{mode.exec ? t('ذاكرة التنفيذ متاحة على هذا الجهاز') : t('ذاكرة التنفيذ غير متاحة: سيعمل المفسّر')}</div>
</div>

<div class="glass">
  <h3 style="margin:0 0 8px">⬆️ {t('رفع ملف درايفر')}</h3>
  <input type="file" accept=".drv" onchange={(e) => (file = e.currentTarget.files[0])} disabled={busy} />
  <div style="margin-top:10px"><button class="btn" onclick={upload} disabled={busy || !file}>{t('تثبيت')}</button></div>
</div>

<div class="glass">
  <h3 style="margin:0 0 8px">🛒 {t('المتجر')}</h3>
  <div class="row" style="gap:6px">
    <input style="flex:1;direction:ltr" bind:value={storeUrl} placeholder="https://.../index.json" />
    <button class="btn ghost" onclick={loadStore}>{t('جلب')}</button>
  </div>
  {#if storeMsg}<div style="margin-top:6px;font-size:12px;color:var(--muted)">{storeMsg}</div>{/if}
  {#each store as d (d.name)}
    <div style="padding:6px 0;border-bottom:1px solid var(--border)">
      <div class="row" style="justify-content:space-between">
        <span style="font-size:13px"><b>{d.name}</b> <span style="color:var(--muted)">v{d.version} · {d.size}B · RAM {d.mem}B · API {d.api}</span></span>
        <button class="btn ghost" disabled={busy || compare(d, list) === 'same'} onclick={() => fromStore(d)}>
          {compare(d, list) === 'same' ? t('مثبّت') : compare(d, list) === 'upd' ? t('تحديث') : t('تثبيت')}
        </button>
      </div>
      <div style="color:var(--muted);font-size:12px">{d.info}</div>
    </div>
  {/each}
</div>

{#if cur}
<div class="glass">
  <h3 style="margin:0 0 8px">🧪 {t('اختبار')} — {cur.name}</h3>
  <div class="row" style="gap:8px;margin-bottom:8px">
    <label style="font-size:13px">SDA <input type="number" style="width:60px" bind:value={sda} /></label>
    <label style="font-size:13px">SCL <input type="number" style="width:60px" bind:value={scl} /></label>
    <select bind:value={sel}>{#each list as d}<option value={d.name}>{d.name}</option>{/each}</select>
  </div>
  <textarea rows="2" style="width:100%;direction:ltr;box-sizing:border-box" bind:value={cfgText} placeholder={'addr=0x27\ncols=16'}></textarea>
  <div class="row" style="gap:8px;margin:8px 0">
    <select bind:value={method}>{#each cur.methods as m}<option value={m.name}>{m.name}</option>{/each}</select>
  </div>
  {#each sigs as c, i}
    <label style="display:block;font-size:13px;margin-bottom:6px">
      {t('معامل')} {i + 1} ({tr[c.toLowerCase()]}{c === c.toUpperCase() ? ' ' + t('اختياري') : ''})
      {#if c.toLowerCase() === 'b'}<input type="checkbox" bind:checked={args[i]} />
      {:else if c.toLowerCase() === 'i'}<input type="number" style="width:100%;box-sizing:border-box" bind:value={args[i]} />
      {:else}<input type="text" style="width:100%;box-sizing:border-box" bind:value={args[i]} />{/if}
    </label>
  {/each}
  <div class="row" style="gap:8px">
    <button class="btn green" onclick={runTest} disabled={busy || !method}>▶ {t('تشغيل')}</button>
    <button class="btn ghost" onclick={toLab}>{t('فتح في المختبر')}</button>
  </div>
  <pre style="direction:ltr;text-align:left;font-size:12px;margin:8px 0 0;overflow:auto;white-space:pre-wrap">{genCode()}</pre>
</div>
{/if}

<div class="glass">
  <div class="row" style="justify-content:space-between;margin-bottom:8px">
    <h3 style="margin:0">🖥 {t('الطرفية')}</h3>
    <button class="btn ghost" onclick={() => (lab.out = '')}>🧹 {t('تنظيف')}</button>
  </div>
  <div class="term" bind:this={box} style="min-height:100px;max-height:200px">{lab.out}</div>
</div>
