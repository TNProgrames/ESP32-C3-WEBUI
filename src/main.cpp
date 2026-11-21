#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <FS.h>
#include <ArduinoJson.h>  // Use Version 7.x
#include <Update.h>
#include <esp_timer.h>
#include <stdarg.h>
#include "em.h"           // تأكد من تعريف index_html داخل هذا الملف
#include <string.h>

#define MAX_NAME_LEN   32
#define MAX_VALUE_LEN  32

// --- Settings On Ram ---
char localhost[33] = "";
char internethost[33] = "";
char pinreset[33] = "";
char wifimode[33] = "AP";
char wifissid[33] = "ESP32_STA";
char wifipass[33] = "12345678";
char apwifissid[33] = "ESP32_AP";
char apwifipass[33] = "12345678";

// --- Network Settings ---
const char* access_key = "1234"; // مفتاح الوصول لصفحة الطوارئ /em

// --- Token Store ---
char Admin_Token[33] = "";
char User_Token[33] = "";
// --- WiFi is Working 
bool wifiIsWorking = false;
volatile bool wifiApplyPending = false; // يُنفَّذ applyWiFi() داخل loop() وليس داخل callback

// --- OTA State ---
char otaError[64] = "";
bool otaStarted = false;
bool otaFsUnmounted = false;
volatile bool restartPending = false; // إعادة تشغيل مؤجلة (OTA / استعادة النسخة الاحتياطية)

// --- Firmware version (يمكن تمريره من البناء: -DFW_VERSION=\"v1.0.0\") ---
#ifndef FW_VERSION
#define FW_VERSION "dev"
#endif

// --- Log ring buffer (يُعرض في تبويب الطرفية) ---
#define LOG_SIZE 4096
static char logBuf[LOG_SIZE];
static uint32_t logTotal = 0;
static portMUX_TYPE logMux = portMUX_INITIALIZER_UNLOCKED;

static void logPrintf(const char* fmt, ...) {
  char tmp[160];
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
  va_end(ap);
  if (n <= 0) return;
  if (n >= (int)sizeof(tmp)) n = sizeof(tmp) - 1;
  Serial.write((const uint8_t*)tmp, n);
  portENTER_CRITICAL(&logMux);
  for (int i = 0; i < n; i++) logBuf[(logTotal++) % LOG_SIZE] = tmp[i];
  portEXIT_CRITICAL(&logMux);
}

static uint32_t uptimeSec() { return (uint32_t)(esp_timer_get_time() / 1000000ULL); }

// --- Login lockout & password reset (بزر فيزيائي) ---
static uint8_t  loginFails = 0;
static uint32_t loginLockUntil = 0;
static uint32_t resetWaitUntil = 0;   // != 0 : بانتظار الضغط على الزر
static bool     resetDone = false;
static uint32_t resetDoneAt = 0;
static bool     resetSawHigh = false;
static int      resetPin = -1;

AsyncWebServer server(80);

// --- Token Validation ---
// الرمز الفارغ مرفوض دائماً (قبل أول تسجيل دخول يكون الرمز فارغاً)
static bool tokenMatches(AsyncWebServerRequest *request, const char* stored) {
    if (stored[0] == '\0' || !request->hasHeader("User_Token")) return false;
    return strcmp(stored, request->getHeader("User_Token")->value().c_str()) == 0;
}
static bool isAdminToken(AsyncWebServerRequest *request) { return tokenMatches(request, Admin_Token); }
static bool isAnyToken(AsyncWebServerRequest *request)   { return tokenMatches(request, Admin_Token) || tokenMatches(request, User_Token); }

// --- Hardware Usage Functions ---

char* RamUsage(char* buffer, size_t bufferSize) {
    float totalHeap = ESP.getHeapSize();
    float freeHeap = ESP.getFreeHeap();
    float usagePercentage = ((totalHeap - freeHeap) / totalHeap) * 100.0;
    snprintf(buffer, bufferSize, "%.1f", usagePercentage);
    return buffer;
}

char* StorageUsage(char* buffer, size_t bufferSize) {
    size_t total = LittleFS.totalBytes();
    size_t used = LittleFS.usedBytes();
    if (total == 0) {
        snprintf(buffer, bufferSize, "0.0");
        return buffer;
    }
    float usagePercentage = ((float)used / (float)total) * 100.0;
    snprintf(buffer, bufferSize, "%.1f", usagePercentage);
    return buffer;
}

char* generateToken(char* buffer, size_t bufferSize) {
    const char alphanum[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    for (int i = 0; i < 32; i++) {
        buffer[i] = alphanum[random(sizeof(alphanum) - 1)];
    }
    buffer[32] = '\0';
    return buffer;
}

// --- JSON Engines (Optimized for ArduinoJson 7) ---

bool updateOrCreateJson(const char* filePath, const char* path, const char* newValue) {
  if (!path || !newValue || strlen(newValue) > MAX_VALUE_LEN) return false;

  JsonDocument doc;
  File file = LittleFS.open(filePath, "r");
  if (file) {
    deserializeJson(doc, file);
    file.close();
  }

  if (!doc.is<JsonArray>()) doc.to<JsonArray>();
  JsonVariant current = doc.as<JsonArray>();

  char temp[192];
  strlcpy(temp, path, sizeof(temp));
  char* token = strtok(temp, ".");
  
  while (token) {
    if (strcmp(token, "newAry") == 0) {
      current = current.as<JsonArray>().add<JsonArray>();
    } else if (strcmp(token, "newObj") == 0) {
      current = current.as<JsonArray>().add<JsonObject>();
    } else if (strstr(token, "_obj") || strstr(token, "_Obj")) {
      int idx = atoi(token);
      current = current.as<JsonArray>()[idx];
    } else if (token[strlen(token) - 1] == '-') {
      token[strlen(token) - 1] = 0;
      if (!current[token].is<JsonArray>()) current[token].to<JsonArray>();
      current = current[token];
    } else {
      if (!current[token].is<JsonObject>()) current[token].to<JsonObject>();
      current = current[token];
    }
    token = strtok(NULL, ".");
  }
  
  current.set(newValue);
  File fileW = LittleFS.open(filePath, "w");
  if (!fileW) return false;
  serializeJsonPretty(doc, fileW);
  fileW.close();
  return true;
}

bool getJson(const char* filePath, const char* path, char* outValue, size_t outValueSize) {
  if (!LittleFS.exists(filePath)) return false;
  JsonDocument doc;
  File file = LittleFS.open(filePath, "r");
  DeserializationError err = deserializeJson(doc, file);
  file.close();
  if (err) return false;

  JsonVariant current = doc.as<JsonArray>();
  char temp[192];
  strlcpy(temp, path, sizeof(temp));
  char* token = strtok(temp, ".");
  
  while (token) {
    if (strstr(token, "_obj") || strstr(token, "_Obj")) {
      int idx = atoi(token);
      current = current.as<JsonArray>()[idx];
    } else {
      current = current[token];
    }
    if (current.isNull()) return false;
    token = strtok(NULL, ".");
  }

  if (!current.is<const char*>()) return false;
  strlcpy(outValue, current.as<const char*>(), outValueSize);
  return true;
}

// --- Apply WiFi Settings ---

void applyWiFi() {
  wifiIsWorking = true;
  logPrintf("Reconfiguring WiFi...\n");
  WiFi.disconnect();
  WiFi.softAPdisconnect(true);
  delay(500);

  getJson("/settings.json", "0_Obj.wifi.mode", wifimode, sizeof(wifimode));
  getJson("/settings.json", "0_Obj.wifi.ssid", wifissid, sizeof(wifissid));
  getJson("/settings.json", "0_Obj.wifi.pass", wifipass, sizeof(wifipass));
  getJson("/settings.json", "0_Obj.wifi.ap_ssid", apwifissid, sizeof(apwifissid));
  getJson("/settings.json", "0_Obj.wifi.ap_pass", apwifipass, sizeof(apwifipass));

  bool wantSta = (strcmp(wifimode, "STA") == 0 || strcmp(wifimode, "AP_STA") == 0);
  bool wantAp  = !wantSta || strcmp(wifimode, "AP_STA") == 0; // أي قيمة غير معروفة = AP

  // اضبط الوضع أولاً ثم شغّل الواجهات
  WiFi.mode(wantSta && wantAp ? WIFI_AP_STA : (wantSta ? WIFI_STA : WIFI_AP));

  if (wantAp) {
    WiFi.softAP(apwifissid, apwifipass);
  }

  if (wantSta) {
    WiFi.begin(wifissid, wifipass);

    // فحص النجاح لمدة 10 ثواني
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
      delay(500); attempts++;
    }

    if (WiFi.status() != WL_CONNECTED && !wantAp) {
      logPrintf("STA failed, reverting to AP safety mode\n");
      WiFi.mode(WIFI_AP);
      WiFi.softAP("RECOVERY_MODE", "12345678");
    }
  }
  if (WiFi.status() == WL_CONNECTED) logPrintf("WiFi connected: %s\n", WiFi.localIP().toString().c_str());
  wifiIsWorking = false;
}

// --- System Initialization ---

void loadsetting() {
  Serial.begin(115200);
  logPrintf("Boot %s\n", FW_VERSION);
  if (!LittleFS.begin(true)) {
    logPrintf("LittleFS mount failed\n");
    return;
  }

  if (!LittleFS.exists("/users.json")) {
    updateOrCreateJson("/users.json", "newObj.username", "admin");
    updateOrCreateJson("/users.json", "0_Obj.password", "admin123");
    updateOrCreateJson("/users.json", "newObj.username", "user");
    updateOrCreateJson("/users.json", "1_Obj.password", "user123");
  }

  if (!LittleFS.exists("/settings.json")) {
    updateOrCreateJson("/settings.json", "newObj.localhost", "esp32.local");
    updateOrCreateJson("/settings.json", "0_Obj.internethost", "api.esp-controller.io");
    updateOrCreateJson("/settings.json", "0_Obj.pinreset", "9");
    updateOrCreateJson("/settings.json", "0_Obj.wifi.mode", "AP");
    updateOrCreateJson("/settings.json", "0_Obj.wifi.ssid", "ESP32_STA");
    updateOrCreateJson("/settings.json", "0_Obj.wifi.pass", "12345678");
    updateOrCreateJson("/settings.json", "0_Obj.wifi.ap_ssid", "ESP32_AP");
    updateOrCreateJson("/settings.json", "0_Obj.wifi.ap_pass", "12345678");
  }
  else
  {
    getJson("/settings.json", "0_Obj.localhost", localhost, sizeof(localhost));
    getJson("/settings.json", "0_Obj.internethost", internethost, sizeof(internethost));
    getJson("/settings.json", "0_Obj.pinreset", pinreset, sizeof(pinreset));
  }
  applyWiFi();
  
}

// --- Emergency Tools Helper ---
bool isAuthorizedEm(AsyncWebServerRequest *request) {
    if (request->hasParam("pass") && request->getParam("pass")->value() == access_key) return true;
    if (request->hasHeader("Cookie")) {
        String cookie = request->header("Cookie");
        if (cookie.indexOf("session_key_em=" + String(access_key)) != -1) return true;
    }
    return false;
}

bool isAuthorized(AsyncWebServerRequest *request) {
    if (isAdminToken(request)) return true;
    if (request->hasHeader("Cookie")) {
        String cookie = request->header("Cookie");
        if (cookie.indexOf("session_key=" + String(access_key)) != -1) return true;
    }
    return false;
}

void refreshSessionEm(AsyncWebServerResponse *response) {
    response->addHeader("Set-Cookie", "session_key_em=" + String(access_key) + "; Max-Age=900; Path=/");
}

void refreshSession(AsyncWebServerResponse *response) {
    response->addHeader("Set-Cookie", "session_key=" + String(access_key) + "; Max-Age=900; Path=/");
}

bool deleteRecursive(String path) {
    File root = LittleFS.open(path);
    if (!root) return false;
    if (!root.isDirectory()) {
        root.close();
        return LittleFS.remove(path);
    }
    File file = root.openNextFile();
    while (file) {
        String filePath = path + "/" + file.name();
        if (file.isDirectory()) { file.close(); deleteRecursive(filePath); }
        else { file.close(); LittleFS.remove(filePath); }
        file = root.openNextFile();
    }
    root.close();
    return LittleFS.rmdir(path);
}

// --- Path / Param Helpers ---
// المسار يجب أن يبدأ بـ / ولا يحتوي على ..
static bool validPath(const String& p) {
    return p.length() > 0 && p[0] == '/' && p.indexOf("..") < 0;
}
static bool getPathParam(AsyncWebServerRequest *request, const char* name, String& out) {
    if (!request->hasParam(name)) return false;
    out = request->getParam(name)->value();
    return validPath(out);
}

// --- Terminal commands ---
static String formatUptime(uint32_t sec) {
  char b[40];
  snprintf(b, sizeof(b), "%lud %02luh %02lum %02lus", (unsigned long)(sec / 86400), (unsigned long)((sec / 3600) % 24), (unsigned long)((sec / 60) % 60), (unsigned long)(sec % 60));
  return String(b);
}

static String runCommand(String line) {
  line.trim();
  if (line.length() == 0) return "";
  int sp = line.indexOf(' ');
  String c = sp < 0 ? line : line.substring(0, sp);
  String arg = sp < 0 ? String("") : line.substring(sp + 1);
  arg.trim();
  c.toLowerCase();

  if (c == "help") return "help | version | uptime | heap | wifi | ls [path] | df | echo <text> | reboot";
  if (c == "version") return String("firmware ") + FW_VERSION + "\nchip " + ESP.getChipModel() + " rev " + String(ESP.getChipRevision()) + "\nsdk " + ESP.getSdkVersion();
  if (c == "uptime") return formatUptime(uptimeSec());
  if (c == "heap") return String("free ") + String(ESP.getFreeHeap()) + " / total " + String(ESP.getHeapSize()) + " / min free " + String(ESP.getMinFreeHeap());
  if (c == "df") return String("used ") + String((unsigned long)LittleFS.usedBytes()) + " / total " + String((unsigned long)LittleFS.totalBytes());
  if (c == "echo") return arg;
  if (c == "reboot") { restartPending = true; return "Rebooting..."; }
  if (c == "wifi") {
    wifi_mode_t m = WiFi.getMode();
    String o = String("mode: ") + (m == WIFI_AP ? "AP" : (m == WIFI_STA ? "STA" : (m == WIFI_AP_STA ? "AP_STA" : "OFF")));
    o += "\nAP ip: " + WiFi.softAPIP().toString();
    if (WiFi.status() == WL_CONNECTED) o += "\nSTA ssid: " + WiFi.SSID() + "\nSTA ip: " + WiFi.localIP().toString() + "\nrssi: " + String(WiFi.RSSI());
    else o += "\nSTA: disconnected";
    return o;
  }
  if (c == "ls") {
    String p = arg.length() ? arg : String("/");
    if (!validPath(p)) return "invalid path";
    File d = LittleFS.open(p);
    if (!d || !d.isDirectory()) return "not a directory";
    String o;
    File f = d.openNextFile();
    while (f) {
      o += f.isDirectory() ? "[D] " : "    ";
      o += f.name();
      if (!f.isDirectory()) { o += "  "; o += String((unsigned long)f.size()); }
      o += "\n";
      f = d.openNextFile();
    }
    return o.length() ? o : String("(empty)");
  }
  return "Unknown command: " + c + " (type help)";
}

// --- Password reset (يتطلب ضغط الزر الفيزيائي) ---
static int resetPinNumber() { return pinreset[0] ? atoi(pinreset) : 9; }

static void doPasswordReset() {
  updateOrCreateJson("/users.json", "0_Obj.password", "admin123");
  updateOrCreateJson("/users.json", "1_Obj.password", "user123");
  Admin_Token[0] = 0; User_Token[0] = 0;
  loginFails = 0; loginLockUntil = 0;
  logPrintf("Passwords reset to defaults\n");
}

// OTA: admin يحدّث البرنامج أو الواجهة، ومفتاح الطوارئ يحدّث الواجهة (type=fs) فقط
static bool otaAllowed(AsyncWebServerRequest *request) {
    if (isAdminToken(request)) return true;
    bool fs = request->hasParam("type") && request->getParam("type")->value() == "fs";
    return fs && isAuthorizedEm(request);
}

// --- Emergency APIs Setup ---

void EmSetupAPI() {
    server.on("/em", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (isAuthorizedEm(request)) {
            AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html", index_html);
            refreshSessionEm(response);
            request->send(response);
        } else {
            // غير مصرّح: اعرض نافذة إدخال المفتاح
            request->send(request->beginResponse_P(401, "text/html", login_html));
        }
    });

    // التحقق من مفتاح الطوارئ (مع قفل مؤقت بعد 5 محاولات خاطئة)
    server.on("/em/check", HTTP_GET, [](AsyncWebServerRequest *request) {
        static uint8_t fails = 0;
        static uint32_t lockUntil = 0;
        uint32_t now = millis();
        if (lockUntil && (int32_t)(now - lockUntil) < 0) { request->send(429); return; }
        if (isAuthorizedEm(request) && request->hasParam("pass")) {
            fails = 0; lockUntil = 0;
            AsyncWebServerResponse *response = request->beginResponse(200);
            refreshSessionEm(response);
            request->send(response);
            return;
        }
        if (++fails >= 5) { fails = 0; lockUntil = now + 60000; }
        request->send(401);
    });

    server.on("/api/list", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!isAuthorized(request) && !isAuthorizedEm(request)) { request->send(403); return; }
        String path = "/";
        if (request->hasParam("path") && !getPathParam(request, "path", path)) { request->send(400); return; }
        File root = LittleFS.open(path);
        if (!root || !root.isDirectory()) { request->send(400); return; }
        JsonDocument doc;
        doc["total"] = LittleFS.totalBytes();
        doc["used"] = LittleFS.usedBytes();
        JsonArray files = doc["files"].to<JsonArray>();
        File file = root.openNextFile();
        while (file) {
            JsonObject f = files.add<JsonObject>();
            f["name"] = String(file.name());
            f["isDir"] = file.isDirectory();
            f["size"] = file.size();
            file = root.openNextFile();
        }
        String output; serializeJson(doc, output);
        AsyncWebServerResponse *response = request->beginResponse(200, "application/json", output);
        if (isAuthorizedEm(request)) {refreshSessionEm(response);}
        refreshSession(response);
        request->send(response);
    });

    server.on("/api/upload", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!isAuthorized(request) && !isAuthorizedEm(request)) { request->send(403); return; }
        AsyncWebServerResponse *response = request->beginResponse(200);
        if (isAuthorizedEm(request)) {refreshSessionEm(response);}
        refreshSession(response);
        request->send(response);
    }, [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
        if (!isAuthorized(request) && !isAuthorizedEm(request)) return;
        if (!index) request->_tempFile = LittleFS.open(filename, "w");
        if (request->_tempFile) request->_tempFile.write(data, len);
        if (final && request->_tempFile) request->_tempFile.close();
    });

    server.on("/api/download", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!isAuthorized(request) && !isAuthorizedEm(request)) { request->send(403); return; }
        String path;
        if (!getPathParam(request, "path", path) || !LittleFS.exists(path)) { request->send(400); return; }
        AsyncWebServerResponse *response = request->beginResponse(LittleFS, path, "application/octet-stream");
        if (isAuthorizedEm(request)) {refreshSessionEm(response);}
        refreshSession(response);
        request->send(response);
    });
    
    server.on("/api/mkdir", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!isAuthorized(request) && !isAuthorizedEm(request)) { request->send(404); return; }
        String path;
        if (!getPathParam(request, "path", path)) { request->send(400); return; }
        LittleFS.mkdir(path);
        AsyncWebServerResponse *response = request->beginResponse(200);
        if (isAuthorizedEm(request)) {refreshSessionEm(response);}
        refreshSession(response);
        request->send(response);
    });
    
    server.on("/api/read", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!isAuthorized(request) && !isAuthorizedEm(request)) { request->send(404); return; }
        String path;
        if (!getPathParam(request, "path", path) || !LittleFS.exists(path)) { request->send(400); return; }
        AsyncWebServerResponse *response = request->beginResponse(LittleFS, path, "text/plain");
        if (isAuthorizedEm(request)) {refreshSessionEm(response);}
        refreshSession(response);
        request->send(response);
    });

    server.on("/api/save", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!isAuthorized(request) && !isAuthorizedEm(request)) { request->send(403); return; }
        String path;
        if (!getPathParam(request, "path", path) || !request->hasParam("content", true)) { request->send(400); return; }
        File f = LittleFS.open(path, "w");
        if (!f) { request->send(500); return; }
        f.print(request->getParam("content", true)->value());
        f.close();
        AsyncWebServerResponse *response = request->beginResponse(200);
        if (isAuthorizedEm(request)) {refreshSessionEm(response);}
        refreshSession(response);
        request->send(response);
    });

    server.on("/api/delete", HTTP_DELETE, [](AsyncWebServerRequest *request) {
        if (!isAuthorized(request) && !isAuthorizedEm(request)) { request->send(403); return; }
        String path;
        if (!getPathParam(request, "path", path) || path == "/") { request->send(400); return; }
        bool success = deleteRecursive(path);
        AsyncWebServerResponse *response = request->beginResponse(success ? 200 : 500);
        if (isAuthorizedEm(request)) {refreshSessionEm(response);}
        refreshSession(response);
        request->send(response);
    });
    
    server.on("/api/rename", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!isAuthorized(request) && !isAuthorizedEm(request)) { request->send(404); return; }
        String oldPath, newPath;
        if (!getPathParam(request, "old", oldPath) || !getPathParam(request, "new", newPath)) { request->send(400); return; }
        LittleFS.rename(oldPath, newPath);
        AsyncWebServerResponse *response = request->beginResponse(200);
        if (isAuthorizedEm(request)) {refreshSessionEm(response);}
        refreshSession(response);
        request->send(response);
    });
    
    server.on("/api/format", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!isAuthorized(request) && !isAuthorizedEm(request)) { request->send(403); return; }
        LittleFS.format();
        AsyncWebServerResponse *response = request->beginResponse(200);
        if (isAuthorizedEm(request)) {refreshSessionEm(response);}
        refreshSession(response);
        request->send(response);
    });
}

// --- Main Application Web APIs ---

void setupServerWeb() {
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "Content-Type, Authorization, User_Token, User_Role");
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "POST, GET, DELETE, OPTIONS");
  
  server.serveStatic("/", LittleFS, "/app").setDefaultFile("index.html");

  // --- Login API ---
  server.on("/login", HTTP_POST, [](AsyncWebServerRequest *request){}, NULL,
  [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    JsonDocument doc; 
    if (deserializeJson(doc, data, len)) { request->send(400); return; }
    const char* u = doc["username"];
    const char* p = doc["password"];
    if (!u || !p) { request->send(400); return; }
    if (loginLockUntil && (int32_t)(millis() - loginLockUntil) < 0) {
      request->send(429, "application/json", "{\"error\":\"locked\"}");
      return;
    }

    char passWord[33] = "", token[33];
    int userIdx = (strcmp(u, "admin") == 0) ? 0 : ((strcmp(u, "user") == 0) ? 1 : -1);

    if (userIdx != -1) {
      char pathP[32];
      snprintf(pathP, sizeof(pathP), "%d_Obj.password", userIdx);

      if (getJson("/users.json", pathP, passWord, sizeof(passWord)) && strcmp(p, passWord) == 0) {
        generateToken(token, sizeof(token));
        if (userIdx == 0) strlcpy(Admin_Token, token, sizeof(Admin_Token));
        else strlcpy(User_Token, token, sizeof(User_Token));
        char res[128];
        snprintf(res, sizeof(res), "{\"token\":\"%s\", \"role\":\"%s\"}", token, u);
        loginFails = 0; loginLockUntil = 0;
        logPrintf("Login ok: %s\n", u);
        request->send(200, "application/json", res);
        return;
      }
    }
    logPrintf("Login failed: %s\n", u);
    if (++loginFails >= 5) { loginFails = 0; loginLockUntil = millis() + 30000; if (!loginLockUntil) loginLockUntil = 1; }
    request->send(401);
  });

  // --- إعادة ضبط كلمة المرور: تتطلب ضغط الزر الفيزيائي (GPIO من إعداد pinreset) خلال 30 ثانية ---
  server.on("/login/reset_request", HTTP_POST, [](AsyncWebServerRequest *request){
    int pin = resetPinNumber();
    if (pin < 0 || pin > 21 || pin == 18 || pin == 19) { request->send(400, "application/json", "{\"error\":\"bad pin\"}"); return; }
    resetPin = pin; resetDone = false; resetSawHigh = false;
    resetWaitUntil = millis() + 30000; if (!resetWaitUntil) resetWaitUntil = 1;
    pinMode(pin, INPUT_PULLUP);
    logPrintf("Password reset requested: press GPIO%d within 30s\n", pin);
    char res[48];
    snprintf(res, sizeof(res), "{\"pin\":%d,\"seconds\":30}", pin);
    request->send(200, "application/json", res);
  });

  server.on("/login/reset_status", HTTP_GET, [](AsyncWebServerRequest *request){
    JsonDocument d;
    if (resetDone && (millis() - resetDoneAt) < 60000) { d["state"] = "done"; d["admin"] = "admin123"; d["user"] = "user123"; }
    else if (resetWaitUntil) { int32_t left = (int32_t)(resetWaitUntil - millis()); d["state"] = "waiting"; d["left"] = left > 0 ? left / 1000 : 0; d["pin"] = resetPin; }
    else d["state"] = "idle";
    String o; serializeJson(d, o);
    request->send(200, "application/json", o);
  });

  // --- Settings SAVE APIs ---
  auto saveHandler = [](AsyncWebServerRequest *request, uint8_t *data, size_t len, int type) {
    if (isAdminToken(request)) {
      JsonDocument doc;
      if (deserializeJson(doc, data, len)) { request->send(400); return; }
      if (type == 0) { // WiFi
        updateOrCreateJson("/settings.json", "0_Obj.wifi.mode", doc["mode"]);
        updateOrCreateJson("/settings.json", "0_Obj.wifi.ssid", doc["ssid"]);
        updateOrCreateJson("/settings.json", "0_Obj.wifi.pass", doc["pass"]);
        updateOrCreateJson("/settings.json", "0_Obj.wifi.ap_ssid", doc["ap_ssid"]);
        updateOrCreateJson("/settings.json", "0_Obj.wifi.ap_pass", doc["ap_pass"]);
        wifiApplyPending = true; // يُطبَّق في loop() بعد إرسال الرد
      } else if (type == 1) { // Comm
        updateOrCreateJson("/settings.json", "0_Obj.localhost", doc["localhost"]);
        updateOrCreateJson("/settings.json", "0_Obj.internethost", doc["host"]);
      } else if (type == 2) { // Pin
        updateOrCreateJson("/settings.json", "0_Obj.pinreset", doc["pin"]);
      } else if (type == 3) { // Pass
        updateOrCreateJson("/users.json", "0_Obj.password", doc["ap"]);
        updateOrCreateJson("/users.json", "1_Obj.password", doc["up"]);
      }
      request->send(200);
    } else { request->send(401); }
  };

  server.on("/settings/save_wifi", HTTP_POST, [](AsyncWebServerRequest* r){}, NULL, [saveHandler](AsyncWebServerRequest* r, uint8_t* d, size_t l, size_t i, size_t t){ saveHandler(r,d,l,0); });
  server.on("/settings/save_comm", HTTP_POST, [](AsyncWebServerRequest* r){}, NULL, [saveHandler](AsyncWebServerRequest* r, uint8_t* d, size_t l, size_t i, size_t t){ saveHandler(r,d,l,1); });
  server.on("/settings/save_pin", HTTP_POST, [](AsyncWebServerRequest* r){}, NULL, [saveHandler](AsyncWebServerRequest* r, uint8_t* d, size_t l, size_t i, size_t t){ saveHandler(r,d,l,2); });
  server.on("/settings/save_pass", HTTP_POST, [](AsyncWebServerRequest* r){}, NULL, [saveHandler](AsyncWebServerRequest* r, uint8_t* d, size_t l, size_t i, size_t t){ saveHandler(r,d,l,3); });

  // --- Settings GET APIs ---
  
  server.on("/settings/get_stat", HTTP_GET, [](AsyncWebServerRequest *request){
    if(wifiIsWorking) {
       request->send(401);
    } else {
    if(WiFi.status() != WL_CONNECTED) {
       request->send(202, "application/json", "{\"ip\":\"" + WiFi.softAPIP().toString() + "\"}");
    } else {
    wifiIsWorking = true;
    JsonDocument doc;
    doc["ssid"] = (WiFi.status() == WL_CONNECTED) ? WiFi.SSID() : "";
    doc["ip"] = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
    doc["status"] = (WiFi.status() == WL_CONNECTED) ? "CONNECTED" : "DISCONNECTED";
    doc["rssi"] = WiFi.RSSI();
    String output; serializeJson(doc, output);
    request->send(202, "application/json", output);
    wifiIsWorking = false;
    }}
  });
  
  server.on("/settings/scan_wifi", HTTP_GET, [](AsyncWebServerRequest *request){
    if(wifiIsWorking) {
       request->send(401);
    } else {
    int n = WiFi.scanComplete();
    if(n == WIFI_SCAN_FAILED) {        // لم يبدأ مسح بعد
       WiFi.scanNetworks(true);
       request->send(202);
    } else if(n == WIFI_SCAN_RUNNING) { // المسح جارٍ، لا تلغِه
       request->send(202);
    } else {
       JsonDocument doc;
       JsonArray root = doc.to<JsonArray>();
       for (int i = 0; i < n; ++i) {
         JsonObject net = root.add<JsonObject>();
         net["ssid"] = WiFi.SSID(i);
         net["rssi"] = WiFi.RSSI(i);
         net["secure"] = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
       }
       WiFi.scanDelete();
       WiFi.scanNetworks(true);
       String output; serializeJson(doc, output);
       request->send(202, "application/json", output);
    }
    }
  });
  
  server.on("/settings/get_wifi", HTTP_GET, [](AsyncWebServerRequest *request){
    if (!isAdminToken(request)) { request->send(401); return; }
    char m[33] = "", s[33] = "", p[33] = "", ap_s[33] = "", ap_p[33] = "", res[256];
    getJson("/settings.json", "0_Obj.wifi.mode", m, 33);
    getJson("/settings.json", "0_Obj.wifi.ssid", s, 33);
    getJson("/settings.json", "0_Obj.wifi.pass", p, 33);
    getJson("/settings.json", "0_Obj.wifi.ap_ssid", ap_s, 33);
    getJson("/settings.json", "0_Obj.wifi.ap_pass", ap_p, 33);
    snprintf(res, sizeof(res), "{\"mode\":\"%s\",\"ssid\":\"%s\",\"pass\":\"%s\",\"ap_ssid\":\"%s\",\"ap_pass\":\"%s\"}", m, s, p, ap_s, ap_p);
    request->send(200, "application/json", res);
  });

  server.on("/settings/get_comm", HTTP_GET, [](AsyncWebServerRequest *request){
    if (!isAnyToken(request)) { request->send(401); return; }
    char l[33] = "", h[33] = "", res[128];
    getJson("/settings.json", "0_Obj.localhost", l, 33);
    getJson("/settings.json", "0_Obj.internethost", h, 33);
    snprintf(res, sizeof(res), "{\"localhost\":\"%s\",\"host\":\"%s\"}", l, h);
    request->send(200, "application/json", res);
  });

  server.on("/settings/get_pin", HTTP_GET, [](AsyncWebServerRequest *request){
    if (!isAnyToken(request)) { request->send(401); return; }
    char p[33] = "", res[64];
    getJson("/settings.json", "0_Obj.pinreset", p, 33);
    snprintf(res, sizeof(res), "{\"pin\":\"%s\"}", p);
    request->send(200, "application/json", res);
  });

  server.on("/settings/get_pass", HTTP_GET, [](AsyncWebServerRequest *request){
    if (isAdminToken(request)) {
        char ap[33] = "", up[33] = "", res[128];
        getJson("/users.json", "0_Obj.password", ap, 33);
        getJson("/users.json", "1_Obj.password", up, 33);
        snprintf(res, sizeof(res), "{\"ap\":\"%s\",\"up\":\"%s\"}", ap, up);
        request->send(200, "application/json", res);
    } else { request->send(401); }
  });

  server.on("/hdusage", HTTP_GET, [](AsyncWebServerRequest *request){
    if (!isAnyToken(request)) { request->send(401); return; }
    char r[16], s[16], res[192];
    RamUsage(r, 16); StorageUsage(s, 16);
    snprintf(res, sizeof(res), "{\"RamUsage\":\"%s\",\"StorageUsage\":\"%s\",\"Version\":\"%s\",\"Uptime\":%lu}", r, s, FW_VERSION, (unsigned long)uptimeSec());
    request->send(200, "application/json", res);
  });

  // --- OTA (admin، أو مفتاح الطوارئ للواجهة فقط): POST /api/ota?type=fw  (firmware.bin)  أو  type=fs  (littlefs.bin) ---
  server.on("/api/ota", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!otaAllowed(request)) { request->send(401); return; }
    bool started = otaStarted;
    otaStarted = false;
    if (otaError[0]) {
      if (otaFsUnmounted) { LittleFS.begin(false); otaFsUnmounted = false; } // أعد تركيب النظام بعد الفشل
      request->send(500, "application/json", String("{\"error\":\"") + otaError + "\"}");
      return;
    }
    if (!started) { request->send(400, "application/json", "{\"error\":\"no file\"}"); return; }
    request->send(200, "application/json", "{\"ok\":true}");
    restartPending = true;
  }, [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
    if (!index) {
      otaError[0] = 0;
      otaStarted = false;
      if (!otaAllowed(request)) { strlcpy(otaError, "unauthorized", sizeof(otaError)); return; }
      bool fs = request->hasParam("type") && request->getParam("type")->value() == "fs";
      if (!fs && (len == 0 || data[0] != 0xE9)) { strlcpy(otaError, "not a firmware image", sizeof(otaError)); return; }
      if (fs) { LittleFS.end(); otaFsUnmounted = true; }
      if (!Update.begin(UPDATE_SIZE_UNKNOWN, fs ? U_SPIFFS : U_FLASH)) {
        strlcpy(otaError, Update.errorString(), sizeof(otaError));
        return;
      }
      otaStarted = true;
    }
    if (otaError[0] || !otaStarted) return;
    if (Update.write(data, len) != len) {
      strlcpy(otaError, Update.errorString(), sizeof(otaError));
      Update.abort();
      return;
    }
    if (final && !Update.end(true)) {
      strlcpy(otaError, Update.errorString(), sizeof(otaError));
    }
  });

  // --- نسخة احتياطية (admin فقط): تنزيل واستعادة settings.json و users.json ---
  server.on("/settings/backup", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!isAdminToken(request)) { request->send(401); return; }
    String s = "[]", u = "[]";
    File f = LittleFS.open("/settings.json", "r");
    if (f) { s = f.readString(); f.close(); }
    f = LittleFS.open("/users.json", "r");
    if (f) { u = f.readString(); f.close(); }
    String out = "{\"version\":1,\"settings\":" + s + ",\"users\":" + u + "}";
    AsyncWebServerResponse *response = request->beginResponse(200, "application/json", out);
    response->addHeader("Content-Disposition", "attachment; filename=esp32-backup.json");
    request->send(response);
  });

  server.on("/settings/restore", HTTP_POST, [](AsyncWebServerRequest *request){}, NULL,
  [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    if (!isAdminToken(request)) { request->send(401); return; }
    if (total == 0 || total > 4096) { request->send(413); return; }
    // الجسم قد يصل على أجزاء: اجمعه ثم عالجه عند اكتماله
    if (index == 0) request->_tempObject = malloc(total + 1);
    char* buf = (char*)request->_tempObject;
    if (!buf) { request->send(500); return; }
    memcpy(buf + index, data, len);
    if (index + len < total) return;
    buf[total] = 0;

    JsonDocument doc;
    if (deserializeJson(doc, buf, total)) { request->send(400, "application/json", "{\"error\":\"invalid json\"}"); return; }
    JsonArray st = doc["settings"].as<JsonArray>();
    JsonArray us = doc["users"].as<JsonArray>();
    bool valid = !st.isNull() && st.size() >= 1 && st[0].is<JsonObject>()
              && !us.isNull() && us.size() >= 2
              && us[0]["username"].is<const char*>() && us[0]["password"].is<const char*>()
              && us[1]["username"].is<const char*>() && us[1]["password"].is<const char*>();
    if (!valid) { request->send(400, "application/json", "{\"error\":\"invalid backup\"}"); return; }

    File f = LittleFS.open("/settings.json", "w");
    if (!f) { request->send(500); return; }
    serializeJsonPretty(st, f); f.close();
    f = LittleFS.open("/users.json", "w");
    if (!f) { request->send(500); return; }
    serializeJsonPretty(us, f); f.close();

    request->send(200, "application/json", "{\"ok\":true}");
    restartPending = true;
  });

  // --- إعادة التشغيل (admin) ---
  server.on("/settings/restart", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!isAdminToken(request)) { request->send(401); return; }
    logPrintf("Restart requested\n");
    request->send(200, "application/json", "{\"ok\":true}");
    restartPending = true;
  });

  // --- الطرفية (admin): قراءة السجل وإرسال الأوامر ---
  server.on("/terminal/log", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!isAdminToken(request)) { request->send(401); return; }
    uint32_t since = request->hasParam("since") ? strtoul(request->getParam("since")->value().c_str(), NULL, 10) : 0;
    char* tmp = (char*)malloc(LOG_SIZE + 1);
    if (!tmp) { request->send(500); return; }
    portENTER_CRITICAL(&logMux);
    uint32_t total = logTotal;
    uint32_t oldest = total > LOG_SIZE ? total - LOG_SIZE : 0;
    uint32_t start = (since > total || since < oldest) ? oldest : since;
    uint32_t n = total - start;
    for (uint32_t i = 0; i < n; i++) tmp[i] = logBuf[(start + i) % LOG_SIZE];
    portEXIT_CRITICAL(&logMux);
    tmp[n] = 0;
    JsonDocument d;
    d["seq"] = total;
    d["text"] = (const char*)tmp;
    String o; serializeJson(d, o);
    free(tmp);
    request->send(200, "application/json", o);
  });

  server.on("/terminal/cmd", HTTP_POST, [](AsyncWebServerRequest *request){}, NULL,
  [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    if (!isAdminToken(request)) { request->send(401); return; }
    if (index != 0 || len != total || total > 256) { request->send(413); return; }
    JsonDocument d;
    if (deserializeJson(d, data, len)) { request->send(400); return; }
    const char* c = d["cmd"];
    if (!c) { request->send(400); return; }
    String out = runCommand(String(c));
    JsonDocument r;
    r["out"] = out;
    String o; serializeJson(r, o);
    request->send(200, "application/json", o);
  });

  // CORS Options
  server.onNotFound([](AsyncWebServerRequest *request){
    if (request->method() == HTTP_OPTIONS) request->send(200);
    // فُقد /app/index.html أو أول تشغيل: اعرض نافذة مفتاح الطوارئ بدل 404
    else if (request->method() == HTTP_GET && request->url() == "/") request->send(request->beginResponse_P(200, "text/html", login_html));
    else request->send(404);
  });
}

// --- Setup & Loop ---

void setup() {
  loadsetting();
  EmSetupAPI();
  setupServerWeb();
  server.begin();
}

void loop() {
  if (wifiApplyPending) {
    wifiApplyPending = false;
    delay(300); // أمهل الرد 200 وقتاً ليصل قبل إعادة ضبط الواي فاي
    applyWiFi();
  }
  if (resetWaitUntil) {
    if ((int32_t)(millis() - resetWaitUntil) >= 0) {
      resetWaitUntil = 0;
      logPrintf("Password reset timed out\n");
    } else if (!resetSawHigh) {
      if (digitalRead(resetPin) == HIGH) resetSawHigh = true;   // يجب أن يكون الزر غير مضغوط أولاً
    } else if (digitalRead(resetPin) == LOW) {
      delay(50);
      if (digitalRead(resetPin) == LOW) {
        doPasswordReset();
        resetWaitUntil = 0; resetDone = true; resetDoneAt = millis();
      }
    }
  }
  if (restartPending) {
    delay(1000); // أمهل الرد وقتاً ليصل ثم أعد التشغيل
    ESP.restart();
  }
  delay(10);
}