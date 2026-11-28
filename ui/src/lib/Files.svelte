<script>
  import { authHeaders } from '../api.js';
  import { t } from '../store.svelte.js';
  import Icon from './Icon.svelte';

  // متصفح ملفات متكامل: يستعمل واجهات /api/* نفسها التي تستعملها صفحة الطوارئ (em)
  // المسارات: /c/... و/d/... و/e/... (أقراص C: D: E:)
  const DRV = { c: ['الواجهة', 'fs_c'], d: ['الإعدادات', 'fs_d'], e: ['البيانات', 'fs_e'] };
  let path = $state('/');
  let files = $state([]);
  let parts = $state([]);
  let total = $state(0), used = $state(0);
  let msg = $state(''), busy = $state(false);
  let clip = $state(null);                 // { path, name, isDir, cut }
  let ed = $state(null);                   // { path, text, dirty }
  let up;

  const q = (s) => encodeURIComponent(s);
  const join = (a, b) => (a === '/' ? '' : a) + '/' + b;
  const parent = (p) => p.substring(0, p.lastIndexOf('/')) || '/';
  const drive = (p) => (/^\/([cde])(\/|$)/.exec(p) || [])[1] || '';
  const crumbs = $derived(path.split('/').filter(Boolean));
  const pct = $derived(total ? Math.min(100, Math.round((used / total) * 100)) : 0);

  async function call(url, opt = {}) {
    const r = await fetch(url, { ...opt, headers: { ...authHeaders(), ...(opt.headers || {}) } });
    if (!r.ok) throw new Error('HTTP ' + r.status);
    return r;
  }
  async function list(p = path) {
    try {
      const d = await (await call('/api/list?path=' + q(p))).json();
      path = p; parts = d.parts || []; total = d.total || 0; used = d.used || 0;
      files = (d.files || []).slice().sort((a, b) => (b.isDir - a.isDir) || a.name.localeCompare(b.name));
      msg = '';
    } catch (e) { msg = t('تعذّر') + ': ' + e.message; }
  }
  async function act(fn) {
    if (busy) return; busy = true; msg = '';
    try { await fn(); } catch (e) { msg = t('تعذّر') + ': ' + e.message; }
    busy = false; await list();
  }
  function needDrive() { if (path === '/') { msg = t('افتح قرصًا (C: D: E:) أولًا'); return true; } return false; }

  const newFile = () => { if (needDrive()) return; const n = prompt(t('اسم الملف:')); if (n) ed = { path: join(path, n.trim()), text: '', dirty: true, isNew: true }; };
  const newDir = () => { if (needDrive()) return; const n = prompt(t('اسم المجلد:')); if (n) act(() => call('/api/mkdir?path=' + q(join(path, n.trim())), { method: 'POST' })); };
  const del = (f) => { if (confirm(t('حذف') + ' ' + f.name + '؟')) act(() => call('/api/delete?path=' + q(join(path, f.name)), { method: 'DELETE' })); };
  const rename = (f) => {
    const n = prompt(t('الاسم الجديد:'), f.name);
    if (n && n !== f.name) act(() => call(`/api/rename?old=${q(join(path, f.name))}&new=${q(join(path, n.trim()))}`, { method: 'POST' }));
  };

  async function download(f) {
    try {
      const b = await (await call('/api/download?path=' + q(join(path, f.name)))).blob();
      const a = document.createElement('a'); a.href = URL.createObjectURL(b); a.download = f.name; a.click();
      setTimeout(() => URL.revokeObjectURL(a.href), 4000);
    } catch (e) { msg = t('تعذّر') + ': ' + e.message; }
  }
  function upload(e) {
    const fl = [...e.currentTarget.files]; e.currentTarget.value = '';
    if (!fl.length || needDrive()) return;
    act(async () => { for (const f of fl) { const fd = new FormData(); fd.append('file', f, join(path, f.name)); await call('/api/upload', { method: 'POST', body: fd }); } });
  }

  async function edit(f) {
    try { ed = { path: join(path, f.name), text: await (await call('/api/read?path=' + q(join(path, f.name)))).text(), dirty: false }; }
    catch (e) { msg = t('تعذّر') + ': ' + e.message; }
  }
  async function save() {
    if (!ed) return;
    try {
      await call('/api/save?path=' + q(ed.path), { method: 'POST', headers: { 'Content-Type': 'application/x-www-form-urlencoded' }, body: new URLSearchParams({ content: ed.text }) });
      ed.dirty = false; ed.isNew = false; msg = t('تم الحفظ');
    } catch (e) { msg = t('تعذّر') + ': ' + e.message; }
  }
  async function closeEd() { if (ed?.dirty && !confirm(t('تغييرات غير محفوظة. إغلاق؟'))) return; ed = null; await list(); }
  function edKey(e) {
    if ((e.ctrlKey || e.metaKey) && e.key === 's') { e.preventDefault(); save(); }
    else if (e.key === 'Tab') { e.preventDefault(); const s = e.target.selectionStart; ed.text = ed.text.slice(0, s) + '  ' + ed.text.slice(e.target.selectionEnd); ed.dirty = true; queueMicrotask(() => (e.target.selectionStart = e.target.selectionEnd = s + 2)); }
  }

  // نسخ/نقل: النقل داخل القرص نفسه rename، وبين الأقراص نسخ ثم حذف. نسخ المجلدات تكراري
  const copy = (f, cut) => { clip = { path: join(path, f.name), name: f.name, isDir: f.isDir, cut }; msg = (cut ? t('قصّ') : t('نسخ')) + ': ' + f.name; };
  async function cp(src, dst, isDir) {
    if (isDir) {
      await call('/api/mkdir?path=' + q(dst), { method: 'POST' });
      const d = await (await call('/api/list?path=' + q(src))).json();
      for (const c of d.files || []) await cp(join(src, c.name), join(dst, c.name), c.isDir);
    } else {
      const b = await (await call('/api/download?path=' + q(src))).blob();
      const fd = new FormData(); fd.append('file', b, dst);
      await call('/api/upload', { method: 'POST', body: fd });
    }
  }
  const rmTree = (p) => call('/api/delete?path=' + q(p), { method: 'DELETE' });
  function paste() {
    if (!clip || needDrive()) return;
    let dst = join(path, clip.name);
    if (dst === clip.path) dst = join(path, t('نسخة') + '_' + clip.name);
    if (dst.startsWith(clip.path + '/')) { msg = t('لا يمكن النسخ داخل نفسه'); return; }
    act(async () => {
      if (clip.cut && drive(clip.path) === drive(dst)) await call(`/api/rename?old=${q(clip.path)}&new=${q(dst)}`, { method: 'POST' });
      else { await cp(clip.path, dst, clip.isDir); if (clip.cut) await rmTree(clip.path); }
      if (clip.cut) clip = null;
    });
  }

  $effect(() => { list('/'); });
</script>

{#if ed}
  <div class="glass">
    <div class="row" style="justify-content:space-between;gap:8px;flex-wrap:nowrap">
      <b style="font-family:monospace;font-size:13px;overflow:hidden;text-overflow:ellipsis;direction:ltr">{ed.path}{ed.dirty ? ' •' : ''}</b>
      <div class="row" style="gap:6px;flex-wrap:nowrap">
        <button class="btn sm green" onclick={save}><Icon name="save" size={14} mono={true} /> {t('حفظ')}</button>
        <button class="btn sm ghost" onclick={closeEd}>{t('إغلاق')}</button>
      </div>
    </div>
    <textarea class="term-input" style="width:100%;min-height:55vh;margin-top:8px;font-family:monospace;font-size:13px;direction:ltr;white-space:pre;tab-size:2"
      bind:value={ed.text} oninput={() => (ed.dirty = true)} onkeydown={edKey} spellcheck="false" autocapitalize="off"></textarea>
    {#if msg}<div style="font-size:12px;color:var(--muted);margin-top:6px">{msg}</div>{/if}
  </div>
{:else}
  <div class="glass">
    <div class="row" style="gap:4px;font-size:13px;margin-bottom:8px;direction:ltr;flex-wrap:wrap">
      <button class="btn sm ghost" onclick={() => list('/')}>💽 {t('الأقراص')}</button>
      {#each crumbs as c, i}
        <span style="color:var(--muted)">/</span>
        <button class="btn sm ghost" onclick={() => list('/' + crumbs.slice(0, i + 1).join('/'))}>{i === 0 && DRV[c] ? c.toUpperCase() + ':' : c}</button>
      {/each}
    </div>

    <div class="row" style="gap:6px;margin-bottom:8px">
      <button class="btn sm" onclick={newFile} disabled={busy}>📄 {t('ملف')}</button>
      <button class="btn sm" onclick={newDir} disabled={busy}><Icon name="folder" size={14} mono={true} /> {t('مجلد')}</button>
      <button class="btn sm" onclick={() => up.click()} disabled={busy}><Icon name="upload" size={14} mono={true} /> {t('رفع')}</button>
      <button class="btn sm ghost" onclick={paste} disabled={busy || !clip}>📋 {t('لصق')}{clip ? ' (' + clip.name + ')' : ''}</button>
      <button class="btn sm ghost" onclick={() => list()} disabled={busy}>↻</button>
      <input type="file" multiple bind:this={up} onchange={upload} style="display:none" />
    </div>

    <div style="height:6px;background:var(--border);border-radius:4px;overflow:hidden"><div style="height:100%;width:{pct}%;background:{pct > 90 ? '#e55' : 'var(--primary)'}"></div></div>
    <div style="font-size:11px;color:var(--muted);margin:3px 0 8px;font-family:monospace">{(used / 1024).toFixed(1)} / {(total / 1024).toFixed(1)} KB ({pct}%)</div>

    {#if msg}<div style="font-size:12px;color:var(--muted);margin-bottom:6px">{msg}</div>{/if}

    {#if path === '/'}
      {#each parts as p}
        {@const pp = p.total ? Math.round((p.used / p.total) * 100) : 0}
        <button class="btn ghost" style="display:block;width:100%;text-align:start;margin-bottom:6px" onclick={() => list('/' + p.id)}>
          <b>💽 {p.id.toUpperCase()}: {t(DRV[p.id][0])}</b> <span style="color:var(--muted);font-size:11px">({p.label})</span>
          <div style="height:5px;background:var(--border);border-radius:3px;margin:5px 0"><div style="height:100%;width:{pp}%;background:{pp > 90 ? '#e55' : 'var(--primary)'}"></div></div>
          <span style="font-size:11px;color:var(--muted)">{((p.total - p.used) / 1024).toFixed(1)} KB {t('حر من')} {(p.total / 1024).toFixed(0)} KB</span>
        </button>
      {/each}
    {:else}
      {#if crumbs.length > 0}
        <button class="btn sm ghost" style="margin-bottom:6px" onclick={() => list(parent(path))}>⬆ ..</button>
      {/if}
      {#each files as f (f.name)}
        <div class="row" style="gap:6px;padding:6px 0;border-top:1px solid var(--border);flex-wrap:nowrap;align-items:center">
          <button class="btn ghost" style="flex:1;min-width:0;text-align:start;padding:6px 8px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;direction:ltr"
            onclick={() => (f.isDir ? list(join(path, f.name)) : edit(f))}>
            {f.isDir ? '📁' : '📄'} {f.name}
            {#if !f.isDir}<span style="color:var(--muted);font-size:11px"> · {f.size < 1024 ? f.size + ' B' : (f.size / 1024).toFixed(1) + ' KB'}</span>{/if}
          </button>
          <div class="row" style="gap:3px;flex-wrap:nowrap">
            {#if !f.isDir}<button class="btn sm ghost icon-btn" title={t('تنزيل')} onclick={() => download(f)}><Icon name="download" size={14} /></button>{/if}
            <button class="btn sm ghost icon-btn" title={t('نسخ')} onclick={() => copy(f, false)}>⧉</button>
            <button class="btn sm ghost icon-btn" title={t('قصّ')} onclick={() => copy(f, true)}>✂</button>
            <button class="btn sm ghost icon-btn" title={t('إعادة تسمية')} onclick={() => rename(f)}><Icon name="edit" size={14} /></button>
            <button class="btn sm ghost icon-btn" title={t('حذف')} onclick={() => del(f)}><Icon name="trash" size={14} /></button>
          </div>
        </div>
      {:else}
        <div style="color:var(--muted);font-size:13px;padding:8px 0">{t('المجلد فارغ')}</div>
      {/each}
    {/if}
  </div>
{/if}
