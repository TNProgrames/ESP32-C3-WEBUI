-- محطة طقس: sht31 (حرارة/رطوبة) + bh1750 (ضوء) + ssd1306 (اختياري). الأرجل: SDA=8, SCL=9
local SDA, SCL = 8, 9
i2c.begin(SDA, SCL, 400000)
local p = ui.panel("Weather")
p:page("ui/weather_station.html")
local devs, cfg = {}, {sht31 = {}, bh1750 = {}, ssd1306 = {}}
local function get(name)
  if devs[name] then return devs[name] end
  local d = drv.open(name, cfg[name])
  if d then devs[name] = d end
  return d
end
timer.every(2000, function()
  local out = {}
  local s = get("sht31")
  if s then
    local t, h = s:read()
    if t then out.t, out.h = t / 100, h / 100 end
  end
  local b = get("bh1750")
  if b then
    local lux = b:read()
    if lux then out.l = lux / 10 end
  end
  p:emit("w", out)
  p:emit("err", (s and b) and "" or (s and "bh1750 ✗" or "sht31 ✗"))
  local o = get("ssd1306")           -- الشاشة اختيارية
  if o and out.t then
    o:clear()
    o:text(0, 0, string.format("T %.1fC  H %d%%", out.t, math.floor(out.h)))
    o:text(0, 16, string.format("Lux %d", math.floor(out.l or 0)))
    o:bar(0, 40, 128, 10, math.min(100, math.floor(out.h)))
    o:show()
  end
end)
function on_stop()
  if devs.ssd1306 then devs.ssd1306:clear() devs.ssd1306:show() end
  for _, d in pairs(devs) do d:close() end
end
