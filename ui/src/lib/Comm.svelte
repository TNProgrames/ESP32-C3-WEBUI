<script>
  import { api } from '../api.js';

  let c = $state({ localhost: '', host: '' });
  let pin = $state('');
  let msg = $state(''); let ok = $state(true);

  $effect(() => {
    api('/settings/get_comm').then(r => (c = r)).catch(() => {});
    api('/settings/get_pin').then(r => (pin = r.pin)).catch(() => {});
  });

  async function save(e) {
    e.preventDefault();
    try {
      await api('/settings/save_comm', c);
      await api('/settings/save_pin', { pin });
      ok = true; msg = 'تم الحفظ';
    } catch { ok = false; msg = 'فشل الحفظ'; }
  }
</script>

<form class="glass" onsubmit={save}>
  <h2>الاتصال</h2>
  <label for="l">المضيف المحلي</label>
  <input id="l" bind:value={c.localhost} maxlength="32" />
  <label for="h">مضيف الإنترنت</label>
  <input id="h" bind:value={c.host} maxlength="32" />
  <label for="pn">رقم PIN لإعادة الضبط</label>
  <input id="pn" bind:value={pin} maxlength="32" />
  <div style="margin-top:14px"><button class="btn green">حفظ</button></div>
  <div class="msg" class:ok class:err={!ok}>{msg}</div>
</form>
