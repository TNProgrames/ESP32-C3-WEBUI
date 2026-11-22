# أمثلة

كل أمثلة القسم الأول تستعمل **فقط ما هو منفَّذ حاليًا** ويمكن لصقها في تبويب **🧪 مختبر Lua**. الأرجل المستعملة (4 و5 و6 و7 و8 و10 وغيرها) غيّرها بحسب لوحتك. القسم الثاني يحتاج وحدات لم تُنفَّذ بعد.

> الأمثلة العملية الجاهزة في قائمة "أمثلة..." داخل المختبر: Hello وBlink وTimer وADC وPWM وأدوات جاهزة وعداد حرارة وصفحة الرسم البياني.

---

# الجزء 1: يعمل الآن

## أساسيات

### 1) Hello وحالة الذاكرة
```lua
print("hello", 1 + 2, 10 / 4)
print("free heap", sys.heap(), "uptime ms", sys.millis())
```

### 2) نصائح Lua: format وجداول وترتيب
```lua
local t = { 5, 2, 9, 1 }
table.sort(t)
print(table.concat(t, ","))                       --> 1,2,5,9
print(string.format("%5.2f | %04d | %s", 3.14159, 42, "ok"))
print(math.random(1, 6), math.floor(7.8), math.max(3, 9, 4))
```

### 3) التقاط الأخطاء بـ pcall
```lua
local ok, err = pcall(function() gpio.write(15, 1) end)   -- رجل ممنوعة
print(ok, err)   --> false   ... pin 15 not allowed
```

## GPIO

### 4) ليد يومض
```lua
gpio.mode(8, "out")
local on = false
timer.every(500, function()
  on = not on
  gpio.write(8, on)
end)
```

### 5) ليدان بإيقاعين مختلفين
```lua
gpio.mode(8, "out"); gpio.mode(10, "out")
local a, b = false, false
timer.every(300,  function() a = not a; gpio.write(8,  a) end)
timer.every(1000, function() b = not b; gpio.write(10, b) end)
```

### 6) زر يبدّل ليد (قراءة دورية مع كشف الحافة)
```lua
gpio.mode(4, "pullup")      -- الزر بين الرجل 4 والأرضي
gpio.mode(8, "out")
local last, on = 1, false
timer.every(20, function()
  local v = gpio.read(4)
  if v == 0 and last == 1 then        -- لحظة الضغط فقط
    on = not on
    gpio.write(8, on)
    print("toggle", on)
  end
  last = v
end)
```

### 7) إشارة مرور بتسلسل مؤقتات
```lua
local pins = { 5, 6, 7 }                -- أحمر، أصفر، أخضر
for _, p in ipairs(pins) do gpio.mode(p, "out") end
local function show(i) for k, p in ipairs(pins) do gpio.write(p, k == i) end end

local seq = { {1, 3000}, {3, 3000}, {2, 1000} }   -- {الضوء, المدة ms}
local i = 0
local function step()
  i = i % #seq + 1
  show(seq[i][1])
  timer.after(seq[i][2], step)
end
step()
```

### 8) نمط sleep داخل coroutine (تجريبي)
```lua
local function sleep(ms)
  local co = coroutine.running()
  timer.after(ms, function() coroutine.resume(co) end)
  coroutine.yield()
end
gpio.mode(8, "out")
coroutine.wrap(function()
  while true do
    gpio.write(8, 1); sleep(100)
    gpio.write(8, 0); sleep(900)
  end
end)()
```

## ADC و PWM

### 9) فولتميتر بمتوسط 8 قراءات (GPIO0)
```lua
local function avg(pin, n)
  local s = 0
  for i = 1, n do s = s + adc.mv(pin) end
  return s / n
end
timer.every(1000, function()
  print(string.format("A0 = %.0f mV", avg(0, 8)))
end)
```

### 10) مقسّم جهد (يقرأ حتى 6.6V بمقاومتين متساويتين)
```lua
timer.every(1000, function()
  local v = adc.mv(1) * 2 / 1000
  print(string.format("battery %.2f V", v))
end)
```

### 11) إضاءة "تنفّس"
```lua
local d, dir = 0, 5
timer.every(30, function()
  d = d + dir
  if d >= 100 or d <= 0 then dir = -dir end
  pwm.set(8, 1000, d)                 -- pin, freq, duty%
end)
```

### 12) تحريك سيرفو 0–180° (إشارة 50Hz)
```lua
local angle, dir = 0, 3
timer.every(40, function()
  angle = angle + dir
  if angle >= 180 or angle <= 0 then dir = -dir end
  local duty = 2.5 + angle / 180 * 10      -- نبضة 0.5–2.5ms من 20ms
  pwm.set(5, 50, duty)
end)
```

### 13) منظّم حرارة: مروحة PWM حسب حساس LM35 (GPIO0)
```lua
timer.every(1000, function()
  local t = adc.mv(0) / 10                    -- 10mV لكل درجة
  local duty = math.max(0, math.min(100, (t - 25) * 10))
  pwm.set(5, 25000, duty)
  print(string.format("%.1f C -> fan %d%%", t, duty))
end)
```

## اللوحات (طبقة الأدوات الجاهزة)

### 14) مفتاح وشريط وزر
```lua
local p = ui.panel("Control")
gpio.mode(8, "out")
local count = 0

p:label("st", "LED: OFF")
p:switch("led", "LED (GPIO8)", function(on)
  gpio.write(8, on)
  p:set("st", on and "LED: ON" or "LED: OFF")
end)
p:slider("br", "Brightness (GPIO10)", 0, 100, function(v) pwm.set(10, 1000, v) end, 0)
p:button("inc", "Count +1", function()
  count = count + 1
  p:set("cnt", "pressed " .. count .. " times")
end)
p:label("cnt", "pressed 0 times")
```

### 15) ساعة إيقاف
```lua
local p = ui.panel("Stopwatch")
local running, t0, acc = false, 0, 0
local function fmt(ms)
  local s = ms // 1000
  return string.format("%02d:%02d.%d", s // 60, s % 60, (ms % 1000) // 100)
end
p:label("t", "00:00.0")
p:button("go", "Start / Stop", function()
  if running then acc = acc + (sys.millis() - t0) else t0 = sys.millis() end
  running = not running
end)
p:button("rst", "Reset", function() running = false; acc = 0; p:set("t", fmt(0)) end)
timer.every(100, function()
  if running then p:set("t", fmt(acc + sys.millis() - t0)) end
end)
```

### 16) لوحة قياس: حرارة + شريط + تنبيه (GPIO0)
```lua
local p = ui.panel("Thermometer")
local limit = 30
p:label("t", "-- C")
p:progress("bar", "Temperature (0-50C)", 0)
p:slider("lim", "Alarm limit", 20, 45, function(v) limit = v end, limit)
p:label("alarm", "OK")
gpio.mode(8, "out")

timer.every(1000, function()
  local t = adc.mv(0) / 10
  p:set("t", string.format("%.1f C", t))
  p:set("bar", t * 2)
  local hot = t > limit
  p:set("alarm", hot and "ALARM" or "OK")
  gpio.write(8, hot)
end)
```

### 17) تحكم مروحة: آلي/يدوي
```lua
local p = ui.panel("Fan")
local auto, manual = true, 0
p:label("t", "-- C")
p:switch("auto", "Auto (by temperature)", function(v) auto = v end, true)
p:slider("man", "Manual %", 0, 100, function(v) manual = v end, 0)
p:progress("duty", "Fan %", 0)

timer.every(1000, function()
  local t = adc.mv(0) / 10
  local d = auto and math.max(0, math.min(100, (t - 25) * 10)) or manual
  pwm.set(5, 25000, d)
  p:set("t", string.format("%.1f C", t))
  p:set("duty", math.floor(d))
end)
```

### 18) نرد وإدخال نص
```lua
local p = ui.panel("Toys")
p:label("r", "press roll")
p:button("roll", "Roll", function() p:set("r", "dice: " .. math.random(1, 6)) end)
p:input("msg", "Say something", function(s) print("you said:", s) end, "hi")
```

## صفحات مخصصة (طبقة iframe)

الصفحة ملف HTML على الجهاز (`/ui/<name>.html`). في المختبر: فعّل "صفحة HTML مخصصة" واكتب المسار والمحتوى، فتُحفظ قبل التشغيل. داخل الصفحة الكائن `esp` (انظر [ui.md](ui.md)).

### 19) متحكم RGB: منتقي لون يقود ثلاث قنوات PWM
**Lua** (`rgb`):
```lua
local p = ui.panel("RGB")
p:page("ui/rgb.html")
local pins = { r = 5, g = 6, b = 7 }
for k, pin in pairs(pins) do
  p:on(k, function(v) pwm.set(pin, 1000, v) end)   -- v من 0 إلى 100
end
```
**الصفحة** `/ui/rgb.html`:
```html
<!doctype html><meta charset="utf-8">
<style>
  body{margin:0;padding:12px;text-align:center;font-family:system-ui;color:#999}
  input{width:120px;height:120px;border:0;background:none}
  [data-theme=light] body{color:#333}
</style>
<input type="color" id="c" value="#ff8800">
<div id="v">pick a color</div>
<script>
  c.onchange = function () {
    var h = c.value, k = ["r","g","b"];
    for (var i = 0; i < 3; i++)
      esp.send(k[i], Math.round(parseInt(h.substr(1 + 2*i, 2), 16) / 2.55));
    v.textContent = h;
  };
</script>
```

### 20) شاشة زمن التشغيل الكبيرة
**Lua**:
```lua
local p = ui.panel("Clock")
p:page("ui/clock.html")
timer.every(1000, function() p:emit("up", sys.millis() // 1000) end)
```
**الصفحة** `/ui/clock.html` (تتبع السمة واللغة):
```html
<!doctype html><meta charset="utf-8">
<style>
  body{margin:0;padding:10px;text-align:center;font-family:system-ui;color:#eee}
  [data-theme=light] body{color:#111}
  #t{font-size:44px;font-weight:bold;font-variant-numeric:tabular-nums}
</style>
<div id="t">--:--:--</div>
<div id="l" style="color:#888"></div>
<script>
  function pad(n){return (n<10?"0":"")+n}
  esp.on("up", function (s) {
    t.textContent = pad(Math.floor(s/3600)) + ":" + pad(Math.floor(s/60)%60) + ":" + pad(s%60);
  });
  function lbl(){ l.textContent = esp.lang === "ar" ? "زمن التشغيل" : "Uptime"; }
  esp.onchange = lbl; lbl();
</script>
```

### 21) صفحة تجمع الاتجاهين: لوحة أزرار تتحكم وتعرض الحالة
**Lua**:
```lua
local p = ui.panel("Pad")
p:page("ui/pad.html")
gpio.mode(8, "out")
local on = false
p:on("toggle", function()
  on = not on
  gpio.write(8, on)
  p:emit("state", on)          -- تأكيد من الجهاز: القيمة المعروضة هي الحقيقية
end)
p:emit("state", on)
```
**الصفحة** `/ui/pad.html`:
```html
<!doctype html><meta charset="utf-8">
<style>
  body{margin:0;padding:10px;text-align:center;font-family:system-ui}
  button{font-size:20px;padding:14px 28px;border:0;border-radius:14px;color:#fff;background:#6366f1}
  .on{background:#10b981}
</style>
<button id="b">LED</button>
<script>
  b.onclick = function () { esp.send("toggle", true); };
  esp.on("state", function (s) { b.className = s ? "on" : ""; b.textContent = "LED " + (s ? "ON" : "OFF"); });
</script>
```

## أفكار لتجرّبها مع تعديل بسيط
- **أضواء متتابعة (Knight Rider):** مصفوفة أرجل ومؤشر يتحرك ذهابًا وإيابًا بـ `timer.every(80, ...)`.
- **مؤقت مطبخ:** لوحة فيها `input` للدقائق وزر بدء، و`timer.after` يشغّل ليدًا عند الانتهاء.
- **مقياس إضاءة:** LDR على GPIO1 مع مقسّم، وشريط تقدم في لوحة، ويُشغّل ليلًا ليدًا بـ PWM.
- **ملاحظة:** مع سكريبتات متعددة، اجعل لكل واحد لوحته، فلا تتشارك الأسماء بين السكريبتات.

---

# الجزء 2: مخطَّط (يحتاج وحدات لم تُنفَّذ بعد)

> هذه تصاميم للاتفاق على الشكل قبل التنفيذ، وقد تتغيّر الأسماء.

## زر بحدث تغيّر (بدل القراءة الدورية)
```lua
--@name زر
--@pins 4,8
gpio.mode(8, "out")
gpio.mode(4, "in_pullup")
gpio.on_change(4, function(v)
  gpio.write(8, v == 0 and 1 or 0)   -- الزر مضغوط = 0
end)
```

## شاشة OLED عبر I2C
```lua
--@name ساعة
--@pins 6,7
--@needs i2c
local ssd1306 = require("lib/ssd1306")
i2c.begin(6, 7, 400000)
local oled = ssd1306.new(0x3C, 128, 64)

timer.every(1000, function()
  oled:clear()
  oled:text(0, 0, "up: " .. sys.millis() // 1000 .. "s")
  oled:show()
end)
```

## حساس حرارة ورطوبة وضغط BME280 + لوحة
```lua
--@pins 6,7
--@needs i2c
local bme = require("lib/bme280").new(0x76)
local p = ui.panel("Weather")
p:label("t", "--"); p:label("h", "--"); p:label("p", "--")
timer.every(2000, function()
  local r = bme:read()
  p:set("t", string.format("%.1f C", r.temp))
  p:set("h", string.format("%.0f %%", r.hum))
  p:set("p", string.format("%.0f hPa", r.press))
end)
```

## تبادل بين سكريبتين بـ bus
```lua
-- حساس.lua
--@pins 0
timer.every(1000, function() bus.publish("temp", adc.mv(0) / 10) end)

-- عرض.lua
local box = ui.panel("العرض")
box:label("v", "--")
bus.subscribe("temp", function(v) box:set("v", string.format("%.1f", v)) end)
```

## عدّاد إقلاعات محفوظ
```lua
local n = storage.get("boots", 0) + 1
storage.set("boots", n)
log("boot number", n)
```

## شريط ليد WS2812 (يُنفَّذ في الفيرموير عبر RMT)
```lua
--@pins 5
--@needs rmt
local strip = ws2812.new(5, 8)           -- 8 ليدات
local hue = 0
timer.every(30, function()
  for i = 1, 8 do strip:set(i, ws2812.hsv((hue + i * 30) % 360, 1, 0.3)) end
  strip:show()
  hue = hue + 5
end)
```

## حساس DHT22 (توقيت دقيق يُنفَّذ في الفيرموير)
```lua
--@pins 4
local dht = require("dht").new(4, "dht22")
timer.every(3000, function()
  local t, h = dht:read()
  if t then log(string.format("%.1f C  %.0f %%", t, h)) end
end)
```

## مستقبل أشعة تحت الحمراء (IR)
```lua
--@pins 3
--@needs rmt
ir.receive(3, function(proto, addr, cmd)
  log(proto, addr, cmd)
  if cmd == 0x45 then gpio.write(8, 1) end
end)
```

## قراءة GPS على UART
```lua
--@pins 20,21
--@needs uart
uart.begin(1, 9600, 20, 21)
uart.on_line(1, function(line)
  if line:sub(1, 6) == "$GPGGA" then log(line) end
end)
```
