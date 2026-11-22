# ui — العلب والأدوات في الواجهة

لواجهة السكريبت **طبقتان**:

1. **أدوات جاهزة** (الأغلب): خفيفة وآمنة وتتبع اللغة والمظهر تلقائيًا.
2. **صفحة مخصصة** (HTML + CSS + JS) داخل إطار معزول، للحرية الكاملة.

## ui.panel(title) → panel

ينشئ **علبة (div)** داخل بطاقة السكريبت. يمكن إنشاء أكثر من علبة، وتُحذف كلها عند إيقاف السكريبت. مُعرّفات الأدوات مستقلة لكل سكريبت.

---

## الطبقة 1: الأدوات الجاهزة

كل أداة لها `id` فريد داخل السكريبت، وآخر معامل اختياري `style` (جدول).

| الدالة | الوصف |
|---|---|
| `panel:label(id, text [, style])` | نص |
| `panel:button(id, text, fn [, style])` | زر؛ `fn()` عند الضغط |
| `panel:switch(id, text, value, fn [, style])` | مفتاح؛ `fn(value)` |
| `panel:slider(id, text, min, max, value [, fn])` | شريط منزلق؛ `fn(value)` |
| `panel:input(id, text, value [, fn])` | حقل نص؛ `fn(text)` |
| `panel:select(id, text, options, value [, fn])` | قائمة اختيار؛ `options` جدول نصوص |
| `panel:color(id, text, value [, fn])` | منتقي لون؛ `fn("#rrggbb")` |
| `panel:progress(id, text, value)` | شريط تقدم 0–100 |
| `panel:gauge(id, text, min, max, value [, style])` | عداد دائري |
| `panel:led(id, text, state)` | مؤشر LED (`"on"` `"off"` `"warn"` `"err"`) |
| `panel:chart(id, title [, max_points])` | رسم بياني خطي (الافتراضي 60 نقطة) |
| `panel:table(id, title, columns)` | جدول؛ الأعمدة جدول نصوص |

### التخطيط
- `panel:row()` يعيد حاوية أفقية تُنشأ عليها الأدوات بنفس الدوال: `local r = box:row(); r:button(...)`.
- `panel:grid(columns)` يعيد شبكة بعدد أعمدة محدد.

### التنسيق (`style`)

| المفتاح | القيم |
|---|---|
| `icon` | إيموجي، مثل `"🌡️"` |
| `state` | `"ok"` `"warn"` `"err"` `"info"` |
| `size` | `"sm"` `"md"` `"lg"` `"xl"` |
| `unit` | وحدة تُعرض بعد القيمة (للعداد) |

### التحكم بالأدوات
- `panel:set(id, value)`: يحدّث نصًا أو قيمة أداة.
- `panel:get(id)`: يقرأ القيمة الحالية.
- `panel:push(id, number)`: يضيف نقطة لرسم بياني.
- `panel:rows(id, rows)`: يستبدل صفوف جدول (جدول جداول).
- `panel:style(id, style)`: يغيّر تنسيق أداة.
- `panel:remove(id)`: يحذف أداة.
- `panel:title(text)`: يغيّر عنوان العلبة.

```lua
local box = ui.panel("المراقبة")
local r = box:row()
r:gauge("g", "الحرارة", 0, 50, 0, { unit = "°C", icon = "🌡️" })
r:led("fan", "المروحة", "off")
box:chart("c", "الحرارة", 120)
box:switch("auto", "تشغيل تلقائي", true, function(on) storage.set("auto", on) end)

timer.every(2000, function()
  local t = adc.read_mv(0) / 10
  box:set("g", t)
  box:push("c", t)
  box:style("g", { state = t > 35 and "err" or "ok" })
end)
```

### حدود
- 20 أداة لكل سكريبت، و4 علب.
- النص حتى 64 حرفًا. التحديثات تُدمج (حوالي 5 في الثانية لكل أداة).

---

## الطبقة 2: صفحة مخصصة

لحرية كاملة تكتب HTML وCSS وJS في ملف، وتعرضه داخل العلبة.

```lua
local p = ui.panel("لوحتي")
p:page("ui/gauge.html", { height = 240 })   -- ملف من /scripts/ui/
p:emit("temp", 29.5)                         -- يرسل قيمة للصفحة
p:on("setpoint", function(v) ... end)        -- يستقبل حدثًا منها
```

### داخل الصفحة (جانب المتصفح)

```html
<div id="v">--</div>
<button onclick="esp.send('setpoint', 25)">اضبط 25</button>
<script>
  esp.on('temp', v => document.getElementById('v').textContent = v + ' °C');
  esp.onTheme(t => {});      // يُستدعى عند تغير المظهر/اللغة
  // esp.theme  : {bg, panel, text, muted, primary, ...}
  // esp.lang   : "ar" | "en"
</script>
```

متغيرات CSS (`--bg` و`--panel` و`--text` و`--primary` ...) تُحقن تلقائيًا لتتماشى الصفحة مع الواجهة.

### العزل (أمان)
- الصفحة داخل `<iframe sandbox="allow-scripts">` **بدون** `allow-same-origin`: لا ترى رمز الدخول ولا الكوكيز ولا تستدعي API الجهاز.
- سياسة CSP تمنع أي اتصال شبكي من الصفحة (`default-src 'none'`)، فلا تحميل من الإنترنت ولا تسريب بيانات.
- التواصل مع الجهاز **فقط** عبر الجسر `esp.on` و`esp.send`، والواجهة تمرر الرسائل.
- مفيد خصوصًا عند استعمال سكريبتات كتبها غيرك.

### حدود
- حجم ملف الصفحة حتى 24KB. صفحتان لكل سكريبت.
- المكتبات الخارجية (مثل Chart.js) لا تُحمَّل من الإنترنت؛ تُضمَّن في الملف نفسه، أو تُطلب كمكتبة مضمنة: `p:page("ui/x.html", { libs = {"chart"} })` (مخطط).
- الحساب كله في **المتصفح**، فلا يزيد حمل الجهاز. والحجم الكبير للصفحة يُرسل مرة واحدة ثم تتحدث القيم فقط.

## كيف تصل للواجهة
الجهاز يرسل وصف الأدوات والتحديثات كـ JSON، والواجهة ترسمها وتحدّثها دوريًا، وأحداث الأزرار تعود للجهاز فتستدعي دالة السكريبت. لا يُسمح بإرسال HTML خام في الطبقة 1، فكل النصوص تُعرض كنص آمن.
