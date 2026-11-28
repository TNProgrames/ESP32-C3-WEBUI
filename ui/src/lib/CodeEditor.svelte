<script>
  import { highlight } from '../highlight.js';
  import { t } from '../store.svelte.js';
  import Icon from './Icon.svelte';

  // محرر كود: طبقة ملوّنة خلف textarea شفاف. Tab/مسافة تلقائية/سطر الخطأ، ومع toolbar: خط، التفاف، بحث، لغة، شريط حالة (كمحرر em.html)
  let { value = $bindable(''), lang = $bindable('lua'), errLine = 0, height = 240, toolbar = false, fill = false, onsave = null, onchange = null } = $props();

  let ta = $state(), pre = $state(), gut = $state();
  let st = $state(0), pos = $state('1:1'), q = $state(''), wrap = $state(false);
  let fs = $state(+(safe('ce-font') || 12.5));
  const LANGS = [['plain', 'Text'], ['lua', 'Lua'], ['json', 'JSON'], ['html', 'HTML'], ['css', 'CSS'], ['js', 'JS'], ['c', 'C/C++']];
  const lh = $derived(Math.round(fs * 1.5));
  const html = $derived(highlight(value, lang) + '\n');
  const nLines = $derived(value.split('\n').length);
  const nums = $derived(Array.from({ length: nLines }, (_, i) => i + 1).join('\n'));
  function safe(k, v) { try { return v === undefined ? localStorage.getItem(k) : localStorage.setItem(k, v); } catch { return null; } }

  function sync() {
    if (!ta) return;
    st = ta.scrollTop;
    if (pre) { pre.scrollTop = ta.scrollTop; pre.scrollLeft = ta.scrollLeft; }
    if (gut) gut.scrollTop = ta.scrollTop;
  }
  $effect(() => { value; queueMicrotask(sync); });
  function status() {
    if (!ta) return;
    const l = ta.value.slice(0, ta.selectionStart).split('\n');
    pos = l.length + ':' + (l[l.length - 1].length + 1);
  }
  const font = (d) => { fs = Math.max(10, Math.min(24, fs + d)); safe('ce-font', fs); };
  function find(back) {
    if (!q || !ta) return;
    const v = value.toLowerCase(), k = q.toLowerCase();
    let i = back ? v.lastIndexOf(k, ta.selectionStart - 1) : v.indexOf(k, ta.selectionEnd);
    if (i < 0) i = back ? v.lastIndexOf(k) : v.indexOf(k);
    if (i < 0) return;
    ta.focus(); ta.setSelectionRange(i, i + k.length);
    ta.scrollTop = Math.max(0, (v.slice(0, i).split('\n').length - 1) * lh - ta.clientHeight / 2);
    sync(); status();
  }
  function changed() { onchange?.(); }

  function insert(text) {
    const s = ta.selectionStart, e = ta.selectionEnd;
    ta.setRangeText(text, s, e, 'end');
    value = ta.value; changed();
  }

  function keydown(e) {
    if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 's' && onsave) { e.preventDefault(); onsave(); return; }
    if (e.key === 'Tab') {
      e.preventDefault();
      const s = ta.selectionStart, en = ta.selectionEnd;
      if (s !== en && value.slice(s, en).includes('\n')) {
        // مسافة بادئة لعدة أسطر (Shift = إزالة)
        const ls = value.lastIndexOf('\n', s - 1) + 1;
        const block = value.slice(ls, en).split('\n');
        const out = block.map((l) => (e.shiftKey ? l.replace(/^ {1,2}/, '') : '  ' + l)).join('\n');
        ta.setRangeText(out, ls, en, 'select');
        value = ta.value; changed();
      } else if (e.shiftKey) {
        const ls = value.lastIndexOf('\n', s - 1) + 1;
        if (value.slice(ls, ls + 2) === '  ') { ta.setRangeText('', ls, ls + 2, 'preserve'); value = ta.value; changed(); }
      } else insert('  ');
    } else if (e.key === 'Enter' && !e.ctrlKey && !e.metaKey && ta.selectionStart === ta.selectionEnd) {
      // مسافة بادئة تلقائية كالسطر الحالي، وزيادة بعد فتح كتلة
      const p = ta.selectionStart;
      const ls = value.lastIndexOf('\n', p - 1) + 1;
      const line = value.slice(ls, p);
      const indent = line.match(/^\s*/)[0];
      const opens = lang === 'html'
        ? /<(?!\/|!|meta|br|hr|img|input|link)[A-Za-z][^>]*[^\/]>\s*$/.test(line)
        : lang === 'lua'
          ? /(\bthen|\bdo|\belse|\brepeat|\bfunction\b[^\n]*\)|\{)\s*(--.*)?$/.test(line)
          : /[{\[(]\s*$/.test(line);
      e.preventDefault();
      insert('\n' + indent + (opens ? '  ' : ''));
    }
  }
</script>

<div class="wrapper" class:fill style={fill ? '' : `height:${height + (toolbar ? 78 : 0)}px`}>
  {#if toolbar}
    <div class="tools">
      <button class="btn sm ghost icon-btn" onclick={() => font(-1)} aria-label="A-">A-</button>
      <button class="btn sm ghost icon-btn" onclick={() => font(1)} aria-label="A+">A+</button>
      <button class="btn sm ghost" class:on={wrap} onclick={() => (wrap = !wrap)}><Icon name="wrap" size={14} mono={true} /> {t('التفاف')}</button>
      <select class="sel" bind:value={lang} aria-label="syntax">{#each LANGS as [v, l]}<option value={v}>{l}</option>{/each}</select>
      <div class="find">
        <Icon name="search" size={14} mono={true} />
        <input bind:value={q} placeholder={t('بحث...')} onkeydown={(e) => { if (e.key === 'Enter') { e.preventDefault(); find(e.shiftKey); } }} />
      </div>
      <button class="btn sm ghost icon-btn" onclick={() => find(false)} aria-label="next"><Icon name="arrow-down" size={14} mono={true} /></button>
      <button class="btn sm ghost icon-btn" onclick={() => find(true)} aria-label="prev"><Icon name="arrow-up" size={14} mono={true} /></button>
    </div>
  {/if}
  <div class="ce" style="--fs:{fs}px;--lh:{lh}px">
    <pre class="gut" bind:this={gut} aria-hidden="true">{nums}</pre>
    <div class="body">
      <pre class="hl" class:wrap bind:this={pre} aria-hidden="true">{@html html}</pre>
      {#if errLine > 0}<div class="errline" style="top:{8 + (errLine - 1) * lh - st}px"></div>{/if}
      <textarea class:wrap bind:this={ta} bind:value oninput={() => { changed(); status(); }} onscroll={sync} onkeydown={keydown} onkeyup={status} onclick={status}
        spellcheck="false" autocapitalize="off" autocomplete="off" autocorrect="off" wrap={wrap ? 'soft' : 'off'}></textarea>
    </div>
  </div>
  {#if toolbar}<div class="status"><span>{pos}</span><span>{nLines} {t('سطر')} · {value.length} B</span></div>{/if}
</div>

<style>
  .wrapper { display: flex; flex-direction: column; direction: ltr; border: 1px solid var(--border); border-radius: 12px; background: var(--input); overflow: hidden; }
  .wrapper.fill { height: 100%; min-height: 50vh; }
  .ce { display: flex; flex: 1; min-height: 0; direction: ltr; text-align: left; position: relative; }
  .tools { display: flex; gap: 6px; padding: 7px 10px; border-bottom: 1px solid var(--border); align-items: center; flex-wrap: wrap; background: var(--panel-2, rgba(128,128,128,.08)); }
  .tools :global(.btn.on) { background: var(--primary); color: #fff; }
  .sel { padding: 5px 8px; border-radius: 8px; border: 1px solid var(--border); background: var(--input); color: var(--text); font-size: 12px; font-weight: 600; }
  .find { position: relative; flex: 1; min-width: 100px; max-width: 220px; display: flex; align-items: center; }
  .find :global(svg) { position: absolute; left: 8px; color: var(--muted); pointer-events: none; }
  .find input { width: 100%; padding: 6px 10px 6px 28px; border-radius: 8px; border: 1px solid var(--border); background: var(--input); color: var(--text); font-size: 12.5px; direction: ltr; outline: none; }
  .find input:focus, .sel:focus { border-color: var(--primary); }
  .status { display: flex; justify-content: space-between; padding: 4px 12px; font: 11px monospace; color: var(--muted); border-top: 1px solid var(--border); background: var(--panel-2, rgba(128,128,128,.08)); }
  .wrap { white-space: pre-wrap !important; overflow-wrap: anywhere; overflow-x: hidden !important; }
  .gut, .hl, textarea { margin: 0; font: var(--fs) / var(--lh) ui-monospace, SFMono-Regular, Menlo, Consolas, 'Liberation Mono', 'Courier New', monospace; font-variant-ligatures: none; tab-size: 2; -moz-tab-size: 2; letter-spacing: 0; }
  .gut { padding: 8px 8px 8px 10px; min-width: 2.4em; text-align: right; color: var(--muted); opacity: .6; overflow: hidden; user-select: none; border-right: 1px solid var(--border); }
  .body { position: relative; flex: 1; min-width: 0; }
  .hl, textarea { position: absolute; inset: 0; padding: 8px 10px; width: 100%; height: 100%; box-sizing: border-box; white-space: pre; overflow: auto; border: 0; }
  .hl { overflow: hidden; color: var(--text); pointer-events: none; }
  textarea { background: transparent; color: transparent; -webkit-text-fill-color: transparent; caret-color: var(--text); resize: none; outline: none; appearance: none; -webkit-appearance: none; border-radius: 0; direction: ltr; text-align: left; }
  textarea::selection { background: rgba(99, 102, 241, .35); -webkit-text-fill-color: transparent; }
  .errline { position: absolute; left: 0; right: 0; height: var(--lh); background: rgba(239, 68, 68, .28); border-left: 3px solid #ef4444; pointer-events: none; }

  .hl :global(.tk-k) { color: #c792ea; font-weight: 600; }
  .hl :global(.tk-l) { color: #82aaff; font-weight: 600; }
  .hl :global(.tk-b) { color: #7dcfff; }
  .hl :global(.tk-fn) { color: #ffcb6b; }
  .hl :global(.tk-p) { color: #f07178; }
  .hl :global(.tk-s) { color: #a5e075; }
  .hl :global(.tk-n) { color: #f78c6c; }
  .hl :global(.tk-c) { color: #7a8394; font-style: italic; }
  .hl :global(.tk-o) { color: #89ddff; }
  .hl :global(.tk-t) { color: #f07178; }
  .hl :global(.tk-a) { color: #ffcb6b; }
  :global([data-theme="light"]) .hl :global(.tk-k) { color: #7c3aed; }
  :global([data-theme="light"]) .hl :global(.tk-l) { color: #1d4ed8; }
  :global([data-theme="light"]) .hl :global(.tk-b) { color: #0369a1; }
  :global([data-theme="light"]) .hl :global(.tk-fn) { color: #b45309; }
  :global([data-theme="light"]) .hl :global(.tk-p) { color: #be123c; }
  :global([data-theme="light"]) .hl :global(.tk-s) { color: #15803d; }
  :global([data-theme="light"]) .hl :global(.tk-n) { color: #c2410c; }
  :global([data-theme="light"]) .hl :global(.tk-c) { color: #6b7280; }
  :global([data-theme="light"]) .hl :global(.tk-o) { color: #0e7490; }
  :global([data-theme="light"]) .hl :global(.tk-t) { color: #be123c; }
  :global([data-theme="light"]) .hl :global(.tk-a) { color: #b45309; }
</style>
