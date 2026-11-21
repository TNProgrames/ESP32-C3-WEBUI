#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <FS.h>
#include <ArduinoJson.h>  // Use Version 7.x
#include <Update.h>
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
volatile bool otaRestartPending = false;

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
  Serial.println("Reconfiguring WiFi...");
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
      delay(500); Serial.print("."); attempts++;
    }

    if (WiFi.status() != WL_CONNECTED && !wantAp) {
      Serial.println("STA Failed! Reverting to AP Safety Mode");
      WiFi.mode(WIFI_AP);
      WiFi.softAP("RECOVERY_MODE", "12345678");
    }
  }
  wifiIsWorking = false;
}

// --- System Initialization ---

void loadsetting() {
  Serial.begin(115200);
  if (!LittleFS.begin(true)) {
    Serial.println(F("LittleFS Mount Failed"));
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
        request->send(200, "application/json", res);
        return;
      }
    }
    request->send(401);
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
    char r[16], s[16], res[128];
    RamUsage(r, 16); StorageUsage(s, 16);
    snprintf(res, sizeof(res), "{\"RamUsage\":\"%s\",\"StorageUsage\":\"%s\"}", r, s);
    request->send(200, "application/json", res);
  });

  // --- OTA (admin فقط): POST /api/ota?type=fw  (firmware.bin)  أو  type=fs  (littlefs.bin) ---
  server.on("/api/ota", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!isAdminToken(request)) { request->send(401); return; }
    bool started = otaStarted;
    otaStarted = false;
    if (otaError[0]) {
      if (otaFsUnmounted) { LittleFS.begin(false); otaFsUnmounted = false; } // أعد تركيب النظام بعد الفشل
      request->send(500, "application/json", String("{\"error\":\"") + otaError + "\"}");
      return;
    }
    if (!started) { request->send(400, "application/json", "{\"error\":\"no file\"}"); return; }
    request->send(200, "application/json", "{\"ok\":true}");
    otaRestartPending = true;
  }, [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
    if (!index) {
      otaError[0] = 0;
      otaStarted = false;
      if (!isAdminToken(request)) { strlcpy(otaError, "unauthorized", sizeof(otaError)); return; }
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
  if (otaRestartPending) {
    delay(1000); // أمهل الرد وقتاً ليصل ثم أعد التشغيل
    ESP.restart();
  }
  delay(10);
}