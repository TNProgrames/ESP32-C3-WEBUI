// محمّل الدرايفرات الموقّعة (.drv) من /drivers في LittleFS إلى ذاكرة تنفيذ
#pragma once
#include <Arduino.h>
#include "drv_api.h"

#define DRV_DIR "/drivers"
#define DRV_MAX_LOADED 4
#define DRV_MAX_FILE (24 * 1024)

struct DrvEntry { char name[16]; char version[8]; uint32_t fileSize; uint32_t memSize; const drv_ops_t* ops; uint8_t* mem; int users; };

void drv_boot_load();                                            // عند الإقلاع: يحمّل كل /drivers/*.drv الصالحة (مع حماية من الانهيار)
bool drv_install(const uint8_t* buf, size_t len, String* err);   // يتحقق ثم يحفظ ثم يحمّل (يستبدل النسخة القديمة إن لم تكن مستعملة)
bool drv_remove(const char* name, String* err);                  // يفرغ من الذاكرة ويحذف الملف
const DrvEntry* drv_find(const char* name);
DrvEntry* drv_find_mut(const char* name);
String drv_list_json();
String drv_list_text();
bool drv_valid_name(const char* name);
