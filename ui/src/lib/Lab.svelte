<script>
  import { api, apiRaw, authHeaders } from '../api.js';
  import { t } from '../store.svelte.js';
  import { examples } from '../examples.js';

  let { go = () => {} } = $props();

  let name = $state('lab');
  let code = $state(examples['Blink (GPIO8)'].code);
  let pagePath = $state('/ui/page.html');
  let pageHtml = $state('');
  let showPage = $state(false);
  let list = $state([]);
  let out = $state('');
  let seq = 0;
  let box = $state();
  let busy = $state(false);

  async function refresh() {
    try {
      list = await api('/script/list');
      const r = await api('/terminal/log?since=' + seq);
      seq = r.seq;
      if (r.text) {
        out = (out + r.text).slice(-6000);
        queueMicrotask(() => { if (box) box.scrollTop = box.scrollHeight; });
      }
    } catch {}
  }

  $effect(() => {
    refresh();
    const timer = setInterval(refresh, 1200);
    return () => clearInterval(timer);
  });

  async function savePage() {
    if (!/^\/ui\/[\w.-]+\.html$/.test(pagePath)) throw new Error(t('مسار الصفحة يجب أن يكون /ui/<اسم>.html'));
    await fetch('/api/mkdir?path=/ui', { method: 'POST', headers: authHeaders() });
    const r = await fetch('/api/save?path=' + encodeURIComponent(pagePath), {
      method: 'POST',
      headers: { ...authHeaders(), 'Content-Type': 'application/x-www-form-urlencoded' },
      body: 'content=' + encodeURIComponent(pageHtml)
    });
    if (!r.ok) throw new Error(t('تعذر حفظ الصفحة'));
  }

  async function run() {
    busy = true;
    try {
      if (showPage && pageHtml.trim()) await savePage();
      await apiRaw('/script/run?name=' + encodeURIComponent(name.trim()), code);
    } catch (e) {
      out += (e?.message && !/^HTTP/.test(e.message) ? e.message : t('تعذر التشغيل (تحقق من الاسم أو الحجم)')) + '\n';
    }
    busy = false;
    setTimeout(refresh, 300);
  }
  async function stop(n) {
    try { await apiRaw('/script/stop?name=' + encodeURIComponent(n)); } catch {}
    setTimeout(refresh, 300);
  }
  function load(e) {
    const k = e.target.value;
    if (!k) return;
    const ex = examples[k];
    code = ex.code;
    name = k.replace(/[^A-Za-z0-9]+/g, '_').replace(/^_+|_+$/g, '').toLowerCase().slice(0, 20) || 'lab';
    if (ex.page) { pagePath = ex.page.path; pageHtml = ex.page.html; showPage = true; }
    else { pageHtml = ''; showPage = false; }
    e.target.value = '';
  }
</script>

<div class="glass">
  <h2 style="margin-top:0">🧪 {t('مختبر Lua')}</h2>
  <p style="color:var(--muted);font-size:13px;margin:0 0 10px">
    {t('اكتب سكريبت Lua وشغّله على الجهاز مباشرة. المتاح: gpio, adc, pwm, timer, sys, ui, print.')}
  </p>

  <div class="row" style="margin-bottom:8px">
    <input style="flex:1;min-width:90px" bind:value={name} placeholder={t('اسم السكريبت')} autocapitalize="off" spellcheck="false" />
    <select onchange={load}>
      <option value="">{t('أمثلة...')}</option>
      {#each Object.keys(examples) as k}<option value={k}>{k}</option>{/each}
    </select>
  </div>

  <textarea class="term-input" style="width:100%;min-height:200px;direction:ltr;text-align:left;font-family:monospace"
    bind:value={code} spellcheck="false" autocapitalize="off"></textarea>

  <label class="row" style="margin:10px 0 6px;font-size:13px;gap:8px">
    <input type="checkbox" style="width:auto" bind:checked={showPage} /> {t('صفحة HTML مخصصة (تُحفظ على الجهاز قبل التشغيل)')}
  </label>
  {#if showPage}
    <input style="direction:ltr;text-align:left;margin-bottom:6px" bind:value={pagePath} spellcheck="false" autocapitalize="off" />
    <textarea class="term-input" style="width:100%;min-height:180px;direction:ltr;text-align:left;font-family:monospace"
      bind:value={pageHtml} spellcheck="false" autocapitalize="off"></textarea>
  {/if}

  <div class="row" style="margin:10px 0">
    <button class="btn" onclick={run} disabled={busy}>▶ {t('تشغيل')}</button>
    <button class="btn ghost" onclick={() => stop('all')}>⏹ {t('إيقاف الكل')}</button>
    <button class="btn ghost" onclick={() => go('panels')}>🪟 {t('اللوحات')}</button>
  </div>

  {#if list.length}
    <div style="margin-bottom:10px">
      {#each list as s}
        <div class="row" style="justify-content:space-between;font-size:13px;padding:4px 0">
          <span>{s.state === 'running' ? '🟢' : '🔴'} <b>{s.name}</b> · {(s.mem / 1024).toFixed(1)}KB · {s.up}s</span>
          <button class="btn ghost" onclick={() => stop(s.name)}>⏹</button>
        </div>
        {#if s.err}<div style="color:#e55;font-size:12px;direction:ltr;text-align:left">{s.err}</div>{/if}
      {/each}
    </div>
  {/if}

  <div class="term" bind:this={box} style="min-height:120px;max-height:220px">{out}</div>
</div>
