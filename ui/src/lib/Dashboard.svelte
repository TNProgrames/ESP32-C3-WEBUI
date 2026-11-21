<script>
  import { api, session } from '../api.js';
  import { t } from '../store.svelte.js';
  let { role } = $props();

  let ram = $state(0);
  let storage = $state(0);
  let version = $state('');
  let uptime = $state(0);
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
    } catch {}
    try { net = await api('/settings/get_stat'); } catch { net = null; }
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
  </div>
  {#if role === 'admin'}
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
