<script>
  import { api } from '../api.js';

  let ap = $state(''); let up = $state('');
  let msg = $state(''); let ok = $state(true);

  $effect(() => { api('/settings/get_pass').then(r => { ap = r.ap; up = r.up; }).catch(() => {}); });

  async function save(e) {
    e.preventDefault();
    if (!ap || !up) { ok = false; msg = 'الحقلان مطلوبان'; return; }
    try { await api('/settings/save_pass', { ap, up }); ok = true; msg = 'تم الحفظ'; }
    catch { ok = false; msg = 'فشل الحفظ'; }
  }
</script>

<form class="glass" onsubmit={save}>
  <h2>كلمات المرور</h2>
  <label for="a">كلمة مرور admin</label>
  <input id="a" type="text" bind:value={ap} maxlength="32" />
  <label for="u">كلمة مرور user</label>
  <input id="u" type="text" bind:value={up} maxlength="32" />
  <div style="margin-top:14px"><button class="btn green">حفظ</button></div>
  <div class="msg" class:ok class:err={!ok}>{msg}</div>
</form>
