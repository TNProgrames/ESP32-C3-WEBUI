#include "drv_loader.h"
#include "drv_pubkey.h"
#include <LittleFS.h>
#include <esp_heap_caps.h>
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
    strncpy(out[n].info, g_drv[i].ops->info ? g_drv[i].ops->info : "", 47); out[n].info[47] = 0;
    n++;
  }
  return n;
}

static void unloadSlot(int i) {
  if (g_drv[i].mem) heap_caps_free(g_drv[i].mem);
  memset(&g_drv[i], 0, sizeof(DrvEntry)); g_used[i] = false;
}

// ينسخ الصورة إلى ذاكرة قابلة للتنفيذ ويطبّق إعادة التموضع ويستدعي drv_entry
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
  uint8_t* mem = (uint8_t*)heap_caps_malloc((p.memLen + 15) & ~15u, MALLOC_CAP_EXEC);
  if (!mem) {
    // تشخيص: كم متاح فعليًا من الذاكرة القابلة للتنفيذ؟
    static char msg[64];
    snprintf(msg, sizeof msg, "no executable memory (need %u, free %u, max %u)", (unsigned)p.memLen,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_EXEC), (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_EXEC));
    Serial.println(msg);
    if (err) *err = msg;
    return false;
  }
  memcpy(mem, b + HDR, p.imgLen);
  if (p.memLen > p.imgLen) memset(mem + p.imgLen, 0, p.memLen - p.imgLen);
  const uint8_t* rel = b + HDR + p.imgLen;
  uint32_t base = (uint32_t)(uintptr_t)mem;
  for (uint32_t i = 0; i < p.nrel; i++) {
    uint32_t off = rd32(rel + 4 * i);
    uint32_t v = rd32(mem + off) + base;
    memcpy(mem + off, &v, 4);
  }
  asm volatile("fence.i" ::: "memory");
  const drv_ops_t* ops = ((drv_entry_t)(uintptr_t)(base + p.entry))();
  bool good = ops && ops->version == DRV_API_VERSION && ops->open && ops->close && ops->methods && ops->nmethods > 0 && ops->nmethods <= 16;
  for (int i = 0; good && i < ops->nmethods; i++) {
    const drv_method_t& m = ops->methods[i];
    good = m.name && m.sig && m.fn && strlen(m.sig) <= DRV_MAX_ARGS;
    bool optional = false;
    for (const char* c = m.sig; good && *c; c++) {
      char l = (char)tolower((unsigned char)*c);
      if (l != 'i' && l != 's' && l != 'b' && l != 'd') good = false;
      if (isupper((unsigned char)*c)) optional = true; else if (optional) good = false;   // الإلزامي قبل الاختياري
    }
  }
  if (!good) { heap_caps_free(mem); if (err) *err = "bad driver ops"; return false; }
  DrvEntry& d = g_drv[slot];
  memset(&d, 0, sizeof(d));
  strncpy(d.name, p.name, 15); strncpy(d.version, p.ver, 7);
  d.fileSize = (uint32_t)fileSize; d.memSize = p.memLen; d.ops = ops; d.mem = mem;
  g_used[slot] = true;
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
    o["info"] = g_drv[i].ops->info;
    JsonArray ms = o["methods"].to<JsonArray>();
    for (int k = 0; k < g_drv[i].ops->nmethods; k++) {
      JsonObject m = ms.add<JsonObject>(); m["name"] = g_drv[i].ops->methods[k].name; m["sig"] = g_drv[i].ops->methods[k].sig;
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
