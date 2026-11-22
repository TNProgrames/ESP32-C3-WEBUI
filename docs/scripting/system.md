# system — `log` و`storage` و`bus` و`sys`

## log
`log(...)` يكتب في **الطرفية** (تبويب 💻) مسبوقًا باسم السكريبت. يقبل عدة معاملات، و`print` مرادف له. السطر حتى 160 حرفًا.

## storage — حفظ قيم بين التشغيلات
- `storage.get(key [, default])`
- `storage.set(key, value)` (رقم أو نص أو منطقي)
- `storage.remove(key)`

لكل سكريبت مخزن خاص، حتى **2KB**، ويُحفظ في `/scripts/.data/`. الكتابة تُدمج ولا تتجاوز مرة كل بضع ثوانٍ لحماية الفلاش.

```lua
local count = storage.get("boots", 0) + 1
storage.set("boots", count)
```

## bus — التبادل بين السكريبتات
- `bus.publish(topic, value)`: يرسل لكل المشتركين (رقم أو نص أو منطقي أو جدول بسيط).
- `bus.subscribe(topic, fn)`: يستدعي `fn(value)` عند كل نشر.

```lua
-- سكريبت أ
bus.publish("temp", 29.5)
-- سكريبت ب
bus.subscribe("temp", function(v) log("وصلت الحرارة", v) end)
```

## sys
- `sys.millis()`: ميلي ثانية منذ إقلاع الجهاز.
- `sys.heap()`: الذاكرة الحرة بالبايت.
- `sys.version()`: إصدار الفيرموير.
