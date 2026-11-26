<script>
  import { t } from '../store.svelte.js';
  import { appDrivers } from '../drvstore.js';

  // بطاقات متجر التطبيقات: بحث + تصنيفات + حالة التثبيت. العمليات تُنفَّذ في المكوّن الأب.
  let { apps = [], installed = [], scripts = [], busyName = '', msg = '', onInstall, onRun, onEdit, onRemove, onRefresh } = $props();

  const CATS = [['all', '✨', 'الكل'], ['sensors', '🌡️', 'حساسات'], ['display', '🖥️', 'شاشات'], ['time', '🕒', 'وقت'], ['tools', '🧰', 'أدوات']];
  let q = $state('');
  let cat = $state('all');

  const shown = $derived(apps.filter((a) => {
    if (cat !== 'all' && (a.category || 'tools') !== cat) return false;
    const s = q.trim().toLowerCase();
    return !s || [a.title, a.name, a.desc, ...appDrivers(a)].some((x) => (x || '').toLowerCase().includes(s));
  }));
  const PER = 5;
  let pg = $state(1);
  const pages = $derived(Math.max(1, Math.ceil(shown.length / PER)));
  const cur = $derived(Math.min(pg, pages));
  const view = $derived(shown.slice((cur - 1) * PER, cur * PER));
  // أرقام الصفحات مع اختصار بنقاط: 1 … 4 5 6 … 12
  const nums = $derived.by(() => {
    const r = [];
    for (let i = 1; i <= pages; i++) if (i === 1 || i === pages || Math.abs(i - cur) <= 1) r.push(i); else if (r[r.length - 1] !== '…') r.push('…');
    return r;
  });
  const have = (d) => installed.some((i) => i.name === d);
</script>

<div class="glass">
  <div class="row" style="gap:6px">
    <input style="flex:1;min-width:0" type="search" placeholder={'🔍 ' + t('بحث عن تطبيق أو درايفر')} bind:value={q} oninput={() => (pg = 1)} />
    <button class="btn ghost" title={t('جلب')} disabled={!!busyName} onclick={onRefresh}>🔄</button>
  </div>
  <div class="row" style="gap:6px;margin-top:8px;flex-wrap:wrap">
    {#each CATS as [id, ic, lb]}
      <button class="btn {cat === id ? '' : 'ghost'}" style="padding:4px 10px;font-size:12px" onclick={() => { cat = id; pg = 1; }}>{ic} {t(lb)}</button>
    {/each}
  </div>
</div>

{#if !shown.length}
  <div class="glass" style="color:var(--muted);font-size:13px">{msg || (apps.length ? t('لا نتائج') : t('لا توجد تطبيقات (اضغط 🔄)'))}</div>
{/if}

{#each view as a (a.name)}
  {@const has = scripts.includes(a.name)}
  {@const missing = appDrivers(a).filter((d) => !have(d))}
  {@const busy = busyName === a.name}
  <div class="glass">
    <div class="row" style="gap:10px;align-items:flex-start">
      <div style="font-size:30px;line-height:1">{a.icon || '📦'}</div>
      <div style="flex:1;min-width:0">
        <div class="row" style="justify-content:space-between;gap:6px">
          <b>{a.title || a.name}</b>
          {#if has}<span style="font-size:11px;color:#3c9">● {t('مثبّت')}</span>{/if}
        </div>
        <div style="color:var(--muted);font-size:12px;margin:3px 0 6px">{a.desc}</div>
        <div class="row" style="gap:5px;flex-wrap:wrap;font-size:11px">
          {#each appDrivers(a) as d}
            <span style="padding:2px 7px;border-radius:99px;background:rgba(127,127,127,.15)">🔌 {d} {have(d) ? '✓' : ''}</span>
          {/each}
          {#if a.page_file}<span style="padding:2px 7px;border-radius:99px;background:rgba(127,127,127,.15)">🖼 {t('صفحة + canvas')}</span>{/if}
          <span style="color:var(--muted)">{((a.size || 0) + (a.page_size || 0)) / 1000 | 0}KB</span>
        </div>
      </div>
    </div>
    <div class="row" style="gap:6px;margin-top:10px;flex-wrap:wrap">
      <button class="btn" disabled={!!busyName} onclick={() => onInstall(a)}>
        {busy ? '…' : has ? '⟳ ' + t('تحديث') : '⬇ ' + t('تثبيت')}{!has && missing.length ? ' (+' + missing.length + ' ' + t('درايفر') + ')' : ''}
      </button>
      {#if has}
        <button class="btn green" onclick={() => onRun(a)}>▶ {t('تشغيل')}</button>
        <button class="btn ghost" onclick={() => onEdit(a)}>✏ {t('تعديل')}</button>
        <button class="btn ghost" title={t('حذف')} disabled={!!busyName} onclick={() => onRemove(a)}>🗑</button>
      {/if}
    </div>
  </div>
{/each}

{#if pages > 1}
  <div class="row" style="gap:5px;justify-content:center;flex-wrap:wrap;margin:6px 0">
    <button class="btn ghost" style="padding:4px 10px" disabled={cur === 1} onclick={() => (pg = cur - 1)}>‹</button>
    {#each nums as n}
      {#if n === '…'}<span style="color:var(--muted)">…</span>
      {:else}<button class="btn {n === cur ? '' : 'ghost'}" style="padding:4px 10px;min-width:32px" onclick={() => (pg = n)}>{n}</button>{/if}
    {/each}
    <button class="btn ghost" style="padding:4px 10px" disabled={cur === pages} onclick={() => (pg = cur + 1)}>›</button>
  </div>
  <div style="text-align:center;color:var(--muted);font-size:11px">{shown.length} {t('تطبيق')} · {t('صفحة')} {cur}/{pages}</div>
{/if}
