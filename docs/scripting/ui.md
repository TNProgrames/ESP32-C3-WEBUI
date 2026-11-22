# ui — العلب والأدوات في الواجهة

## `ui.panel(title) → panel`
ينشئ **علبة (div)** في تبويب السكريبت، داخل بطاقة السكريبت. يمكن إنشاء أكثر من علبة، وتُحذف كلها عند إيقاف السكريبت. مُعرّفات الأدوات مستقلة لكل سكريبت.

## الأدوات
كل أداة لها `id` فريد داخل السكريبت.

| الدالة | الوصف |
|---|---|
| `panel:label(id, text)` | نص |
| `panel:button(id, text, fn)` | زر؛ `fn()` عند الضغط |
| `panel:switch(id, text, value, fn)` | مفتاح؛ `fn(value)` عند التبديل |
| `panel:slider(id, text, min, max, value [, fn])` | شريط منزلق؛ `fn(value)` عند الحركة |
| `panel:input(id, text, value [, fn])` | حقل نص؛ `fn(text)` عند الإدخال |
| `panel:progress(id, text, value)` | شريط تقدم من 0 إلى 100 |
| `panel:chart(id, title [, max_points])` | رسم بياني خطي (الافتراضي 60 نقطة) |

## التحكم بالأدوات
- `panel:set(id, value)`: يحدّث نص أو قيمة أداة.
- `panel:get(id)`: يقرأ القيمة الحالية.
- `panel:push(id, number)`: يضيف نقطة إلى رسم بياني.
- `panel:remove(id)`: يحذف أداة.
- `panel:title(text)`: يغير عنوان العلبة.

```lua
local box = ui.panel("المراقبة")
box:label("t", "الحرارة: --")
box:chart("c", "الحرارة", 120)
box:switch("fan", "المروحة", false, function(on) gpio.write(5, on and 1 or 0) end)

timer.every(2000, function()
  local t = adc.read_mv(0) / 10
  box:set("t", string.format("الحرارة: %.1f °C", t))
  box:push("c", t)
end)
```

## كيف تصل للواجهة
الجهاز يرسل وصف الأدوات والتحديثات كـ JSON، والواجهة ترسمها كعناصر HTML وتحدّثها دوريًا، وأحداث الأزرار تعود للجهاز فتستدعي دالة السكريبت. لا يُسمح بإرسال HTML خام، فكل النصوص تُعرض كنص آمن.

## حدود
- 20 أداة لكل سكريبت، و4 علب.
- النص حتى 64 حرفًا، وعدد التحديثات يُدمج (حد أقصى تقريبًا 5 في الثانية لكل أداة).
