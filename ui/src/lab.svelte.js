import { examples } from './examples.js';

// حالة المختبر على مستوى الوحدة: لا تضيع عند التنقل بين التبويبات، وتُحفظ أيضًا في المتصفح
const KEY = 'lab-state';
function read() { try { return JSON.parse(localStorage.getItem(KEY)) || {}; } catch { return {}; } }
const s = read();

export const lab = $state({
  name: s.name || 'lab',
  code: typeof s.code === 'string' ? s.code : examples['Blink (GPIO8)'].code,
  showPage: !!s.showPage,
  pagePath: s.pagePath || '/ui/page.html',
  pageHtml: s.pageHtml || '',
  out: '',          // الطرفية الوسيطة
  seq: 0,
  list: [],         // السكريبتات العاملة
  files: []         // الملفات المحفوظة على الجهاز
});

export function persistLab() {
  try {
    localStorage.setItem(KEY, JSON.stringify({ name: lab.name, code: lab.code, showPage: lab.showPage, pagePath: lab.pagePath, pageHtml: lab.pageHtml }));
  } catch {}
}
