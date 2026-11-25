<script>
  // حقل كلمة مرور مع زر إظهار/إخفاء
  let { value = $bindable(''), id = undefined, placeholder = '', autocomplete = 'off' } = $props();
  let show = $state(false);
</script>

<div class="row" style="gap:6px;align-items:stretch">
  <input {id} style="flex:1;min-width:0" type={show ? 'text' : 'password'} bind:value {placeholder} {autocomplete} />
  <button type="button" class="btn ghost" aria-label="show password" aria-pressed={show} onclick={() => (show = !show)}>{show ? '🙈' : '👁'}</button>
</div>
