<script>
  import { session } from './api.js';
  import Login from './lib/Login.svelte';
  import Dashboard from './lib/Dashboard.svelte';
  import Wifi from './lib/Wifi.svelte';
  import Comm from './lib/Comm.svelte';
  import Passwords from './lib/Passwords.svelte';
  import Ota from './lib/Ota.svelte';

  let role = $state(session.role);
  let tab = $state('dash');

  const tabs = [
    { id: 'dash', label: 'الحالة', admin: false },
    { id: 'wifi', label: 'الواي فاي', admin: true },
    { id: 'comm', label: 'الاتصال', admin: true },
    { id: 'pass', label: 'كلمات المرور', admin: true },
    { id: 'ota', label: 'تحديث', admin: true }
  ];

  function onLogin(r) { role = r; tab = 'dash'; }
  function logout() { session.clear(); role = ''; }
</script>

<div class="container">
  {#if !role}
    <Login {onLogin} />
  {:else}
    <div class="glass row" style="justify-content:space-between">
      <strong>ESP32-C3</strong>
      <span style="color:var(--muted);font-size:12px">{role === 'admin' ? 'مدير' : 'مستخدم'}</span>
      <button class="btn ghost" onclick={logout}>خروج</button>
    </div>

    <div class="tabs">
      {#each tabs.filter(t => !t.admin || role === 'admin') as t}
        <button class="btn" class:ghost={tab !== t.id} onclick={() => (tab = t.id)}>{t.label}</button>
      {/each}
    </div>

    {#if tab === 'dash'}<Dashboard />
    {:else if tab === 'wifi'}<Wifi />
    {:else if tab === 'comm'}<Comm />
    {:else if tab === 'pass'}<Passwords />
    {:else if tab === 'ota'}<Ota />
    {/if}
  {/if}
</div>
