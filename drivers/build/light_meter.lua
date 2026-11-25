-- يتطلب درايفر bh1750. عدّل أرجل I2C هنا:
local SDA, SCL = 8, 9
i2c.begin(SDA, SCL, 100000)
local s, err = drv.open("bh1750", {})
if not s then print("BH1750:", err) return end
local p = ui.panel("BH1750")
p:page("ui/light_meter.html")
timer.every(500, function()
  local lx = s:read()
  if not lx then p:emit("err", "read") return end
  p:emit("lux", lx / 10)
end)
on_stop(function() s:close() end)
