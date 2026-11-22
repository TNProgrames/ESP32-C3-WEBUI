// المفتاح = النص العربي، والقيمة = الترجمة الإنجليزية. أي نص بلا ترجمة يظهر بالعربية.
export const en = {
  // App
  'مدير': 'Admin', 'مستخدم': 'User', 'خروج': 'Logout',
  'الحالة': 'Status', 'الواي فاي': 'WiFi', 'الاتصال': 'Connection', 'كلمات المرور': 'Passwords',
  'نسخ احتياطي': 'Backup', 'الطرفية': 'Terminal', 'تحديث': 'Update',
  // Lab
  'مختبر Lua': 'Lua Lab',
  'اكتب سكريبت Lua وشغّله على الجهاز مباشرة. المتاح: gpio, adc, pwm, timer, sys, ui, print.': 'Write a Lua script and run it on the device. Available: gpio, adc, pwm, timer, sys, ui, print.',
  'اللوحات': 'Panels', 'لا توجد لوحات الآن': 'No panels right now',
  'اللوحات التي تنشئها السكريبتات بـ ui.panel(...). شغّل مثالًا من تبويب المختبر لتظهر هنا.': 'Panels created by scripts with ui.panel(...). Run an example from the Lab tab to see them here.',
  'صفحة HTML مخصصة (تُحفظ على الجهاز قبل التشغيل)': 'Custom HTML page (saved to the device before running)',
  'مسار الصفحة يجب أن يكون /ui/<اسم>.html': 'Page path must be /ui/<name>.html',
  'تعذر حفظ الصفحة': 'Could not save the page', 'تعذر تحميل الصفحة': 'Could not load the page',
  'اسم السكريبت': 'Script name', 'أمثلة...': 'Examples...', 'تشغيل': 'Run', 'إيقاف الكل': 'Stop all',
  'تعذر التشغيل (تحقق من الاسم أو الحجم)': 'Cannot run (check name or size)',
  // Login
  'تسجيل الدخول': 'Sign in', 'المستخدم': 'User', 'كلمة المرور': 'Password', 'دخول': 'Sign in',
  'اسم المستخدم أو كلمة المرور غير صحيحة': 'Wrong username or password',
  'تعذر الاتصال بالجهاز': 'Cannot reach the device',
  'محاولات كثيرة، انتظر 30 ثانية': 'Too many attempts, wait 30 seconds',
  'نسيت كلمة المرور؟': 'Forgot password?',
  'إعادة ضبط كلمة المرور': 'Reset password',
  'سيُعاد ضبط كلمات المرور إلى الافتراضية. بعد الموافقة اضغط زر الجهاز الفيزيائي. هل تريد المتابعة؟':
    'Passwords will be reset to the defaults. After confirming, press the physical button on the device. Continue?',
  'اضغط زر الجهاز (GPIO {n}) خلال {s} ثانية': 'Press the device button (GPIO {n}) within {s} seconds',
  'تمت إعادة الضبط. كلمات المرور الافتراضية:': 'Reset done. Default passwords:',
  'انتهت المهلة، أعد المحاولة': 'Timed out, try again',
  'تعذر بدء إعادة الضبط (تحقق من رقم PIN في الإعدادات)': 'Could not start the reset (check the PIN number in settings)',
  // Dashboard
  'استخدام الموارد': 'Resource usage', 'الذاكرة (RAM)': 'Memory (RAM)', 'التخزين': 'Storage',
  'الشبكة': 'Network', 'الإصدار': 'Version', 'مدة التشغيل': 'Uptime', 'الإشارة': 'Signal',
  'لا توجد بيانات (قد يكون الواي فاي قيد الضبط)': 'No data (WiFi may be reconfiguring)',
  'إعادة تشغيل الجهاز': 'Restart device', 'هل تريد إعادة تشغيل الجهاز؟': 'Restart the device?',
  'يُعاد تشغيل الجهاز، سجّل الدخول من جديد...': 'Restarting, sign in again...',
  // Wifi
  'إعدادات الواي فاي': 'WiFi settings', 'الوضع': 'Mode',
  'AP (نقطة وصول)': 'AP (access point)', 'STA (اتصال بشبكة)': 'STA (join a network)',
  'اسم الشبكة (STA)': 'Network name (STA)', 'مسح': 'Scan', 'كلمة مرور الشبكة': 'Network password',
  'اسم نقطة الوصول (AP)': 'Access point name (AP)',
  'كلمة مرور نقطة الوصول (8 أحرف على الأقل)': 'Access point password (min 8 chars)',
  'حفظ وتطبيق': 'Save & apply',
  'تم الحفظ. سيُعاد ضبط الواي فاي وقد ينقطع الاتصال لحظات.': 'Saved. WiFi is being reconfigured; the connection may drop briefly.',
  'فشل الحفظ': 'Save failed',
  // Comm
  'المضيف المحلي': 'Local host', 'مضيف الإنترنت': 'Internet host',
  'رقم PIN لإعادة الضبط': 'Reset PIN (GPIO number)', 'حفظ': 'Save', 'تم الحفظ': 'Saved',
  // Passwords
  'كلمة مرور admin': 'Admin password', 'كلمة مرور user': 'User password', 'الحقلان مطلوبان': 'Both fields are required',
  // Backup
  'نسخة احتياطية': 'Backup',
  'تحفظ الإعدادات (الواي فاي، الاتصال، PIN) وكلمات المرور في ملف JSON واحد.': 'Saves the settings (WiFi, connection, PIN) and passwords in one JSON file.',
  'تنزيل النسخة الاحتياطية': 'Download backup',
  'تنبيه: الملف يحتوي كلمات المرور كنص صريح، فاحفظه في مكان آمن.': 'Note: the file contains passwords in plain text, keep it safe.',
  'استعادة': 'Restore', 'ملف النسخة الاحتياطية (.json)': 'Backup file (.json)',
  'استعادة وإعادة التشغيل': 'Restore & restart',
  'تم تنزيل النسخة الاحتياطية': 'Backup downloaded', 'فشل التنزيل: ': 'Download failed: ',
  'ستُستبدل الإعدادات وكلمات المرور الحالية بمحتوى الملف ويُعاد تشغيل الجهاز. متابعة؟': 'Current settings and passwords will be replaced and the device restarted. Continue?',
  'تمت الاستعادة. يُعاد تشغيل الجهاز، سجّل الدخول من جديد...': 'Restored. Restarting, sign in again...',
  'الملف ليس JSON صالحًا': 'The file is not valid JSON', 'فشلت الاستعادة (تحقق من الملف)': 'Restore failed (check the file)',
  // Ota
  'تحديث البرنامج (firmware)': 'Firmware update', 'الملف firmware-*.bin': 'File firmware-*.bin',
  'تحديث الواجهة (littlefs)': 'Web UI update (littlefs)', 'الملف littlefs-*.bin': 'File littlefs-*.bin',
  'رفع وتحديث': 'Upload & update',
  'تحذير: هذا يمسح الإعدادات وكلمات المرور المحفوظة ويعيدها للافتراضي.': 'Warning: this wipes the saved settings and passwords and restores the defaults.',
  'تحديث الواجهة يستبدل نظام الملفات بالكامل ويمسح الإعدادات وكلمات المرور. هل تريد المتابعة؟': 'Updating the UI replaces the whole filesystem and wipes settings and passwords. Continue?',
  'سيُحدَّث البرنامج ثم يُعاد تشغيل الجهاز. هل تريد المتابعة؟': 'The firmware will be updated and the device restarted. Continue?',
  'تم التحديث. يُعاد تشغيل الجهاز، وستحتاج إلى تسجيل الدخول من جديد...': 'Updated. The device is restarting, you will need to sign in again...',
  'فشل التحديث: ': 'Update failed: ', 'انقطع الاتصال أثناء الرفع': 'Connection lost during upload',
  // Terminal
  'عرض سجل الجهاز (Serial)': 'Live device log (Serial)', 'اكتب help لعرض الأوامر': 'Type help to list commands',
  'أمر...': 'Command...', 'إرسال': 'Send', 'مسح الشاشة': 'Clear'
};
