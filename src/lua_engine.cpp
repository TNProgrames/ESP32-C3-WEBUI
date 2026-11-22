#include "lua_engine.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

// ---------- حدود ----------
#define MAX_SCRIPTS   3
#define MAX_TIMERS    8
#define NAME_LEN      24
#define MEM_LIMIT     (48 * 1024)
#define CALL_BUDGET   200      // ms كحد أقصى لكل استدعاء (حارس الحلقات اللانهائية)
#define MAX_CODE      8192
#define PWM_MAX       6
#define NUM_PINS      22

enum { ST_FREE = 0, ST_RUN = 1, ST_ERR = 2 };

struct LTimer { bool used; bool repeat; uint32_t period; uint32_t next; int ref; };

struct Script {
  uint8_t   state;
  char      name[NAME_LEN];
  lua_State* L;
  size_t    mem, limit;
  uint32_t  deadline;
  uint32_t  startMs;
  char      err[96];
  LTimer    timers[MAX_TIMERS];
};

static Script scripts[MAX_SCRIPTS];
static uint8_t pinOwner[NUM_PINS];     // 0 = حر، وإلا (فهرس السكريبت + 1)
static uint8_t pwmPins[PWM_MAX];       // رقم الدبوس + 1 (0 = فارغ)
static SemaphoreHandle_t statusMux;
static QueueHandle_t cmdQ;

enum { C_RUN, C_STOP, C_STOPALL };
struct Cmd { uint8_t type; char name[NAME_LEN]; char* code; size_t len; };

// ---------- أدوات ----------
bool luaeng_valid_name(const char* n) {
  if (!n || !*n) return false;
  size_t l = strlen(n);
  if (l >= NAME_LEN) return false;
  for (size_t i = 0; i < l; i++) {
    char c = n[i];
    if (!(isalnum((unsigned char)c) || c == '_' || c == '-')) return false;
  }
  return true;
}

static int idxOf(Script* s) { return (int)(s - scripts); }
static Script* findByName(const char* n) {
  for (auto& s : scripts) if (s.state != ST_FREE && strcmp(s.name, n) == 0) return &s;
  return nullptr;
}
static Script* me(lua_State* L) { void* ud; lua_getallocf(L, &ud); return (Script*)ud; }

static bool pinAllowed(int p) {
  // GPIO11-17 فلاش، 18-19 USB. المسموح: 0-10 و20 و21
  return (p >= 0 && p <= 10) || p == 20 || p == 21;
}

static int claimPin(lua_State* L, int p) {
  Script* s = me(L);
  if (!pinAllowed(p)) return luaL_error(L, "pin %d not allowed", p);
  int o = pinOwner[p];
  if (o && o != idxOf(s) + 1) return luaL_error(L, "pin %d owned by %s", p, scripts[o - 1].name);
  pinOwner[p] = idxOf(s) + 1;
  return 0;
}

// ---------- مخصص الذاكرة المحدود ----------
static void* limAlloc(void* ud, void* ptr, size_t osize, size_t nsize) {
  Script* s = (Script*)ud;
  if (!ptr) osize = 0;
  if (nsize == 0) {
    if (ptr) { s->mem -= osize; free(ptr); }
    return NULL;
  }
  if (nsize > osize && s->mem - osize + nsize > s->limit) return NULL;
  void* p = realloc(ptr, nsize);
  if (p) s->mem = s->mem - osize + nsize;
  return p;
}

static void hook(lua_State* L, lua_Debug*) {
  Script* s = me(L);
  if ((int32_t)(millis() - s->deadline) >= 0) luaL_error(L, "script too slow (>%d ms without yielding)", CALL_BUDGET);
}

// ---------- واجهة Lua ----------
static int l_print(lua_State* L) {
  Script* s = me(L);
  char line[140];
  int n = snprintf(line, sizeof(line), "[%s] ", s->name);
  int top = lua_gettop(L);
  for (int i = 1; i <= top && n < (int)sizeof(line) - 2; i++) {
    size_t l; const char* v = luaL_tolstring(L, i, &l);
    int w = snprintf(line + n, sizeof(line) - n, "%s%s", i > 1 ? "\t" : "", v);
    if (w > 0) n += w;
    lua_pop(L, 1);
  }
  if (n >= (int)sizeof(line) - 1) n = sizeof(line) - 2;
  line[n] = '\n'; line[n + 1] = 0;
  logPrintf("%s", line);
  return 0;
}

static int l_gpio_mode(lua_State* L) {
  int p = luaL_checkinteger(L, 1);
  const char* m = luaL_checkstring(L, 2);
  claimPin(L, p);
  if (!strcmp(m, "out")) pinMode(p, OUTPUT);
  else if (!strcmp(m, "in")) pinMode(p, INPUT);
  else if (!strcmp(m, "pullup")) pinMode(p, INPUT_PULLUP);
  else if (!strcmp(m, "pulldown")) pinMode(p, INPUT_PULLDOWN);
  else return luaL_error(L, "mode must be out|in|pullup|pulldown");
  return 0;
}
static int l_gpio_write(lua_State* L) {
  int p = luaL_checkinteger(L, 1);
  int v = lua_isboolean(L, 2) ? lua_toboolean(L, 2) : (int)luaL_checkinteger(L, 2);
  claimPin(L, p);
  digitalWrite(p, v ? HIGH : LOW);
  return 0;
}
static int l_gpio_read(lua_State* L) {
  int p = luaL_checkinteger(L, 1);
  claimPin(L, p);
  lua_pushinteger(L, digitalRead(p));
  return 1;
}
static int l_adc_read(lua_State* L) {
  int p = luaL_checkinteger(L, 1);
  if (p < 0 || p > 4) return luaL_error(L, "ADC only on GPIO0-4");
  claimPin(L, p);
  lua_pushinteger(L, analogRead(p));
  return 1;
}
static int l_adc_mv(lua_State* L) {
  int p = luaL_checkinteger(L, 1);
  if (p < 0 || p > 4) return luaL_error(L, "ADC only on GPIO0-4");
  claimPin(L, p);
  lua_pushinteger(L, analogReadMilliVolts(p));
  return 1;
}
// pwm.set(pin, freqHz, duty%)
static int l_pwm_set(lua_State* L) {
  int p = luaL_checkinteger(L, 1);
  int f = luaL_checkinteger(L, 2);
  lua_Number d = luaL_checknumber(L, 3);
  if (f < 1 || f > 40000) return luaL_error(L, "freq 1..40000");
  if (d < 0) d = 0; if (d > 100) d = 100;
  claimPin(L, p);
  int slot = -1;
  for (int i = 0; i < PWM_MAX; i++) if (pwmPins[i] == p + 1) slot = i;
  if (slot < 0) {
    for (int i = 0; i < PWM_MAX; i++) if (!pwmPins[i]) { slot = i; break; }
    if (slot < 0) return luaL_error(L, "no free PWM channel (max %d)", PWM_MAX);
    if (!ledcAttach(p, f, 10)) return luaL_error(L, "PWM attach failed");
    pwmPins[slot] = p + 1;
  } else ledcChangeFrequency(p, f, 10);
  ledcWrite(p, (uint32_t)(d * 1023.0f / 100.0f));
  return 0;
}
static int l_sys_millis(lua_State* L) { lua_pushinteger(L, (lua_Integer)millis()); return 1; }
static int l_sys_heap(lua_State* L) { lua_pushinteger(L, ESP.getFreeHeap()); return 1; }

static int addTimer(lua_State* L, bool repeat) {
  Script* s = me(L);
  lua_Integer ms = luaL_checkinteger(L, 1);
  luaL_checktype(L, 2, LUA_TFUNCTION);
  if (ms < (repeat ? 10 : 0) || ms > 86400000) return luaL_error(L, "bad interval");
  for (int i = 0; i < MAX_TIMERS; i++) if (!s->timers[i].used) {
    lua_pushvalue(L, 2);
    s->timers[i] = { true, repeat, (uint32_t)ms, millis() + (uint32_t)ms, luaL_ref(L, LUA_REGISTRYINDEX) };
    lua_pushinteger(L, i + 1);
    return 1;
  }
  return luaL_error(L, "too many timers (max %d)", MAX_TIMERS);
}
static int l_timer_every(lua_State* L) { return addTimer(L, true); }
static int l_timer_after(lua_State* L) { return addTimer(L, false); }
static void freeTimer(lua_State* L, LTimer& t) {
  if (t.used) { luaL_unref(L, LUA_REGISTRYINDEX, t.ref); t.used = false; }
}
static int l_timer_cancel(lua_State* L) {
  Script* s = me(L);
  int id = luaL_checkinteger(L, 1);
  if (id >= 1 && id <= MAX_TIMERS) freeTimer(L, s->timers[id - 1]);
  return 0;
}

static void reg(lua_State* L, const char* tbl, const luaL_Reg* fns) {
  lua_newtable(L);
  luaL_setfuncs(L, fns, 0);
  lua_setglobal(L, tbl);
}


// ---------- لوحات الواجهة (UI panels) ----------
#define MAX_PANELS        6
#define PANELS_PER_SCRIPT 4
#define MAX_WIDGETS       40
#define MAX_MSGS          48
#define ID_LEN            16
#define VAL_LEN           48

enum { W_LABEL, W_BUTTON, W_SWITCH, W_SLIDER, W_PROGRESS, W_INPUT, W_COUNT };
static const char* WTYPES[W_COUNT] = { "label", "button", "switch", "slider", "progress", "input" };

struct Panel  { bool used; uint8_t owner; char title[24]; char page[40]; };
struct Widget { bool used; uint8_t panel; uint8_t type; char id[ID_LEN]; char label[24]; float mn, mx; char val[VAL_LEN]; };
struct Msg    { uint32_t seq; uint8_t panel; char name[ID_LEN]; char val[VAL_LEN]; };
struct UiEv   { uint8_t panel; char name[ID_LEN]; char val[VAL_LEN]; };

static Panel  panels[MAX_PANELS];
static Widget widgets[MAX_WIDGETS];
static Msg    msgs[MAX_MSGS];
static uint32_t msgSeq = 0, uiVer = 1;
static SemaphoreHandle_t uiMux;
static QueueHandle_t evQ;

static bool safeName(const char* n, size_t max) {
  size_t l = strlen(n);
  if (!l || l >= max) return false;
  for (size_t i = 0; i < l; i++) {
    char c = n[i];
    if (!(isalnum((unsigned char)c) || c == '_' || c == '-')) return false;
  }
  return true;
}

// تحويل قيمة Lua إلى نص JSON (مقتطع ليتسع في cap)
static void encodeVal(lua_State* L, int idx, char* out, size_t cap) {
  switch (lua_type(L, idx)) {
    case LUA_TBOOLEAN: snprintf(out, cap, "%s", lua_toboolean(L, idx) ? "true" : "false"); break;
    case LUA_TNUMBER:
      if (lua_isinteger(L, idx)) snprintf(out, cap, "%lld", (long long)lua_tointeger(L, idx));
      else {
        double d = (double)lua_tonumber(L, idx);
        if (d != d || d > 3e38 || d < -3e38) snprintf(out, cap, "null");
        else snprintf(out, cap, "%.6g", d);
      }
      break;
    case LUA_TSTRING: {
      size_t l; const char* str = lua_tolstring(L, idx, &l);
      size_t n = 0; out[n++] = '"';
      bool cut = false; size_t i = 0;
      for (; i < l; i++) {
        unsigned char c = (unsigned char)str[i];
        char esc[2]; int el;
        if (c == '"' || c == '\\') { esc[0] = '\\'; esc[1] = (char)c; el = 2; }
        else if (c < 0x20) { esc[0] = ' '; el = 1; }
        else { esc[0] = (char)c; el = 1; }
        if (n + el > cap - 2) { cut = true; break; }
        for (int k = 0; k < el; k++) out[n++] = esc[k];
      }
      if (cut && (((unsigned char)str[i]) & 0xC0) == 0x80) {   // لا نقطع حرف UTF-8 في المنتصف
        while (n > 1 && (((unsigned char)out[n - 1]) & 0xC0) == 0x80) n--;
        if (n > 1 && (((unsigned char)out[n - 1]) & 0xC0) == 0xC0) n--;
      }
      out[n++] = '"'; out[n] = 0;
      break;
    }
    default: snprintf(out, cap, "null");
  }
}

static void pushMsg(uint8_t panel, const char* name, const char* val) {
  xSemaphoreTake(uiMux, portMAX_DELAY);
  int slot = -1; uint32_t oldest = 0xFFFFFFFF;
  for (int i = 0; i < MAX_MSGS; i++) {
    if (msgs[i].seq && msgs[i].panel == panel && !strcmp(msgs[i].name, name)) msgs[i].seq = 0;   // القيمة الأحدث تحل محل القديمة
  }
  for (int i = 0; i < MAX_MSGS; i++) {
    if (msgs[i].seq == 0) { slot = i; break; }
    if (msgs[i].seq < oldest) { oldest = msgs[i].seq; slot = i; }
  }
  Msg& m = msgs[slot];
  m.seq = ++msgSeq; m.panel = panel;
  strncpy(m.name, name, ID_LEN - 1); m.name[ID_LEN - 1] = 0;
  strncpy(m.val, val, VAL_LEN - 1); m.val[VAL_LEN - 1] = 0;
  xSemaphoreGive(uiMux);
}

static void uiDrop(int slot) {
  xSemaphoreTake(uiMux, portMAX_DELAY);
  for (int i = 0; i < MAX_PANELS; i++) if (panels[i].used && panels[i].owner == slot) {
    panels[i].used = false;
    for (auto& w : widgets) if (w.used && w.panel == i) w.used = false;
    for (auto& m : msgs) if (m.seq && m.panel == i) m.seq = 0;
  }
  uiVer++;
  xSemaphoreGive(uiMux);
}

static int ownedPanel(lua_State* L, int arg) {
  int i = (int)luaL_checkinteger(L, arg);
  if (i < 0 || i >= MAX_PANELS || !panels[i].used || panels[i].owner != idxOf(me(L))) luaL_error(L, "invalid panel");
  return i;
}

static int l_ui_new(lua_State* L) {
  Script* s = me(L);
  const char* title = luaL_checkstring(L, 1);
  int mine = 0, slot = -1;
  for (int i = 0; i < MAX_PANELS; i++) {
    if (panels[i].used && panels[i].owner == idxOf(s)) mine++;
    if (!panels[i].used && slot < 0) slot = i;
  }
  if (mine >= PANELS_PER_SCRIPT) return luaL_error(L, "too many panels (max %d per script)", PANELS_PER_SCRIPT);
  if (slot < 0) return luaL_error(L, "no free panel (max %d total)", MAX_PANELS);
  xSemaphoreTake(uiMux, portMAX_DELAY);
  panels[slot].used = true; panels[slot].owner = idxOf(s);
  strncpy(panels[slot].title, title, sizeof(panels[slot].title) - 1); panels[slot].title[sizeof(panels[slot].title) - 1] = 0;
  panels[slot].page[0] = 0;
  uiVer++;
  xSemaphoreGive(uiMux);
  lua_pushinteger(L, slot);
  return 1;
}

// ui._w(panel, type, id, label, min, max, step, value)
static int l_ui_w(lua_State* L) {
  int pi = ownedPanel(L, 1);
  const char* ty = luaL_checkstring(L, 2);
  const char* id = luaL_checkstring(L, 3);
  const char* label = luaL_optstring(L, 4, "");
  float mn = (float)luaL_optnumber(L, 5, 0), mx = (float)luaL_optnumber(L, 6, 100);
  int type = -1;
  for (int i = 0; i < W_COUNT; i++) if (!strcmp(ty, WTYPES[i])) type = i;
  if (type < 0) return luaL_error(L, "unknown widget type");
  if (!safeName(id, ID_LEN)) return luaL_error(L, "bad widget id (letters, digits, _ -; max %d)", ID_LEN - 1);
  char val[VAL_LEN]; encodeVal(L, 8, val, sizeof(val));
  int slot = -1, freeSlot = -1;
  for (int i = 0; i < MAX_WIDGETS; i++) {
    if (widgets[i].used && widgets[i].panel == pi && !strcmp(widgets[i].id, id)) slot = i;
    if (!widgets[i].used && freeSlot < 0) freeSlot = i;
  }
  if (slot < 0) slot = freeSlot;
  if (slot < 0) return luaL_error(L, "too many widgets (max %d)", MAX_WIDGETS);
  xSemaphoreTake(uiMux, portMAX_DELAY);
  Widget& w = widgets[slot];
  w.used = true; w.panel = pi; w.type = type; w.mn = mn; w.mx = mx;
  strncpy(w.id, id, ID_LEN - 1); w.id[ID_LEN - 1] = 0;
  strncpy(w.label, label, sizeof(w.label) - 1); w.label[sizeof(w.label) - 1] = 0;
  strcpy(w.val, val);
  uiVer++;
  xSemaphoreGive(uiMux);
  return 0;
}

// ui._set(panel, id, value): يحدّث أداة (إن وُجدت) ويبثّ القيمة للواجهة
static int l_ui_set(lua_State* L) {
  int pi = ownedPanel(L, 1);
  const char* id = luaL_checkstring(L, 2);
  if (!safeName(id, ID_LEN)) return luaL_error(L, "bad id");
  char val[VAL_LEN]; encodeVal(L, 3, val, sizeof(val));
  xSemaphoreTake(uiMux, portMAX_DELAY);
  for (auto& w : widgets) if (w.used && w.panel == pi && !strcmp(w.id, id)) strcpy(w.val, val);
  xSemaphoreGive(uiMux);
  pushMsg(pi, id, val);
  return 0;
}

static int l_ui_emit(lua_State* L) {
  int pi = ownedPanel(L, 1);
  const char* n = luaL_checkstring(L, 2);
  if (!safeName(n, ID_LEN)) return luaL_error(L, "bad event name (letters, digits, _ -; max %d)", ID_LEN - 1);
  char val[VAL_LEN]; encodeVal(L, 3, val, sizeof(val));
  pushMsg(pi, n, val);
  return 0;
}

// ui._page(panel, path): الملف يجب أن يكون تحت /ui/ وبامتداد .html
static int l_ui_page(lua_State* L) {
  int pi = ownedPanel(L, 1);
  const char* path = luaL_checkstring(L, 2);
  if (path[0] == '/') path++;
  size_t l = strlen(path);
  bool ok = l > 8 && l < 38 && !strncmp(path, "ui/", 3) && (!strcmp(path + l - 5, ".html"));
  for (size_t i = 0; ok && i < l; i++) {
    char c = path[i];
    if (!(isalnum((unsigned char)c) || c == '_' || c == '-' || c == '.' || c == '/')) ok = false;
  }
  if (ok && strstr(path, "..")) ok = false;
  if (!ok) return luaL_error(L, "page must be ui/<name>.html");
  xSemaphoreTake(uiMux, portMAX_DELAY);
  snprintf(panels[pi].page, sizeof(panels[pi].page), "/%s", path);
  uiVer++;
  xSemaphoreGive(uiMux);
  return 0;
}

// طبقة Lua فوق الدوال الأساسية: كائن اللوحة والأدوات والأحداث
static const char UI_PRELUDE[] = R"LUA(
local U = {}
U.__index = U
local H = {}
local f_new, f_w, f_set, f_page, f_emit = ui._new, ui._w, ui._set, ui._page, ui._emit
ui._new, ui._w, ui._set, ui._page, ui._emit = nil, nil, nil, nil, nil
function ui.panel(title)
  local i = f_new(tostring(title or "panel"))
  H[i] = {}
  return setmetatable({ i = i }, U)
end
function U:page(path) f_page(self.i, path) return self end
function U:emit(n, v) f_emit(self.i, n, v) return self end
function U:on(n, fn) H[self.i][n] = fn return self end
function U:set(id, v) f_set(self.i, id, v) return self end
function U:label(id, text) f_w(self.i, "label", id, "", 0, 0, 0, text or "") return self end
function U:button(id, text, fn) f_w(self.i, "button", id, text or id, 0, 0, 0, nil) if fn then self:on(id, fn) end return self end
function U:switch(id, text, fn, init) f_w(self.i, "switch", id, text or id, 0, 0, 0, init and true or false) if fn then self:on(id, fn) end return self end
function U:slider(id, text, min, max, fn, init) f_w(self.i, "slider", id, text or id, min or 0, max or 100, 0, init or min or 0) if fn then self:on(id, fn) end return self end
function U:progress(id, text, v) f_w(self.i, "progress", id, text or id, 0, 100, 0, v or 0) return self end
function U:input(id, text, fn, init) f_w(self.i, "input", id, text or id, 0, 0, 0, init or "") if fn then self:on(id, fn) end return self end
function __ui_dispatch(i, n, v)
  local h = H[i] and H[i][n]
  if h then h(v) end
end
)LUA";

static void pushJsonValue(lua_State* L, const char* json) {
  JsonDocument d;
  if (deserializeJson(d, json)) { lua_pushnil(L); return; }
  JsonVariantConst v = d.as<JsonVariantConst>();
  if (v.is<bool>()) lua_pushboolean(L, v.as<bool>());
  else if (v.is<long>()) lua_pushinteger(L, v.as<long>());
  else if (v.is<float>()) lua_pushnumber(L, v.as<float>());
  else if (v.is<const char*>()) lua_pushstring(L, v.as<const char*>());
  else lua_pushnil(L);
}

// ---------- دورة حياة السكريبت ----------
static void setStatus(Script* s, uint8_t st, const char* err) {
  xSemaphoreTake(statusMux, portMAX_DELAY);
  s->state = st;
  if (err) { strncpy(s->err, err, sizeof(s->err) - 1); s->err[sizeof(s->err) - 1] = 0; }
  xSemaphoreGive(statusMux);
}

static void releaseResources(Script* s) {
  int id = idxOf(s) + 1;
  for (int p = 0; p < NUM_PINS; p++) if (pinOwner[p] == id) {
    for (int i = 0; i < PWM_MAX; i++) if (pwmPins[i] == p + 1) { ledcDetach(p); pwmPins[i] = 0; }
    pinMode(p, INPUT);
    pinOwner[p] = 0;
  }
  uiDrop(idxOf(s));
  if (s->L) { lua_close(s->L); s->L = nullptr; }
  s->mem = 0;
  memset(s->timers, 0, sizeof(s->timers));
}

static void failScript(Script* s, const char* msg) {
  logPrintf("[%s] ERROR: %s\n", s->name, msg);
  releaseResources(s);
  setStatus(s, ST_ERR, msg);
}

static void killScript(Script* s) {
  releaseResources(s);
  xSemaphoreTake(statusMux, portMAX_DELAY);
  s->state = ST_FREE; s->name[0] = 0; s->err[0] = 0;
  xSemaphoreGive(statusMux);
}

// استدعاء Lua محمي مع ميزانية زمنية
static bool protectedCall(Script* s, int nargs) {
  lua_State* L = s->L;
  s->deadline = millis() + CALL_BUDGET;
  if (lua_pcall(L, nargs, 0, 0) != LUA_OK) {
    const char* m = lua_tostring(L, -1);
    failScript(s, m ? m : "error");
    return false;
  }
  return true;
}

static void startScript(const char* name, const char* code, size_t len) {
  Script* s = findByName(name);
  if (s) killScript(s);
  else for (auto& x : scripts) if (x.state == ST_FREE) { s = &x; break; }
  if (!s) { logPrintf("[%s] ERROR: max %d scripts\n", name, MAX_SCRIPTS); return; }
  memset(s, 0, sizeof(*s));
  strncpy(s->name, name, NAME_LEN - 1);
  s->limit = MEM_LIMIT;
  s->startMs = millis();
  s->state = ST_RUN;
  s->L = lua_newstate(limAlloc, s);
  if (!s->L) { failScript(s, "out of memory"); return; }
  lua_State* L = s->L;
  luaL_requiref(L, "_G", luaopen_base, 1); lua_pop(L, 1);
  luaL_requiref(L, "table", luaopen_table, 1); lua_pop(L, 1);
  luaL_requiref(L, "string", luaopen_string, 1); lua_pop(L, 1);
  luaL_requiref(L, "math", luaopen_math, 1); lua_pop(L, 1);
  luaL_requiref(L, "utf8", luaopen_utf8, 1); lua_pop(L, 1);
  luaL_requiref(L, "coroutine", luaopen_coroutine, 1); lua_pop(L, 1);
  // عزل: إزالة ما يقرأ ملفات أو يحمّل شيفرة ثنائية
  for (const char* g : { "dofile", "loadfile", "load", "loadstring", "require" }) { lua_pushnil(L); lua_setglobal(L, g); }
  lua_getglobal(L, "string"); lua_pushnil(L); lua_setfield(L, -2, "dump"); lua_pop(L, 1);
  lua_pushcfunction(L, l_print); lua_setglobal(L, "print");

  static const luaL_Reg gpioF[] = { {"mode", l_gpio_mode}, {"write", l_gpio_write}, {"read", l_gpio_read}, {NULL, NULL} };
  static const luaL_Reg adcF[]  = { {"read", l_adc_read}, {"mv", l_adc_mv}, {NULL, NULL} };
  static const luaL_Reg pwmF[]  = { {"set", l_pwm_set}, {NULL, NULL} };
  static const luaL_Reg timF[]  = { {"every", l_timer_every}, {"after", l_timer_after}, {"cancel", l_timer_cancel}, {NULL, NULL} };
  static const luaL_Reg sysF[]  = { {"millis", l_sys_millis}, {"heap", l_sys_heap}, {NULL, NULL} };
  static const luaL_Reg uiF[]   = { {"_new", l_ui_new}, {"_w", l_ui_w}, {"_set", l_ui_set}, {"_page", l_ui_page}, {"_emit", l_ui_emit}, {NULL, NULL} };
  reg(L, "gpio", gpioF); reg(L, "adc", adcF); reg(L, "pwm", pwmF); reg(L, "timer", timF); reg(L, "sys", sysF); reg(L, "ui", uiF);
  if (luaL_loadbufferx(L, UI_PRELUDE, sizeof(UI_PRELUDE) - 1, "=ui", "t") != LUA_OK || !protectedCall(s, 0)) {
    if (s->state == ST_RUN) { const char* m = lua_tostring(L, -1); failScript(s, m ? m : "ui init failed"); }
    return;
  }

  lua_sethook(L, hook, LUA_MASKCOUNT, 5000);
  s->deadline = millis() + CALL_BUDGET;
  if (luaL_loadbufferx(L, code, len, name, "t") != LUA_OK) {
    const char* m = lua_tostring(L, -1);
    failScript(s, m ? m : "syntax error");
    return;
  }
  if (protectedCall(s, 0)) logPrintf("[%s] started (%u bytes RAM)\n", name, (unsigned)s->mem);
}

static void tickScript(Script* s) {
  uint32_t now = millis();
  for (int i = 0; i < MAX_TIMERS && s->state == ST_RUN; i++) {
    LTimer& t = s->timers[i];
    if (!t.used || (int32_t)(now - t.next) < 0) continue;
    lua_rawgeti(s->L, LUA_REGISTRYINDEX, t.ref);
    if (t.repeat) t.next = now + t.period;
    else freeTimer(s->L, t);   // المرجع يبقى على المكدس
    if (!protectedCall(s, 0)) return;
  }
}

static void handleEvent(const UiEv& e) {
  if (e.panel >= MAX_PANELS || !panels[e.panel].used) return;
  Script* s = &scripts[panels[e.panel].owner];
  if (s->state != ST_RUN || !s->L) return;
  xSemaphoreTake(uiMux, portMAX_DELAY);
  for (auto& w : widgets) if (w.used && w.panel == e.panel && !strcmp(w.id, e.name) &&
      (w.type == W_SWITCH || w.type == W_SLIDER || w.type == W_INPUT)) strcpy(w.val, e.val);
  xSemaphoreGive(uiMux);
  lua_State* L = s->L;
  lua_getglobal(L, "__ui_dispatch");
  if (!lua_isfunction(L, -1)) { lua_pop(L, 1); return; }
  lua_pushinteger(L, e.panel);
  lua_pushstring(L, e.name);
  pushJsonValue(L, e.val);
  protectedCall(s, 3);
}

static void engineTask(void*) {
  Cmd c;
  for (;;) {
    if (xQueueReceive(cmdQ, &c, pdMS_TO_TICKS(10)) == pdTRUE) {
      if (c.type == C_RUN) { startScript(c.name, c.code, c.len); free(c.code); }
      else if (c.type == C_STOP) {
        Script* s = findByName(c.name);
        if (s) { killScript(s); logPrintf("[%s] stopped\n", c.name); }
      } else if (c.type == C_STOPALL) {
        for (auto& s : scripts) if (s.state != ST_FREE) killScript(&s);
      }
    }
    UiEv ev;
    while (xQueueReceive(evQ, &ev, 0) == pdTRUE) handleEvent(ev);
    for (auto& s : scripts) if (s.state == ST_RUN) tickScript(&s);
    // تحديث حالة الذاكرة للعرض
  }
}

// ---------- الواجهة العامة ----------
void luaeng_begin() {
  statusMux = xSemaphoreCreateMutex();
  uiMux = xSemaphoreCreateMutex();
  cmdQ = xQueueCreate(4, sizeof(Cmd));
  evQ = xQueueCreate(8, sizeof(UiEv));
  xTaskCreate(engineTask, "lua", 16384, NULL, 2, NULL);
}

bool luaeng_run(const char* name, const char* code, size_t len) {
  if (!luaeng_valid_name(name) || len == 0 || len > MAX_CODE) return false;
  Cmd c = {}; c.type = C_RUN; strncpy(c.name, name, NAME_LEN - 1);
  c.code = (char*)malloc(len + 1);
  if (!c.code) return false;
  memcpy(c.code, code, len); c.code[len] = 0; c.len = len;
  if (xQueueSend(cmdQ, &c, 0) != pdTRUE) { free(c.code); return false; }
  return true;
}

bool luaeng_run_file(const char* name) {
  if (!luaeng_valid_name(name)) return false;
  String p = String("/scripts/") + name + ".lua";
  File f = LittleFS.open(p, "r");
  if (!f || f.isDirectory() || f.size() == 0 || f.size() > MAX_CODE) return false;
  size_t n = f.size();
  char* buf = (char*)malloc(n + 1);
  if (!buf) return false;
  f.read((uint8_t*)buf, n);
  bool ok = luaeng_run(name, buf, n);
  free(buf);
  return ok;
}

bool luaeng_stop(const char* name) {
  if (!luaeng_valid_name(name)) return false;
  Cmd c = {}; c.type = C_STOP; strncpy(c.name, name, NAME_LEN - 1);
  return xQueueSend(cmdQ, &c, 0) == pdTRUE;
}

void luaeng_stop_all() { Cmd c = {}; c.type = C_STOPALL; xQueueSend(cmdQ, &c, 0); }

String luaeng_list_text() {
  String o;
  xSemaphoreTake(statusMux, portMAX_DELAY);
  for (auto& s : scripts) if (s.state != ST_FREE) {
    o += String(s.name) + "  " + (s.state == ST_RUN ? "running" : "error") + "  " + String((unsigned)s.mem) + "B";
    if (s.state == ST_ERR) o += "  " + String(s.err);
    o += "\n";
  }
  xSemaphoreGive(statusMux);
  return o.length() ? o : String("(no scripts)");
}

String luaeng_list_json() {
  JsonDocument d;
  JsonArray a = d.to<JsonArray>();
  xSemaphoreTake(statusMux, portMAX_DELAY);
  for (auto& s : scripts) if (s.state != ST_FREE) {
    JsonObject o = a.add<JsonObject>();
    o["name"] = s.name;
    o["state"] = s.state == ST_RUN ? "running" : "error";
    o["mem"] = (uint32_t)s.mem;
    o["up"] = (millis() - s.startMs) / 1000;
    if (s.state == ST_ERR) o["err"] = s.err;
  }
  xSemaphoreGive(statusMux);
  String out; serializeJson(d, out);
  return out;
}

// ---------- واجهة HTTP للوحات ----------
String luaeng_ui_panels_json() {
  JsonDocument d;
  JsonObject root = d.to<JsonObject>();
  xSemaphoreTake(uiMux, portMAX_DELAY);
  root["ver"] = uiVer;
  JsonArray ps = root["panels"].to<JsonArray>();
  for (int i = 0; i < MAX_PANELS; i++) if (panels[i].used) {
    JsonObject po = ps.add<JsonObject>();
    po["id"] = i;
    po["script"] = scripts[panels[i].owner].name;
    po["title"] = panels[i].title;
    if (panels[i].page[0]) po["page"] = panels[i].page;
    JsonArray ws = po["widgets"].to<JsonArray>();
    for (auto& w : widgets) if (w.used && w.panel == i) {
      JsonObject wo = ws.add<JsonObject>();
      wo["id"] = w.id;
      wo["type"] = WTYPES[w.type];
      wo["label"] = w.label;
      wo["min"] = w.mn;
      wo["max"] = w.mx;
      wo["value"] = serialized(w.val);
    }
  }
  String out; serializeJson(d, out);
  xSemaphoreGive(uiMux);
  return out;
}

String luaeng_ui_poll_json(uint32_t since) {
  int idx[MAX_MSGS]; int n = 0;
  String o;
  xSemaphoreTake(uiMux, portMAX_DELAY);
  for (int i = 0; i < MAX_MSGS; i++) if (msgs[i].seq > since) {
    int k = n++;
    while (k > 0 && msgs[idx[k - 1]].seq > msgs[i].seq) { idx[k] = idx[k - 1]; k--; }
    idx[k] = i;
  }
  o.reserve(64 + n * 80);
  o = String("{\"seq\":") + String((unsigned)msgSeq) + ",\"ver\":" + String((unsigned)uiVer) + ",\"msgs\":[";
  for (int k = 0; k < n; k++) {
    char b[110];
    snprintf(b, sizeof(b), "%s{\"p\":%u,\"n\":\"%s\",\"v\":%s}", k ? "," : "", (unsigned)msgs[idx[k]].panel, msgs[idx[k]].name, msgs[idx[k]].val);
    o += b;
  }
  o += "]}";
  xSemaphoreGive(uiMux);
  return o;
}

bool luaeng_ui_event(int panel, const char* name, const char* valJson) {
  if (panel < 0 || panel >= MAX_PANELS || !safeName(name, ID_LEN) || strlen(valJson) >= VAL_LEN) return false;
  UiEv e = {}; e.panel = panel;
  strncpy(e.name, name, ID_LEN - 1); strncpy(e.val, valJson, VAL_LEN - 1);
  return xQueueSend(evQ, &e, 0) == pdTRUE;
}
