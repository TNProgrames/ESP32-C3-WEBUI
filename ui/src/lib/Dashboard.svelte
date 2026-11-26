<script>
  import { api, session } from '../api.js';
  import { t } from '../store.svelte.js';
  let { role } = $props();

  let ram = $state(0);
  let storage = $state(0);
  let version = $state('');
  let uptime = $state(0);
  let temp = $state(null);
  let wake = $state('');
  let slSecs = $state(60);
  let slPins = $state([]);
  let slLevel = $state(1);
  let net = $state(null);
  let msg = $state('');

  function fmt(s) {
    const d = Math.floor(s / 86400), h = Math.floor((s % 86400) / 3600), m = Math.floor((s % 3600) / 60);
    return `${d}d ${String(h).padStart(2, '0')}h ${String(m).padStart(2, '0')}m ${String(s % 60).padStart(2, '0')}s`;
  }

  async function refresh() {
    try {
      const u = await api('/hdusage');
      ram = parseFloat(u.RamUsage) || 0;
      storage = parseFloat(u.StorageUsage) || 0;
      version = u.Version || '';
      uptime = u.Uptime || 0;
      temp = typeof u.Temp === 'number' ? u.Temp : null;
      wake = u.Wake === 'timer' ? t('مؤقت') : u.Wake === 'gpio' ? t('رجل') + ' GPIO' + u.WakePin : t('إقلاع عادي');
    } catch {}
    try { net = await api('/settings/get_stat'); } catch { net = null; }
  }

  async function sleepNow() {
    if (!slPins.length && !(slSecs > 0)) { msg = t('حدّد مؤقتًا أو رجل استيقاظ'); return; }
    if (!confirm(t('سيدخل الجهاز النوم العميق وينقطع الاتصال. متابعة؟'))) return;
    try {
      await api(`/settings/sleep?secs=${slSecs > 0 ? Math.floor(slSecs) : 0}&pins=${slPins.join(',')}&level=${slLevel}`, {});
      msg = t('الجهاز في النوم العميق. يستيقظ بالمؤقت أو رجل الاستيقاظ أو زر RESET.');
    } catch { msg = t('تعذّر'); }
  }

  async function restart() {
    if (!confirm(t('هل تريد إعادة تشغيل الجهاز؟'))) return;
    try {
      await api('/settings/restart', {});
      msg = t('يُعاد تشغيل الجهاز، سجّل الدخول من جديد...');
      setTimeout(() => { session.clear(); location.reload(); }, 10000);
    } catch {}
  }

  $effect(() => {
    refresh();
    const timer = setInterval(refresh, 4000);
    return () => clearInterval(timer);
  });
</script>

<div class="glass">
  <h2>📊 {t('استخدام الموارد')}</h2>
  <label>{t('الذاكرة (RAM)')}: {ram}%</label>
  <div class="bar"><div style="width:{ram}%"></div></div>
  <label>{t('التخزين')}: {storage}%</label>
  <div class="bar"><div style="width:{storage}%;background:var(--success)"></div></div>
</div>

<div class="glass">
  <h2>ℹ️ {t('الحالة')}</h2>
  <div class="row" style="gap:18px;font-size:13px">
    <span>{t('الإصدار')}: <b>{version || '-'}</b></span>
    <span>{t('مدة التشغيل')}: <b>{fmt(uptime)}</b></span>
    {#if temp !== null}<span>🌡️ {t('حرارة المعالج')}: <b style={temp > 70 ? 'color:#e55' : ''}>{temp.toFixed(1)}°C</b></span>{/if}
  </div>
  <div style="font-size:13px;margin-top:6px">⏰ {t('سبب آخر إقلاع')}: <b>{wake || '-'}</b></div>
  {#if role === 'admin'}
    <div style="margin-top:14px;padding-top:12px;border-top:1px solid var(--border)">
      <b style="font-size:13px">😴 {t('النوم العميق')}</b>
      <div class="row" style="gap:8px;margin-top:8px;flex-wrap:wrap;font-size:13px">
        <label>{t('مؤقت (ثانية، 0 = بلا)')} <input type="number" min="0" max="2592000" style="width:90px" bind:value={slSecs} /></label>
        <label>{t('مستوى الاستيقاظ')}
          <select bind:value={slLevel}><option value={1}>{t('عالٍ')} (1)</option><option value={0}>{t('منخفض')} (0)</option></select>
        </label>
      </div>
      <div class="row" style="gap:10px;margin-top:8px;flex-wrap:wrap;font-size:13px">
        <span style="color:var(--muted)">{t('أرجل الاستيقاظ')}:</span>
        {#each [0, 1, 2, 3, 4, 5] as n}
          <label><input type="checkbox" value={n} bind:group={slPins} /> {n}</label>
        {/each}
      </div>
      <button class="btn ghost" style="margin-top:8px" onclick={sleepNow}>😴 {t('نوم عميق الآن')}</button>
    </div>
    <div style="margin-top:14px"><button class="btn ghost" onclick={restart}>🔁 {t('إعادة تشغيل الجهاز')}</button></div>
  {/if}
  <div class="msg ok">{msg}</div>
</div>

<div class="glass">
  <h2>🌐 {t('الشبكة')}</h2>
  {#if net}
    <div class="row" style="gap:18px;font-size:13px">
      <span>IP: <b>{net.ip}</b></span>
      {#if net.ssid}<span>{t('الشبكة')}: <b>{net.ssid}</b></span>{/if}
      {#if net.status}<span>{t('الحالة')}: <b>{net.status}</b></span>{/if}
      {#if net.rssi}<span>{t('الإشارة')}: <b>{net.rssi} dBm</b></span>{/if}
    </div>
  {:else}
    <span style="color:var(--muted);font-size:13px">{t('لا توجد بيانات (قد يكون الواي فاي قيد الضبط)')}</span>
  {/if}
</div>
