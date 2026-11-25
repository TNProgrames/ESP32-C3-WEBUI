<script>
  import { session } from './api.js';
  import { t } from './store.svelte.js';
  import Login from './lib/Login.svelte';
  import Dashboard from './lib/Dashboard.svelte';
  import Wifi from './lib/Wifi.svelte';
  import Comm from './lib/Comm.svelte';
  import Passwords from './lib/Passwords.svelte';
  import Backup from './lib/Backup.svelte';
  import Terminal from './lib/Terminal.svelte';
  import Ota from './lib/Ota.svelte';
  import Lab from './lib/Lab.svelte';
  import Drivers from './lib/Drivers.svelte';
  import Panels from './lib/Panels.svelte';
  import Toggles from './lib/Toggles.svelte';

  let role = $state(session.role);
  let tab = $state('dash');

  const tabs = [
    { id: 'dash', icon: '📊', label: 'الحالة', admin: false },
    { id: 'wifi', icon: '📶', label: 'الواي فاي', admin: true },
    { id: 'comm', icon: '🔗', label: 'الاتصال', admin: true },
    { id: 'pass', icon: '🔑', label: 'كلمات المرور', admin: true },
    { id: 'backup', icon: '💾', label: 'نسخ احتياطي', admin: true },
    { id: 'term', icon: '💻', label: 'الطرفية', admin: true },
    { id: 'lab', icon: '🧪', label: 'مختبر Lua', admin: true },
    { id: 'drivers', icon: '🛒', label: 'متجر التطبيقات', admin: true },
    { id: 'panels', icon: '🪟', label: 'اللوحات', admin: true },
    { id: 'ota', icon: '⬆️', label: 'تحديث', admin: true }
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
      <span style="color:var(--muted);font-size:12px">{role === 'admin' ? t('مدير') : t('مستخدم')}</span>
      <button class="btn ghost" onclick={logout}>{t('خروج')}</button>
    </div>

    <div class="tabs">
      {#each tabs.filter(x => !x.admin || role === 'admin') as x}
        <button class="btn" class:ghost={tab !== x.id} onclick={() => (tab = x.id)}>{x.icon} {t(x.label)}</button>
      {/each}
    </div>

    {#if tab === 'dash'}<Dashboard {role} />
    {:else if tab === 'wifi'}<Wifi />
    {:else if tab === 'comm'}<Comm />
    {:else if tab === 'pass'}<Passwords />
    {:else if tab === 'backup'}<Backup />
    {:else if tab === 'term'}<Terminal />
    {:else if tab === 'lab'}<Lab go={(id) => (tab = id)} />
    {:else if tab === 'drivers'}<Drivers go={(id) => (tab = id)} />
    {:else if tab === 'panels'}<Panels />
    {:else if tab === 'ota'}<Ota />
    {/if}
  {/if}
</div>

<Toggles />
