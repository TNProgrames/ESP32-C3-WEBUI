-- BME280/BMP280: يطبع الحرارة والضغط والرطوبة كل 5 ثوانٍ.
local SDA, SCL = 8, 9
i2c.begin(SDA, SCL, 100000)
local s, err = drv.open("bme280", {})
if not s then print("BME280:", err) return end
timer.every(5000, function()
  local t, p, h = s:read()
  if not t then print("read:", p) return end
  print(string.format("%.2f C  %.1f hPa  %.1f %%", t / 100, p / 100, h / 100))
end)
on_stop(function() s:close() end)
