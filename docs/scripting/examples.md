# أمثلة

## 1) ليد يومض

```lua
--@name وميض
--@pins 8
gpio.mode(8, "out")
timer.every(500, function() gpio.toggle(8) end)
```

## 2) زر يشغّل ليد

```lua
--@name زر
--@pins 4,8
gpio.mode(8, "out")
gpio.mode(4, "in_pullup")
gpio.on_change(4, function(v)
  gpio.write(8, v == 0 and 1 or 0)   -- الزر مضغوط = 0
end)
```

## 3) حساس حرارة وعلبة في الواجهة

```lua
--@name حرارة
--@pins 0,5
local box = ui.panel("الحرارة")
box:label("t", "--")
box:slider("limit", "حد التنبيه", 20, 40, 30)

timer.every(2000, function()
  local t = adc.read_mv(0) / 10
  box:set("t", string.format("%.1f °C", t))
  pwm.set(5, t > box:get("limit") and 80 or 0)
end)
```

## 4) شاشة OLED

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

## 5) سكريبتان يتبادلان البيانات

```lua
-- حساس.lua
--@pins 0
timer.every(1000, function() bus.publish("temp", adc.read_mv(0) / 10) end)

-- عرض.lua
local box = ui.panel("العرض")
box:label("v", "--")
bus.subscribe("temp", function(v) box:set("v", string.format("%.1f", v)) end)
```
