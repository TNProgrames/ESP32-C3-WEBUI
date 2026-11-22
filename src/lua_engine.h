#pragma once
#include <Arduino.h>

// محرك Lua: مهمة FreeRTOS واحدة تملك كل حالات Lua (حالة معزولة لكل سكريبت)
void luaeng_begin();
// تشغيل سكريبت من نص (يُنسخ داخلياً). يستبدل سكريبتاً يحمل نفس الاسم. يعيد false عند خطأ في الطلب
bool luaeng_run(const char* name, const char* code, size_t len);
bool luaeng_run_file(const char* name);   // يقرأ /scripts/<name>.lua
bool luaeng_stop(const char* name);
void luaeng_stop_all();
String luaeng_list_text();                // عرض نصي للطرفية
String luaeng_list_json();                // JSON للواجهة
bool luaeng_valid_name(const char* name);

// لوحات الواجهة: تعريفها JSON، وتحديثاتها عبر الاستطلاع، وأحداث المستخدم تُرسل للسكريبت
String luaeng_ui_panels_json();
String luaeng_ui_poll_json(uint32_t since);
bool luaeng_ui_event(int panel, const char* name, const char* valJson);

// يُنفَّذ في main.cpp
void logPrintf(const char* fmt, ...);
