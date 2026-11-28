#pragma once
// تقسيم LittleFS إلى ثلاث مساحات مستقلة (انظر docs/PARTITIONS.md):
//   C  fs_c : الواجهة (/app)                              ← تُحدَّث بـ OTA وحدها
//   D  fs_d : الإعدادات: settings.json users.json wifi_nets.json em_key
//   E  fs_e : سكريبتات Lua والدرايفرات وإعدادات الإقلاع وصفحات التطبيقات (كل ما عدا ما سبق)
// يحافظ الموجّه على واجهة LittleFS المعتادة (open/exists/remove/rename/...) ويختار المساحة من المسار،
// فلا يتغير بقية الكود. المساحات تُركَّب على /c و/d و/e.
#include <Arduino.h>
#include <LittleFS.h>

struct FsRouter {
  fs::LittleFSFS c, d, e;
  static constexpr const char* LABEL[3] = { "fs_c", "fs_d", "fs_e" };
  static constexpr const char* NAME[3]  = { "ui", "config", "data" };

  static bool isName(const String& s, const char* n) {          // n أو n.tmp أو n.bak
    size_t l = strlen(n);
    return s.startsWith(n) && (s.length() == l || s.substring(l) == ".tmp" || s.substring(l) == ".bak");
  }
  // بادئة القرص (يستعملها مدير الطوارئ كأقراص ويندوز): /c/... أو /d/... أو /e/...  ← تحدّد المساحة صراحةً وتُحذف قبل الوصول للمساحة
  static int drivePrefix(const String& s) {
    if (s.length() >= 2 && s[0] == '/' && (s[1] == 'c' || s[1] == 'd' || s[1] == 'e') && (s.length() == 2 || s[2] == '/')) return s[1] - 'c';
    return -1;
  }
  static String inner(const String& p) {
    String s = p.startsWith("/") ? p : "/" + p;
    return drivePrefix(s) >= 0 ? (s.length() == 2 ? String("/") : s.substring(2)) : s;
  }
  // 0 = C، 1 = D، 2 = E
  static int partOf(const String& p) {
    String s = p.startsWith("/") ? p : "/" + p;
    int dp = drivePrefix(s); if (dp >= 0) return dp;
    if (s.startsWith("/app") && (s.length() == 4 || s[4] == '/')) return 0;
    if (isName(s, "/settings.json") || isName(s, "/users.json") || isName(s, "/wifi_nets.json") || isName(s, "/em_key")) return 1;
    return 2;
  }
  fs::LittleFSFS& part(int i) { return i == 0 ? c : (i == 1 ? d : e); }
  fs::LittleFSFS& of(const String& p) { return part(partOf(p)); }

  bool begin(bool fmt = false) {
    bool a = c.begin(fmt, "/c", 10, LABEL[0]), b = d.begin(fmt, "/d", 10, LABEL[1]), x = e.begin(fmt, "/e", 10, LABEL[2]);
    return a && b && x;
  }
  bool beginC() { return c.begin(false, "/c", 10, LABEL[0]); }    // إعادة تركيب الواجهة بعد OTA فاشل
  void end() { c.end(); d.end(); e.end(); }
  void endC() { c.end(); }

  fs::File open(const String& p, const char* mode = "r", bool create = false) { return of(p).open(inner(p), mode, create); }
  bool exists(const String& p) { return (p == "/") ? true : of(p).exists(inner(p)); }
  bool remove(const String& p) { return of(p).remove(inner(p)); }
  bool mkdir(const String& p) { return of(p).mkdir(inner(p)); }
  bool rmdir(const String& p) { return of(p).rmdir(inner(p)); }
  bool rename(const String& a, const String& b) { int i = partOf(a); return i == partOf(b) && part(i).rename(inner(a), inner(b)); }
  bool format() { bool ok = c.format(); ok = d.format() && ok; ok = e.format() && ok; return ok; }
  bool format(int i) { return part(i).format(); }

  // totalBytes/usedBytes الافتراضية = مساحة E (حيث تُثبَّت الدرايفرات والسكريبتات)
  size_t totalBytes() { return e.totalBytes(); }
  size_t usedBytes() { return e.usedBytes(); }
  size_t totalAll() { return c.totalBytes() + d.totalBytes() + e.totalBytes(); }
  size_t usedAll() { return c.usedBytes() + d.usedBytes() + e.usedBytes(); }
};
extern FsRouter gFs;
#define LittleFS gFs
