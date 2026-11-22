// أمثلة المختبر. كل مثال: سكريبت Lua، وصفحة HTML اختيارية تُحفظ على الجهاز قبل التشغيل.
const GAUGE_PAGE = `<!doctype html>
<meta charset="utf-8">
<style>
  :root { --fg:#eee; --mut:#999; --track:#333; }
  [data-theme="light"] { --fg:#111; --mut:#666; --track:#ddd; }
  body { margin:0; padding:8px; font-family:system-ui,sans-serif; text-align:center; color:var(--fg); }
  svg { width:100%; max-width:260px; }
  #n { font-size:34px; font-weight:bold; margin-top:-46px; }
  .ok { color:#10b981 } .hot { color:#ef4444 }
  input { width:80%; }
</style>
<svg viewBox="0 0 200 110">
  <path d="M20 100 A80 80 0 0 1 180 100" fill="none" stroke="var(--track)" stroke-width="16" stroke-linecap="round"/>
  <path id="arc" d="M20 100 A80 80 0 0 1 180 100" fill="none" stroke="#10b981" stroke-width="16" stroke-linecap="round" pathLength="100" stroke-dasharray="0 100"/>
</svg>
<div id="n">--</div>
<div style="color:var(--mut);font-size:13px">setpoint: <b id="sp">30</b> °C</div>
<input id="s" type="range" min="15" max="40" value="30">
<script>
  var sp = 30;
  esp.on("temp", function (v) {
    var pct = Math.max(0, Math.min(100, (v - 15) / 25 * 100));
    arc.setAttribute("stroke-dasharray", pct + " 100");
    n.textContent = v.toFixed(1) + "°";
    n.className = v > sp ? "hot" : "ok";
    arc.setAttribute("stroke", v > sp ? "#ef4444" : "#10b981");
  });
  esp.on("setpoint", function (v) { sp = v; s.value = v; document.getElementById("sp").textContent = v; });
  s.oninput = function () { document.getElementById("sp").textContent = s.value; };
  s.onchange = function () { sp = +s.value; esp.send("setpoint", sp); };
<\/script>`;

const CHART_PAGE = `<!doctype html>
<meta charset="utf-8">
<style>
  :root { --fg:#eee; --grid:#333; }
  [data-theme="light"] { --fg:#111; --grid:#ddd; }
  body { margin:0; padding:6px; font-family:system-ui,sans-serif; color:var(--fg); }
  canvas { width:100%; height:150px; display:block; }
  button { padding:6px 12px; border-radius:8px; border:0; background:#6366f1; color:#fff; font-weight:bold; }
</style>
<canvas id="c" width="400" height="150"></canvas>
<div style="display:flex;justify-content:space-between;align-items:center;margin-top:6px">
  <span>value: <b id="v">--</b></span><button id="b">pause</button>
</div>
<script>
  var data = [], paused = false, ctx = c.getContext("2d");
  function draw() {
    var w = c.width, h = c.height, css = getComputedStyle(document.documentElement);
    ctx.clearRect(0, 0, w, h);
    ctx.strokeStyle = esp.theme === "light" ? "#ddd" : "#333";
    for (var i = 1; i < 4; i++) { ctx.beginPath(); ctx.moveTo(0, h * i / 4); ctx.lineTo(w, h * i / 4); ctx.stroke(); }
    ctx.strokeStyle = "#6366f1"; ctx.lineWidth = 2; ctx.beginPath();
    data.forEach(function (y, i) { var x = i * w / 59, yy = h - y / 100 * h; i ? ctx.lineTo(x, yy) : ctx.moveTo(x, yy); });
    ctx.stroke();
  }
  esp.on("sample", function (v) {
    v_.textContent = v.toFixed(0);
    if (paused) return;
    data.push(v); if (data.length > 60) data.shift(); draw();
  });
  var v_ = document.getElementById("v");
  b.onclick = function () { paused = !paused; b.textContent = paused ? "resume" : "pause"; esp.send("paused", paused); };
  esp.onchange = draw;
<\/script>`;

export const examples = {
  'Hello': { code: `print("hello from Lua", 1 + 2, 10 / 4)\nprint("heap", sys.heap())` },
  'Blink (GPIO8)': { code: `-- يومّض LED على GPIO8 كل 500ms\ngpio.mode(8, "out")\nlocal on = false\ntimer.every(500, function()\n  on = not on\n  gpio.write(8, on)\n  print("led", on)\nend)` },
  'Timer': { code: `local n = 0\nlocal id\nid = timer.every(1000, function()\n  n = n + 1\n  print("tick", n)\n  if n == 5 then timer.cancel(id) print("done") end\nend)` },
  'ADC (GPIO0)': { code: `timer.every(1000, function()\n  print("adc", adc.read(0), adc.mv(0) .. " mV")\nend)` },
  'PWM (GPIO10)': { code: `local d = 0\ntimer.every(50, function()\n  d = (d + 5) % 105\n  pwm.set(10, 1000, d)\nend)` },
  'Panel: أدوات جاهزة': { code:
`-- لوحة بأدوات جاهزة: تظهر في تبويب اللوحات
local p = ui.panel("Control")
gpio.mode(8, "out")
local count = 0

p:label("st", "LED: OFF")
p:switch("led", "LED (GPIO8)", function(on)
  gpio.write(8, on)
  p:set("st", on and "LED: ON" or "LED: OFF")
end)
p:slider("br", "Brightness (GPIO10 PWM)", 0, 100, function(v)
  pwm.set(10, 1000, v)
end, 0)
p:button("inc", "Count +1", function()
  count = count + 1
  p:set("cnt", "pressed " .. count .. " times")
end)
p:label("cnt", "pressed 0 times")
p:progress("up", "Uptime (s mod 100)", 0)
p:input("msg", "Message", function(s) print("msg:", s) end, "hi")

timer.every(1000, function()
  p:set("up", (sys.millis() // 1000) % 100)
end)` },
  'Page: عداد حرارة (iframe)': { code:
`-- صفحة HTML مخصصة داخل إطار معزول
local p = ui.panel("Gauge")
p:page("ui/gauge.html")
local setpoint = 30
p:on("setpoint", function(v)
  setpoint = v
  print("setpoint =", v)
end)
local t = 0
timer.every(500, function()
  t = t + 0.2
  local temp = 25 + 8 * math.sin(t)   -- حرارة تجريبية
  p:emit("temp", temp)
end)`, page: { path: '/ui/gauge.html', html: GAUGE_PAGE } },
  'Page: رسم بياني (canvas)': { code:
`local p = ui.panel("Chart")
p:page("ui/chart.html")
local paused = false
p:on("paused", function(v) paused = v print("paused", v) end)
local x = 0
timer.every(200, function()
  x = x + 0.15
  p:emit("sample", 50 + 40 * math.sin(x) * math.cos(x / 3))
end)`, page: { path: '/ui/chart.html', html: CHART_PAGE } },
  'Error test (حلقة لا نهائية)': { code: `-- يجب أن يوقفها الحارس بعد 200ms\nwhile true do end` }
};
