// lcd1602: شاشة حروف HD44780 عبر محوّل I2C (PCF8574). cfg: addr=0x27, cols=16, rows=2
// الدوال: clear() · text(row, col, str) · backlight(bool) · cursor(row, col)
#include "drvrt.h"

#define RS 0x01
#define EN 0x04
#define BL 0x08

typedef struct { const drv_api_t* api; uint8_t addr, cols, rows, bl; } Lcd;
static const char NAME[] = "lcd1602";

static int wr(Lcd* l, uint8_t b) { uint8_t v = b | (l->bl ? BL : 0); return l->api->i2c_write(l->addr, &v, 1); }
static int nib(Lcd* l, uint8_t n, uint8_t rs) {
  uint8_t b = (uint8_t)((n << 4) | rs);
  int e = wr(l, b); if (e) return e;
  wr(l, b | EN); l->api->delay_us(1);
  wr(l, b);      l->api->delay_us(50);
  return 0;
}
static int send(Lcd* l, uint8_t v, uint8_t rs) { int e = nib(l, v >> 4, rs); return e ? e : nib(l, v & 15, rs); }

static int do_open(void** ctx, const drv_api_t* api, const drv_kv_t* cfg, int n) {
  Lcd* l = (Lcd*)api->alloc(sizeof(Lcd));
  if (!l) return 1;
  l->api = api; l->bl = 1;
  l->addr = (uint8_t)drv_cfg_int(cfg, n, "addr", 0x27);
  l->cols = (uint8_t)drv_cfg_int(cfg, n, "cols", 16);
  l->rows = (uint8_t)drv_cfg_int(cfg, n, "rows", 2);
  api->delay_ms(50);
  int e = nib(l, 3, 0);
  if (e) { api->release(l); return e; }
  api->delay_ms(5); nib(l, 3, 0); api->delay_us(150); nib(l, 3, 0); nib(l, 2, 0);
  send(l, 0x28, 0); send(l, 0x0C, 0); send(l, 0x06, 0); send(l, 0x01, 0);
  api->delay_ms(2);
  *ctx = l;
  return 0;
}
static void do_close(void* ctx) { Lcd* l = (Lcd*)ctx; l->bl = 0; wr(l, 0); l->api->release(l); }

static int goto_rc(Lcd* l, int r, int c) {
  static const uint8_t base[4] = {0x00, 0x40, 0x14, 0x54};
  if (r < 0 || r >= l->rows || r > 3 || c < 0 || c >= l->cols) return DRV_E_ARGS;
  return send(l, (uint8_t)(0x80 | (base[r] + c)), 0);
}

static int do_call(void* ctx, const char* m, const drv_val_t* a, int na, drv_val_t* ret, int* nret) {
  Lcd* l = (Lcd*)ctx; (void)ret; *nret = 0;
  if (!strcmp(m, "clear")) { int e = send(l, 0x01, 0); l->api->delay_ms(2); return e; }
  if (!strcmp(m, "backlight")) { l->bl = (na > 0 && a[0].type == DRV_BOOL && a[0].v.i) ? 1 : 0; return wr(l, 0); }
  if (!strcmp(m, "cursor")) { if (!drv_is_int(a, na, 0) || !drv_is_int(a, na, 1)) return DRV_E_ARGS; return goto_rc(l, a[0].v.i, a[1].v.i); }
  if (!strcmp(m, "text")) {
    if (!drv_is_int(a, na, 0) || !drv_is_int(a, na, 1) || !drv_is_str(a, na, 2)) return DRV_E_ARGS;
    int e = goto_rc(l, a[0].v.i, a[1].v.i); if (e) return e;
    int room = l->cols - a[1].v.i;
    for (int i = 0; i < a[2].len && i < room; i++) { e = send(l, (uint8_t)a[2].v.s[i], RS); if (e) return e; }
    return 0;
  }
  return DRV_E_UNKNOWN;
}

static const drv_ops_t OPS = { DRV_API_VERSION, NAME, do_open, do_close, do_call };
DRV_EXPORT const drv_ops_t* drv_entry(void) { return &OPS; }
