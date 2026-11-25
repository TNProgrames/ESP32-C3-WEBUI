-- يتطلب درايفر ads1115. عدّل أرجل I2C هنا:
local SDA, SCL = 8, 9
i2c.begin(SDA, SCL, 400000)
local adc, err = drv.open("ads1115", {gain = 1})
if not adc then print("ADS1115:", err) return end
local p = ui.panel("ADS1115")
p:page("ui/adc_scope.html")
local ch = 0
p:on("ch", function(v) if v == 0 or v == 1 or v == 2 or v == 3 then ch = v end end)
p:on("g", function(v) if type(v) == "number" and v >= 0 and v <= 5 then adc:gain(math.floor(v)) end end)
timer.every(200, function()
  local mv = adc:read(ch)
  if not mv then p:emit("err", "read") return end
  p:emit("v", mv)
end)
on_stop(function() adc:close() end)
