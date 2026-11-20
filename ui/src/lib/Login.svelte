<script>
  import { api, session } from '../api.js';
  let { onLogin } = $props();

  let username = $state('admin');
  let password = $state('');
  let msg = $state('');
  let busy = $state(false);

  async function submit(e) {
    e.preventDefault();
    busy = true; msg = '';
    try {
      const r = await api('/login', { username, password });
      session.set(r.token, r.role);
      onLogin(r.role);
    } catch (err) {
      msg = err.status === 401 ? 'اسم المستخدم أو كلمة المرور غير صحيحة' : 'تعذر الاتصال بالجهاز';
    }
    busy = false;
  }
</script>

<form class="glass" style="max-width:340px;margin:12vh auto 0" onsubmit={submit}>
  <h2>تسجيل الدخول</h2>
  <label for="u">المستخدم</label>
  <select id="u" bind:value={username}>
    <option value="admin">admin</option>
    <option value="user">user</option>
  </select>
  <label for="p">كلمة المرور</label>
  <input id="p" type="password" bind:value={password} autocomplete="current-password" />
  <div style="margin-top:14px"><button class="btn" style="width:100%" disabled={busy || !password}>دخول</button></div>
  <div class="msg err">{msg}</div>
</form>
