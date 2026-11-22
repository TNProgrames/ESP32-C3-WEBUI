<script>
  import { api, apiRaw } from '../api.js';
  import { t } from '../store.svelte.js';

  const examples = {
    'Blink (GPIO8)': `-- يومّض LED على GPIO8 كل 500ms\ngpio.mode(8, "out")\nlocal on = false\ntimer.every(500, function()\n  on = not on\n  gpio.write(8, on)\n  print("led", on)\nend)`,
    'Hello': `print("hello from Lua", 1 + 2, 10 / 4)\nprint("heap", sys.heap())`,
    'Timer': `local n = 0\nlocal id\nid = timer.every(1000, function()\n  n = n + 1\n  print("tick", n)\n  if n == 5 then timer.cancel(id) print("done") end\nend)`,
    'ADC (GPIO0)': `timer.every(1000, function()\n  print("adc", adc.read(0), adc.mv(0) .. " mV")\nend)`,
    'PWM (GPIO8)': `local d = 0\ntimer.every(50, function()\n  d = (d + 5) % 105\n  pwm.set(8, 1000, d)\nend)`,
    'Error test': `-- حلقة لا نهائية: يجب أن يوقفها الحارس\nwhile true do end`
  };

  let name = $state('lab');
  let code = $state(examples['Blink (GPIO8)']);
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

  async function run() {
    busy = true;
    try { await apiRaw('/script/run?name=' + encodeURIComponent(name.trim()), code); }
    catch { out += t('تعذر التشغيل (تحقق من الاسم أو الحجم)') + '\n'; }
    busy = false;
    setTimeout(refresh, 300);
  }
  async function stop(n) {
    try { await apiRaw('/script/stop?name=' + encodeURIComponent(n)); } catch {}
    setTimeout(refresh, 300);
  }
  function load(e) { const k = e.target.value; if (k) code = examples[k]; e.target.value = ''; }
</script>

<div class="glass">
  <h2 style="margin-top:0">🧪 {t('مختبر Lua')}</h2>
  <p style="color:var(--muted);font-size:13px;margin:0 0 10px">
    {t('اكتب سكريبت Lua وشغّله على الجهاز مباشرة. المتاح: gpio, adc, pwm, timer, sys, print.')}
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

  <div class="row" style="margin:10px 0">
    <button class="btn" onclick={run} disabled={busy}>▶ {t('تشغيل')}</button>
    <button class="btn ghost" onclick={() => stop('all')}>⏹ {t('إيقاف الكل')}</button>
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
