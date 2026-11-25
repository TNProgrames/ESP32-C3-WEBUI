-- يتطلب درايفر bme280. عدّل أرجل I2C هنا:
local SDA, SCL = 8, 9
i2c.begin(SDA, SCL, 100000)
local s, err = drv.open("bme280", {})
if not s then print("BME280:", err) return end
local p = ui.panel("BME280")
p:page("ui/bme_monitor.html")
local function tick()
  local t, pr, h = s:read()
  if not t then p:emit("err", tostring(pr)) return end
  p:emit("t", t / 100)
  p:emit("p", pr / 100)
  p:emit("h", h / 100)
end
tick()
timer.every(3000, tick)
on_stop(function() s:close() end)
