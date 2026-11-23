// fb: إطار نقاط 1 بت (بصيغة صفحات SSD1306) للرسم على الشاشات الصغيرة، يعيش داخل ذاكرة Lua (1KB لـ128x64)
// لا يعتمد إلا على Lua، فيمكن اختباره على الحاسوب.
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
extern "C" {
#include "lua.h"
#include "lauxlib.h"
}
#include "lua_fb.h"

struct FB { uint8_t w, h; };   // يتبعه (w*h/8) بايت: الفهرس = (y>>3)*w + x، والبت = y&7 (الأدنى = الأعلى)
static inline uint8_t* bufOf(FB* f) { return (uint8_t*)(f + 1); }
#define MT "esp.fb"

// خط 5x7 (ASCII 32..126) ثم رمز الدرجة °. كل حرف 5 أعمدة، البت 0 = أعلى الحرف
static const uint8_t FONT[96 * 5] = {
  0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x5F,0x00,0x00, 0x00,0x07,0x00,0x07,0x00, 0x14,0x7F,0x14,0x7F,0x14,
  0x24,0x2A,0x7F,0x2A,0x12, 0x23,0x13,0x08,0x64,0x62, 0x36,0x49,0x55,0x22,0x50, 0x00,0x05,0x03,0x00,0x00,
  0x00,0x1C,0x22,0x41,0x00, 0x00,0x41,0x22,0x1C,0x00, 0x14,0x08,0x3E,0x08,0x14, 0x08,0x08,0x3E,0x08,0x08,
  0x00,0x50,0x30,0x00,0x00, 0x08,0x08,0x08,0x08,0x08, 0x00,0x60,0x60,0x00,0x00, 0x20,0x10,0x08,0x04,0x02,
  0x3E,0x51,0x49,0x45,0x3E, 0x00,0x42,0x7F,0x40,0x00, 0x42,0x61,0x51,0x49,0x46, 0x21,0x41,0x45,0x4B,0x31,
  0x18,0x14,0x12,0x7F,0x10, 0x27,0x45,0x45,0x45,0x39, 0x3C,0x4A,0x49,0x49,0x30, 0x01,0x71,0x09,0x05,0x03,
  0x36,0x49,0x49,0x49,0x36, 0x06,0x49,0x49,0x29,0x1E, 0x00,0x36,0x36,0x00,0x00, 0x00,0x56,0x36,0x00,0x00,
  0x08,0x14,0x22,0x41,0x00, 0x14,0x14,0x14,0x14,0x14, 0x00,0x41,0x22,0x14,0x08, 0x02,0x01,0x51,0x09,0x06,
  0x32,0x49,0x79,0x41,0x3E, 0x7E,0x11,0x11,0x11,0x7E, 0x7F,0x49,0x49,0x49,0x36, 0x3E,0x41,0x41,0x41,0x22,
  0x7F,0x41,0x41,0x22,0x1C, 0x7F,0x49,0x49,0x49,0x41, 0x7F,0x09,0x09,0x09,0x01, 0x3E,0x41,0x49,0x49,0x7A,
  0x7F,0x08,0x08,0x08,0x7F, 0x00,0x41,0x7F,0x41,0x00, 0x20,0x40,0x41,0x3F,0x01, 0x7F,0x08,0x14,0x22,0x41,
  0x7F,0x40,0x40,0x40,0x40, 0x7F,0x02,0x0C,0x02,0x7F, 0x7F,0x04,0x08,0x10,0x7F, 0x3E,0x41,0x41,0x41,0x3E,
  0x7F,0x09,0x09,0x09,0x06, 0x3E,0x41,0x51,0x21,0x5E, 0x7F,0x09,0x19,0x29,0x46, 0x46,0x49,0x49,0x49,0x31,
  0x01,0x01,0x7F,0x01,0x01, 0x3F,0x40,0x40,0x40,0x3F, 0x1F,0x20,0x40,0x20,0x1F, 0x3F,0x40,0x38,0x40,0x3F,
  0x63,0x14,0x08,0x14,0x63, 0x07,0x08,0x70,0x08,0x07, 0x61,0x51,0x49,0x45,0x43, 0x00,0x7F,0x41,0x41,0x00,
  0x02,0x04,0x08,0x10,0x20, 0x00,0x41,0x41,0x7F,0x00, 0x04,0x02,0x01,0x02,0x04, 0x40,0x40,0x40,0x40,0x40,
  0x00,0x01,0x02,0x04,0x00, 0x20,0x54,0x54,0x54,0x78, 0x7F,0x48,0x44,0x44,0x38, 0x38,0x44,0x44,0x44,0x20,
  0x38,0x44,0x44,0x48,0x7F, 0x38,0x54,0x54,0x54,0x18, 0x08,0x7E,0x09,0x01,0x02, 0x0C,0x52,0x52,0x52,0x3E,
  0x7F,0x08,0x04,0x04,0x78, 0x00,0x44,0x7D,0x40,0x00, 0x20,0x40,0x44,0x3D,0x00, 0x7F,0x10,0x28,0x44,0x00,
  0x00,0x41,0x7F,0x40,0x00, 0x7C,0x04,0x18,0x04,0x78, 0x7C,0x08,0x04,0x04,0x78, 0x38,0x44,0x44,0x44,0x38,
  0x7C,0x14,0x14,0x14,0x08, 0x08,0x14,0x14,0x18,0x7C, 0x7C,0x08,0x04,0x04,0x08, 0x48,0x54,0x54,0x54,0x20,
  0x04,0x3F,0x44,0x40,0x20, 0x3C,0x40,0x40,0x20,0x7C, 0x1C,0x20,0x40,0x20,0x1C, 0x3C,0x40,0x30,0x40,0x3C,
  0x44,0x28,0x10,0x28,0x44, 0x0C,0x50,0x50,0x50,0x3C, 0x44,0x64,0x54,0x4C,0x44, 0x00,0x08,0x36,0x41,0x00,
  0x00,0x00,0x7F,0x00,0x00, 0x00,0x41,0x36,0x08,0x00, 0x08,0x04,0x08,0x10,0x08,
  0x00,0x06,0x09,0x09,0x06   /* ° */
};
#define GLYPH_DEG 95

static FB* checkFb(lua_State* L, int i) { return (FB*)luaL_checkudata(L, i, MT); }
static int colorArg(lua_State* L, int i, int def) {
  if (lua_isnoneornil(L, i)) return def;
  if (lua_isboolean(L, i)) return lua_toboolean(L, i) ? 1 : 0;
  lua_Integer c = luaL_checkinteger(L, i);
  return c <= 0 ? 0 : (c == 2 ? 2 : 1);      // 0 إطفاء، 1 إضاءة، 2 عكس
}
static int intArg(lua_State* L, int i) {
  lua_Number n = luaL_checknumber(L, i);
  if (n > 100000) n = 100000;
  if (n < -100000) n = -100000;
  return (int)(n < 0 ? n - 0.5 : n + 0.5);   // تدوير للأعداد العشرية
}

static void pset(FB* f, int x, int y, int c) {
  if ((unsigned)x >= f->w || (unsigned)y >= f->h) return;
  uint8_t* b = bufOf(f) + (y >> 3) * f->w + x;
  uint8_t m = (uint8_t)(1u << (y & 7));
  if (c == 1) *b |= m; else if (c == 0) *b &= (uint8_t)~m; else *b ^= m;
}
static int pget(FB* f, int x, int y) {
  if ((unsigned)x >= f->w || (unsigned)y >= f->h) return 0;
  return (bufOf(f)[(y >> 3) * f->w + x] >> (y & 7)) & 1;
}
static void hspan(FB* f, int x0, int x1, int y, int c) {
  if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
  if (x0 < 0) x0 = 0;
  if (x1 >= f->w) x1 = f->w - 1;
  for (int x = x0; x <= x1; x++) pset(f, x, y, c);
}
static void line(FB* f, int x0, int y0, int x1, int y1, int c) {
  int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
  int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;
  for (int n = 0; n < 4096; n++) {
    pset(f, x0, y0, c);
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * err;
    if (e2 >= dy) { err += dy; x0 += sx; }
    if (e2 <= dx) { err += dx; y0 += sy; }
  }
}

static int l_new(lua_State* L) {
  int w = intArg(L, 1), h = intArg(L, 2);
  if (w < 1 || w > 128 || h < 8 || h > 64 || (h & 7)) return luaL_error(L, "fb.new(w, h): w 1..128, h 8..64 (multiple of 8)");
  FB* f = (FB*)lua_newuserdatauv(L, sizeof(FB) + (size_t)w * h / 8, 0);
  f->w = (uint8_t)w; f->h = (uint8_t)h;
  memset(bufOf(f), 0, (size_t)w * h / 8);
  luaL_setmetatable(L, MT);
  return 1;
}
static int l_clear(lua_State* L) {
  FB* f = checkFb(L, 1);
  memset(bufOf(f), colorArg(L, 2, 0) ? 0xFF : 0x00, (size_t)f->w * f->h / 8);
  return 0;
}
static int l_invert(lua_State* L) {
  FB* f = checkFb(L, 1);
  uint8_t* b = bufOf(f);
  for (size_t i = 0; i < (size_t)f->w * f->h / 8; i++) b[i] = (uint8_t)~b[i];
  return 0;
}
static int l_pixel(lua_State* L) {
  FB* f = checkFb(L, 1);
  pset(f, intArg(L, 2), intArg(L, 3), colorArg(L, 4, 1));
  return 0;
}
static int l_get(lua_State* L) {
  FB* f = checkFb(L, 1);
  lua_pushinteger(L, pget(f, intArg(L, 2), intArg(L, 3)));
  return 1;
}
static int l_line(lua_State* L) {
  FB* f = checkFb(L, 1);
  line(f, intArg(L, 2), intArg(L, 3), intArg(L, 4), intArg(L, 5), colorArg(L, 6, 1));
  return 0;
}
// fb:rect(x, y, w, h [, color [, fill]])
static int l_rect(lua_State* L) {
  FB* f = checkFb(L, 1);
  int x = intArg(L, 2), y = intArg(L, 3), w = intArg(L, 4), h = intArg(L, 5), c = colorArg(L, 6, 1);
  if (w <= 0 || h <= 0) return 0;
  if (lua_toboolean(L, 7)) { for (int j = y; j < y + h; j++) hspan(f, x, x + w - 1, j, c); return 0; }
  hspan(f, x, x + w - 1, y, c);
  if (h > 1) hspan(f, x, x + w - 1, y + h - 1, c);
  for (int j = y + 1; j < y + h - 1; j++) { pset(f, x, j, c); if (w > 1) pset(f, x + w - 1, j, c); }
  return 0;
}
// fb:circle(cx, cy, r [, color [, fill]])
static int l_circle(lua_State* L) {
  FB* f = checkFb(L, 1);
  int cx = intArg(L, 2), cy = intArg(L, 3), r = intArg(L, 4), c = colorArg(L, 5, 1);
  bool fill = lua_toboolean(L, 6);
  if (r < 0 || r > 200) return 0;
  int x = r, y = 0, err = 1 - r;
  while (x >= y) {
    if (fill) {
      hspan(f, cx - x, cx + x, cy + y, c); hspan(f, cx - x, cx + x, cy - y, c);
      hspan(f, cx - y, cx + y, cy + x, c); hspan(f, cx - y, cx + y, cy - x, c);
    } else {
      pset(f, cx + x, cy + y, c); pset(f, cx - x, cy + y, c); pset(f, cx + x, cy - y, c); pset(f, cx - x, cy - y, c);
      pset(f, cx + y, cy + x, c); pset(f, cx - y, cy + x, c); pset(f, cx + y, cy - x, c); pset(f, cx - y, cy - x, c);
    }
    y++;
    if (err < 0) err += 2 * y + 1; else { x--; err += 2 * (y - x) + 1; }
  }
  return 0;
}
// fb:bar(x, y, w, h, percent [, color]): إطار وتعبئة بنسبة مئوية
static int l_bar(lua_State* L) {
  FB* f = checkFb(L, 1);
  int x = intArg(L, 2), y = intArg(L, 3), w = intArg(L, 4), h = intArg(L, 5);
  lua_Number p = luaL_checknumber(L, 6);
  int c = colorArg(L, 7, 1);
  if (w < 3 || h < 3) return 0;
  if (p < 0) p = 0;
  if (p > 100) p = 100;
  for (int j = y; j < y + h; j++) {
    if (j == y || j == y + h - 1) hspan(f, x, x + w - 1, j, c);
    else { pset(f, x, j, c); pset(f, x + w - 1, j, c); }
  }
  int fillw = (int)((w - 4) * p / 100.0f + 0.5f);
  for (int j = y + 2; j < y + h - 2; j++) if (fillw > 0) hspan(f, x + 2, x + 1 + fillw, j, c);
  return 0;
}

// فكّ UTF-8 مبسّط: ASCII كما هو، و° (U+00B0) رمز الدرجة، وغير ذلك يصير ؟
static int nextGlyph(const unsigned char* s, size_t len, size_t* i) {
  unsigned char c = s[*i];
  (*i)++;
  if (c >= 32 && c <= 126) return c - 32;
  if (c == 0xC2 && *i < len && s[*i] == 0xB0) { (*i)++; return GLYPH_DEG; }
  if (c >= 0x80) { while (*i < len && (s[*i] & 0xC0) == 0x80) (*i)++; }   // تخطَّ بقية الحرف متعدد البايتات
  if (c == '\t') return 0;
  return '?' - 32;
}
// fb:text(x, y, str [, scale [, color]]) -> x بعد آخر حرف
static int l_text(lua_State* L) {
  FB* f = checkFb(L, 1);
  int x = intArg(L, 2), y = intArg(L, 3);
  size_t len; const char* str = luaL_checklstring(L, 4, &len);
  int sc = lua_isnoneornil(L, 5) ? 1 : intArg(L, 5);
  int c = colorArg(L, 6, 1);
  if (sc < 1) sc = 1;
  if (sc > 4) sc = 4;
  size_t i = 0;
  while (i < len) {
    int g = nextGlyph((const unsigned char*)str, len, &i);
    if (str[i - 1] == '\n') { x = intArg(L, 2); y += 8 * sc; continue; }
    const uint8_t* col = FONT + g * 5;
    for (int cx = 0; cx < 5; cx++)
      for (int cy = 0; cy < 7; cy++)
        if (col[cx] & (1 << cy))
          for (int dy = 0; dy < sc; dy++) for (int dx = 0; dx < sc; dx++) pset(f, x + cx * sc + dx, y + cy * sc + dy, c);
    x += 6 * sc;
  }
  lua_pushinteger(L, x);
  return 1;
}
static int l_text_width(lua_State* L) {
  size_t len; const char* str = luaL_checklstring(L, 2, &len);
  int sc = lua_isnoneornil(L, 3) ? 1 : intArg(L, 3);
  if (sc < 1) sc = 1;
  if (sc > 4) sc = 4;
  size_t i = 0; int n = 0;
  while (i < len) { nextGlyph((const unsigned char*)str, len, &i); n++; }
  lua_pushinteger(L, n * 6 * sc);
  return 1;
}
// fb:page(p) -> w بايت للصفحة p (8 صفوف) بصيغة SSD1306
static int l_page(lua_State* L) {
  FB* f = checkFb(L, 1);
  lua_Integer p = luaL_checkinteger(L, 2);
  if (p < 0 || p >= f->h / 8) return luaL_error(L, "page 0..%d", f->h / 8 - 1);
  lua_pushlstring(L, (const char*)bufOf(f) + p * f->w, f->w);
  return 1;
}
static int l_data(lua_State* L) {
  FB* f = checkFb(L, 1);
  lua_pushlstring(L, (const char*)bufOf(f), (size_t)f->w * f->h / 8);
  return 1;
}
static int l_size(lua_State* L) {
  FB* f = checkFb(L, 1);
  lua_pushinteger(L, f->w); lua_pushinteger(L, f->h);
  return 2;
}

void luaeng_fb_register(lua_State* L) {
  static const luaL_Reg methods[] = {
    {"clear", l_clear}, {"invert", l_invert}, {"pixel", l_pixel}, {"get", l_get}, {"line", l_line}, {"rect", l_rect},
    {"circle", l_circle}, {"bar", l_bar}, {"text", l_text}, {"text_width", l_text_width}, {"page", l_page},
    {"data", l_data}, {"size", l_size}, {NULL, NULL}
  };
  luaL_newmetatable(L, MT);
  lua_newtable(L);
  luaL_setfuncs(L, methods, 0);
  lua_setfield(L, -2, "__index");
  lua_pop(L, 1);
  lua_newtable(L);
  lua_pushcfunction(L, l_new);
  lua_setfield(L, -2, "new");
  lua_setglobal(L, "fb");
}
