#include "drv_loader.h"
#include "drv_pubkey.h"
#include <LittleFS.h>
#include <mbedtls/sha256.h>
#include <mbedtls/ecdsa.h>
#include <mbedtls/ecp.h>
#include <mbedtls/bignum.h>
#include <ArduinoJson.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// تنسيق الملف (little-endian)  — يطابق tools/drv/build_drv.py
//  0 "ESPD" | 4 u16 fmt=1 | 6 u16 api | 8 u32 entry | 12 u32 image_len | 16 u32 mem_len | 20 u32 nreloc
// 24 name[16] | 40 version[8] | 48 محجوز[16] | 64 الصورة | reloc u32[] | توقيع r||s (64 بايت) على SHA-256 لكل ما قبله
#define HDR 64
#define SIG 64

// السجل تلمسه مهمتان (خادم الويب والمحرك) فيُحمى بقفل متداخل
static SemaphoreHandle_t g_mux = nullptr;
struct Lock { Lock() { if (g_mux) xSemaphoreTakeRecursive(g_mux, portMAX_DELAY); } ~Lock() { if (g_mux) xSemaphoreGiveRecursive(g_mux); } };
static DrvEntry g_drv[DRV_MAX_LOADED];
static bool g_used[DRV_MAX_LOADED];

static inline uint32_t rd32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static inline uint16_t rd16(const uint8_t* p) { return p[0] | (p[1] << 8); }

bool drv_valid_name(const char* n) {
  if (!n || !*n || strlen(n) > 15) return false;
  for (const char* p = n; *p; p++) if (!isalnum((unsigned char)*p)) return false;
  return true;
}

struct Parsed { uint32_t entry, imgLen, memLen, nrel; char name[17]; char ver[9]; };

static bool verifySig(const uint8_t* buf, size_t len) {
  uint8_t hash[32];
  if (mbedtls_sha256(buf, len - SIG, hash, 0) != 0) return false;
  mbedtls_ecp_group grp; mbedtls_ecp_point Q; mbedtls_mpi r, s;
  mbedtls_ecp_group_init(&grp); mbedtls_ecp_point_init(&Q); mbedtls_mpi_init(&r); mbedtls_mpi_init(&s);
  bool ok = mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_SECP256R1) == 0
         && mbedtls_ecp_point_read_binary(&grp, &Q, DRV_PUBKEY, sizeof(DRV_PUBKEY)) == 0
         && mbedtls_mpi_read_binary(&r, buf + len - SIG, 32) == 0
         && mbedtls_mpi_read_binary(&s, buf + len - 32, 32) == 0
         && mbedtls_ecdsa_verify(&grp, hash, 32, &Q, &r, &s) == 0;
  mbedtls_ecp_group_free(&grp); mbedtls_ecp_point_free(&Q); mbedtls_mpi_free(&r); mbedtls_mpi_free(&s);
  return ok;
}

// تحقق كامل: بنية ثم توقيع ثم إزاحات reloc
static bool parse(const uint8_t* b, size_t len, Parsed* o, String* err) {
  auto fail = [&](const char* m) { if (err) *err = m; return false; };
  if (len < HDR + SIG || len > DRV_MAX_FILE) return fail("bad size");
  if (memcmp(b, "ESPD", 4) != 0) return fail("bad magic");
  if (rd16(b + 4) != 1) return fail("bad format");
  if (rd16(b + 6) != DRV_API_VERSION) return fail("api version mismatch");
  o->entry = rd32(b + 8); o->imgLen = rd32(b + 12); o->memLen = rd32(b + 16); o->nrel = rd32(b + 20);
  if (o->imgLen == 0 || o->imgLen > DRV_MAX_FILE || o->memLen < o->imgLen || o->memLen > 64 * 1024 || o->nrel > 4096) return fail("bad layout");
  if (o->entry >= o->imgLen || (o->entry & 1)) return fail("bad entry");
  if ((uint64_t)HDR + o->imgLen + 4ull * o->nrel + SIG != len) return fail("size mismatch");
  memcpy(o->name, b + 24, 16); o->name[16] = 0; memcpy(o->ver, b + 40, 8); o->ver[8] = 0;
  o->name[15] = 0; o->ver[7] = 0;
  if (!drv_valid_name(o->name)) return fail("bad name");
  if (!verifySig(b, len)) return fail("bad signature");
  const uint8_t* rel = b + HDR + o->imgLen;
  for (uint32_t i = 0; i < o->nrel; i++) {
    uint32_t off = rd32(rel + 4 * i);
    if ((off & 3) || off + 4 > o->imgLen) return fail("bad relocation");
  }
  return true;
}

static int slotOf(const char* name) {
  for (int i = 0; i < DRV_MAX_LOADED; i++) if (g_used[i] && !strcmp(g_drv[i].name, name)) return i;
  return -1;
}
const DrvEntry* drv_find(const char* name) { Lock l; int i = slotOf(name); return i < 0 ? nullptr : &g_drv[i]; }
DrvEntry* drv_acquire(const char* name) {
  Lock l; int i = slotOf(name);
  if (i < 0) return nullptr;
  g_drv[i].users++;
  return &g_drv[i];
}
void drv_release(DrvEntry* e) { Lock l; if (e && e->users > 0) e->users--; }
int drv_snapshot(DrvBrief* out, int max) {
  Lock l; int n = 0;
  for (int i = 0; i < DRV_MAX_LOADED && n < max; i++) if (g_used[i]) {
    strncpy(out[n].name, g_drv[i].name, 15); out[n].name[15] = 0;
    strncpy(out[n].version, g_drv[i].version, 7); out[n].version[7] = 0;
    strncpy(out[n].info, g_drv[i].ops.info, 47); out[n].info[47] = 0;
    n++;
  }
  return n;
}


static void unloadSlot(int i) {
  free(g_drv[i].mem);
  memset(&g_drv[i], 0, sizeof(DrvEntry)); g_used[i] = false;
}

// ---------- تمرير المعاملات إلى ذاكرة الدرايفر (التخطيط 32-بت ثابت: انظر drv_api.h) ----------
// منطقة scratch: [0..64) معاملات · [64..128) نتائج · [128] nret · [132] ctx · [136..) نصوص
#define SC_ARGS 0
#define SC_RET 64
#define SC_NRET 128
#define SC_CTX 132
#define SC_STR 136
static inline void put32(uint8_t* p, uint32_t v) { memcpy(p, &v, 4); }
static inline uint32_t get32(const uint8_t* p) { uint32_t v; memcpy(&v, p, 4); return v; }

struct Marshal {
  DrvVM* vm; uint8_t* S; uint32_t strOff;
  explicit Marshal(DrvVM* v) : vm(v), S(v->mem + v->scrOff), strOff(SC_STR) { memset(S, 0, DRV_VM_SCRATCH); }
  uint32_t guest(uint32_t off) const { return DRV_GBASE + vm->scrOff + off; }
  // يكتب drv_val_t (8 بايت) عند off
  bool val(uint32_t off, const drv_val_t& v) {
    uint32_t w = 0;
    S[off] = v.type; S[off + 1] = 0;
    uint16_t len = 0;
    if (v.type == DRV_STR || v.type == DRV_BLOB) {
      len = v.len;
      if (strOff + len + 1 > DRV_VM_SCRATCH) return false;
      memcpy(S + strOff, v.v.p, len); S[strOff + len] = 0;
      w = guest(strOff); strOff += (len + 4) & ~3u;
    } else if (v.type == DRV_INT || v.type == DRV_BOOL) w = (uint32_t)v.v.i;
    memcpy(S + off + 2, &len, 2); put32(S + off + 4, w);
    return true;
  }
};

static bool vmRun(DrvEntry* d, uint32_t fn, const uint32_t* a, int n, uint32_t* r) {
  int f = drv_vm_call(&d->vm, fn, a, n, r);
  if (f) { Serial.printf("[drv] %s: vm fault %d at pc=0x%x\n", d->name, f, (unsigned)d->vm.pc); return false; }
  return true;
}

int drv_vm_open(DrvEntry* d, const drv_kv_t* cfg, int ncfg, uint32_t* ctx) {
  Marshal m(&d->vm);
  uint32_t kvOff = m.strOff; m.strOff += 12 * (uint32_t)ncfg;
  for (int i = 0; i < ncfg; i++) {
    size_t kl = strlen(cfg[i].key);
    if (m.strOff + kl + 1 > DRV_VM_SCRATCH) return DRV_E_ARGS;
    memcpy(m.S + m.strOff, cfg[i].key, kl + 1);
    put32(m.S + kvOff + 12 * i, m.guest(m.strOff)); m.strOff += (kl + 4) & ~3u;
    if (!m.val(kvOff + 12 * i + 4, cfg[i].val)) return DRV_E_ARGS;
  }
  uint32_t a[4] = { m.guest(SC_CTX), drv_vm_api_addr(&d->vm), m.guest(kvOff), (uint32_t)ncfg }, rc = 0;
  if (!vmRun(d, d->ops.open, a, 4, &rc)) return DRV_E_FAULT;
  *ctx = get32(m.S + SC_CTX);
  return (int)rc;
}

int drv_vm_invoke(DrvEntry* d, int mi, uint32_t ctx, const drv_val_t* args, int nargs, drv_val_t* ret, int* nret) {
  Marshal m(&d->vm);
  for (int i = 0; i < nargs; i++) if (!m.val(SC_ARGS + 8 * i, args[i])) return DRV_E_ARGS;
  uint32_t a[5] = { ctx, m.guest(SC_ARGS), (uint32_t)nargs, m.guest(SC_RET), m.guest(SC_NRET) }, rc = 0;
  if (!vmRun(d, d->ops.methods[mi].fn, a, 5, &rc)) return DRV_E_FAULT;
  int nr = (int)get32(m.S + SC_NRET);
  if (nr < 0) nr = 0; if (nr > DRV_MAX_ARGS) nr = DRV_MAX_ARGS;
  *nret = nr;
  for (int i = 0; i < nr; i++) {
    const uint8_t* e = m.S + SC_RET + 8 * i;
    drv_val_t& v = ret[i]; memset(&v, 0, sizeof v);
    v.type = e[0]; memcpy(&v.len, e + 2, 2);
    uint32_t w = get32(e + 4);
    if (v.type == DRV_STR || v.type == DRV_BLOB) {
      const uint8_t* p = drv_vm_ptr(&d->vm, w, v.len);     // مؤشر داخل ذاكرة الدرايفر، صالح حتى الاستدعاء التالي
      if (!p) v.type = DRV_NIL; else v.v.p = p;
    } else if (v.type == DRV_INT || v.type == DRV_BOOL) v.v.i = (int32_t)w;
    else v.type = DRV_NIL;
  }
  return (int)rc;
}

void drv_vm_close(DrvEntry* d, uint32_t ctx) {
  uint32_t a[1] = { ctx }, r;
  vmRun(d, d->ops.close, a, 1, &r);
}

// ينسخ الصورة إلى ذاكرة الدرايفر الخاصة ويطبّق إعادة التموضع ويشغّل drv_entry داخل المفسّر
static bool loadBuf(const uint8_t* b, size_t len, size_t fileSize, String* err) {
  Parsed p;
  if (!parse(b, len, &p, err)) return false;
  int slot = slotOf(p.name);
  if (slot >= 0) {
    if (g_drv[slot].users > 0) { if (err) *err = "driver in use"; return false; }
    unloadSlot(slot);
  } else {
    for (int i = 0; i < DRV_MAX_LOADED; i++) if (!g_used[i]) { slot = i; break; }
    if (slot < 0) { if (err) *err = "too many drivers"; return false; }
  }
  size_t asz = drv_vm_arena_size(p.memLen);
  uint8_t* mem = (uint8_t*)calloc(1, asz);
  if (!mem) { if (err) *err = "no memory"; return false; }
  memcpy(mem, b + HDR, p.imgLen);
  const uint8_t* rel = b + HDR + p.imgLen;
  for (uint32_t i = 0; i < p.nrel; i++) {        // العناوين في نظر الدرايفر = DRV_GBASE + إزاحة
    uint32_t off = rd32(rel + 4 * i);
    put32(mem + off, get32(mem + off) + DRV_GBASE);
  }
  DrvEntry d; memset(&d, 0, sizeof d);
  if (!drv_vm_init(&d.vm, mem, p.memLen, p.imgLen, drv_host_call)) { free(mem); if (err) *err = "bad layout"; return false; }
  put32(mem + d.vm.apiOff, DRV_API_VERSION);
  uint32_t opsG = 0;
  d.mem = mem;
  if (!vmRun(&d, DRV_GBASE + p.entry, nullptr, 0, &opsG)) { free(mem); if (err) *err = "driver entry fault"; return false; }
  // اقرأ جدول ops من ذاكرة الدرايفر (تخطيط 32-بت: version,name,info,methods,nmethods,open,close)
  const uint8_t* o = drv_vm_ptr(&d.vm, opsG, 28);
  bool good = o && get32(o) == DRV_API_VERSION;
  int nm = good ? (int)get32(o + 16) : 0;
  good = good && nm > 0 && nm <= DRV_MAX_METHODS;
  d.ops.nmethods = nm;
  if (good) {
    d.ops.name = drv_vm_str(&d.vm, get32(o + 4), 32);
    d.ops.info = drv_vm_str(&d.vm, get32(o + 8), 96);
    d.ops.open = get32(o + 20); d.ops.close = get32(o + 24);
    good = d.ops.name && d.ops.info && d.ops.open - DRV_GBASE < p.imgLen && d.ops.close - DRV_GBASE < p.imgLen;
  }
  for (int i = 0; good && i < nm; i++) {
    const uint8_t* me = drv_vm_ptr(&d.vm, get32(o + 12) + 12u * i, 12);
    good = me != nullptr;
    if (!good) break;
    DrvMethodInfo& m = d.ops.methods[i];
    m.name = drv_vm_str(&d.vm, get32(me), 32); m.sig = drv_vm_str(&d.vm, get32(me + 4), DRV_MAX_ARGS + 1); m.fn = get32(me + 8);
    good = m.name && m.sig && m.fn - DRV_GBASE < p.imgLen;
    bool optional = false;
    for (const char* c = m.sig; good && *c; c++) {
      char l = (char)tolower((unsigned char)*c);
      if (l != 'i' && l != 's' && l != 'b' && l != 'd') good = false;
      if (isupper((unsigned char)*c)) optional = true; else if (optional) good = false;   // الإلزامي قبل الاختياري
    }
  }
  if (!good) { free(mem); if (err) *err = "bad driver ops"; return false; }
  strncpy(d.name, p.name, 15); strncpy(d.version, p.ver, 7);
  d.fileSize = (uint32_t)fileSize; d.memSize = (uint32_t)asz;
  g_drv[slot] = d; g_used[slot] = true;
  return true;
}

static uint8_t* readFile(const String& path, size_t* n) {
  File f = LittleFS.open(path, "r");
  if (!f || f.isDirectory() || f.size() < HDR + SIG || f.size() > DRV_MAX_FILE) return nullptr;
  size_t sz = f.size();
  uint8_t* buf = (uint8_t*)malloc(sz);
  if (!buf) return nullptr;
  if (f.read(buf, sz) != sz) { free(buf); return nullptr; }
  *n = sz; return buf;
}

// حماية: يُكتب اسم الملف قبل تحميله ويُمحى بعده. إن بقي عند الإقلاع فقد انهار الجهاز أثناء تحميله فيُحجَر.
#define LOADING_MARK DRV_DIR "/.loading"
void drv_boot_load() {
  if (!g_mux) g_mux = xSemaphoreCreateRecursiveMutex();
  if (!LittleFS.exists(DRV_DIR)) return;
  if (LittleFS.exists(LOADING_MARK)) {
    File m = LittleFS.open(LOADING_MARK, "r");
    String bad = m ? m.readString() : String(); if (m) m.close();
    LittleFS.remove(LOADING_MARK);
    if (bad.length() && LittleFS.exists(bad)) { LittleFS.rename(bad, bad + ".bad"); Serial.printf("[drv] quarantined %s\n", bad.c_str()); }
  }
  File dir = LittleFS.open(DRV_DIR);
  if (!dir || !dir.isDirectory()) return;
  String names[DRV_MAX_LOADED + 4]; int n = 0;
  for (File f = dir.openNextFile(); f && n < DRV_MAX_LOADED + 4; f = dir.openNextFile()) {
    String nm = f.name(); if (nm.endsWith(".drv")) names[n++] = String(DRV_DIR "/") + nm;
  }
  for (int i = 0; i < n; i++) {
    File m = LittleFS.open(LOADING_MARK, "w"); if (m) { m.print(names[i]); m.close(); }
    size_t len; uint8_t* buf = readFile(names[i], &len);
    String err;
    bool have = buf != nullptr;
    bool ok = have && loadBuf(buf, len, len, &err);
    free(buf);
    LittleFS.remove(LOADING_MARK);
    Serial.printf("[drv] %s: %s\n", names[i].c_str(), ok ? "ok" : (have ? err.c_str() : "unreadable"));
  }
}

bool drv_install(const uint8_t* buf, size_t len, String* err) {
  Lock l;
  Parsed p;
  if (!parse(buf, len, &p, err)) return false;
  if (LittleFS.totalBytes() - LittleFS.usedBytes() < len + 4096) { if (err) *err = "no space"; return false; }
  if (!LittleFS.exists(DRV_DIR)) LittleFS.mkdir(DRV_DIR);
  String path = String(DRV_DIR "/") + p.name + ".drv";
  if (!loadBuf(buf, len, len, err)) return false;          // يرفض إن كان مستعملًا قبل أن نلمس الملف
  File f = LittleFS.open(path, "w");
  if (!f || f.write(buf, len) != len) { if (f) f.close(); LittleFS.remove(path); drv_remove(p.name, nullptr); if (err) *err = "write failed"; return false; }
  f.close();
  return true;
}

bool drv_remove(const char* name, String* err) {
  Lock l;
  if (!drv_valid_name(name)) { if (err) *err = "bad name"; return false; }
  int i = slotOf(name);
  if (i >= 0) {
    if (g_drv[i].users > 0) { if (err) *err = "driver in use"; return false; }
    unloadSlot(i);
  }
  LittleFS.remove(String(DRV_DIR "/") + name + ".drv");
  return true;
}

String drv_list_json() {
  Lock l;
  JsonDocument d; JsonArray a = d.to<JsonArray>();
  for (int i = 0; i < DRV_MAX_LOADED; i++) if (g_used[i]) {
    JsonObject o = a.add<JsonObject>();
    o["name"] = g_drv[i].name; o["version"] = g_drv[i].version;
    o["size"] = g_drv[i].fileSize; o["mem"] = g_drv[i].memSize; o["users"] = g_drv[i].users;
    o["info"] = g_drv[i].ops.info;
    JsonArray ms = o["methods"].to<JsonArray>();
    for (int k = 0; k < g_drv[i].ops.nmethods; k++) {
      JsonObject m = ms.add<JsonObject>(); m["name"] = g_drv[i].ops.methods[k].name; m["sig"] = g_drv[i].ops.methods[k].sig;
    }
  }
  String s; serializeJson(d, s); return s;
}
String drv_list_text() {
  Lock l;
  String s;
  for (int i = 0; i < DRV_MAX_LOADED; i++) if (g_used[i])
    s += String(g_drv[i].name) + " v" + g_drv[i].version + "  file=" + g_drv[i].fileSize + "B ram=" + g_drv[i].memSize + "B users=" + g_drv[i].users + "\n";
  return s.length() ? s : String("no drivers");
}
