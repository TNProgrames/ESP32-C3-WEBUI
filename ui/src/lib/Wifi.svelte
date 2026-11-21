<script>
  import { api } from '../api.js';
  import { t } from '../store.svelte.js';

  let f = $state({ mode: 'AP', ssid: '', pass: '', ap_ssid: '', ap_pass: '' });
  let nets = $state([]);
  let scanning = $state(false);
  let msg = $state(''); let ok = $state(true);

  $effect(() => { api('/settings/get_wifi').then(r => (f = r)).catch(() => {}); });

  async function scan() {
    scanning = true; nets = [];
    for (let i = 0; i < 10; i++) {
      try {
        const r = await api('/settings/scan_wifi');
        if (Array.isArray(r)) { nets = r; break; }
      } catch {}
      await new Promise(r => setTimeout(r, 1500));
    }
    scanning = false;
  }

  async function save(e) {
    e.preventDefault();
    try {
      await api('/settings/save_wifi', f);
      ok = true; msg = t('تم الحفظ. سيُعاد ضبط الواي فاي وقد ينقطع الاتصال لحظات.');
    } catch { ok = false; msg = t('فشل الحفظ'); }
  }
</script>

<form class="glass" onsubmit={save}>
  <h2>📶 {t('إعدادات الواي فاي')}</h2>
  <label for="m">{t('الوضع')}</label>
  <select id="m" bind:value={f.mode}>
    <option value="AP">{t('AP (نقطة وصول)')}</option>
    <option value="STA">{t('STA (اتصال بشبكة)')}</option>
    <option value="AP_STA">AP + STA</option>
  </select>

  {#if f.mode !== 'AP'}
    <label for="s">{t('اسم الشبكة (STA)')}</label>
    <div class="row">
      <input id="s" style="flex:1" bind:value={f.ssid} />
      <button type="button" class="btn ghost" onclick={scan} disabled={scanning}>{scanning ? '...' : t('مسح')}</button>
    </div>
    {#each nets as n}
      <div class="net" role="button" tabindex="0" onclick={() => (f.ssid = n.ssid)} onkeydown={(e) => e.key === 'Enter' && (f.ssid = n.ssid)}>
        <span>{n.ssid} {n.secure ? '🔒' : ''}</span><span style="color:var(--muted)">{n.rssi} dBm</span>
      </div>
    {/each}
    <label for="p">{t('كلمة مرور الشبكة')}</label>
    <input id="p" type="password" bind:value={f.pass} />
  {/if}

  {#if f.mode !== 'STA'}
    <label for="as">{t('اسم نقطة الوصول (AP)')}</label>
    <input id="as" bind:value={f.ap_ssid} />
    <label for="ap">{t('كلمة مرور نقطة الوصول (8 أحرف على الأقل)')}</label>
    <input id="ap" type="password" bind:value={f.ap_pass} />
  {/if}

  <div style="margin-top:14px"><button class="btn green">{t('حفظ وتطبيق')}</button></div>
  <div class="msg" class:ok class:err={!ok}>{msg}</div>
</form>
