<script>
  import { api } from '../api.js';

  let ram = $state(0);
  let storage = $state(0);
  let net = $state(null);

  async function refresh() {
    try {
      const u = await api('/hdusage');
      ram = parseFloat(u.RamUsage) || 0;
      storage = parseFloat(u.StorageUsage) || 0;
    } catch {}
    try { net = await api('/settings/get_stat'); } catch { net = null; }
  }

  $effect(() => {
    refresh();
    const t = setInterval(refresh, 4000);
    return () => clearInterval(t);
  });
</script>

<div class="glass">
  <h2>استخدام الموارد</h2>
  <label>الذاكرة (RAM): {ram}%</label>
  <div class="bar"><div style="width:{ram}%"></div></div>
  <label>التخزين: {storage}%</label>
  <div class="bar"><div style="width:{storage}%;background:var(--success)"></div></div>
</div>

<div class="glass">
  <h2>الشبكة</h2>
  {#if net}
    <div class="row" style="gap:18px;font-size:13px">
      <span>IP: <b>{net.ip}</b></span>
      {#if net.ssid}<span>الشبكة: <b>{net.ssid}</b></span>{/if}
      {#if net.status}<span>الحالة: <b>{net.status}</b></span>{/if}
      {#if net.rssi}<span>الإشارة: <b>{net.rssi} dBm</b></span>{/if}
    </div>
  {:else}
    <span style="color:var(--muted);font-size:13px">لا توجد بيانات (قد يكون الواي فاي قيد الضبط)</span>
  {/if}
</div>
