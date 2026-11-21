#ifndef EM_H
#define EM_H

#include <Arduino.h>

static const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="ar" dir="rtl">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>ESP32 Pro Manager</title>
    <style>
        :root { --bg: #0a0a0f; --panel: #16161e; --primary: #4f46e5; --danger: #ef4444; --success: #10b981; --warning: #f59e0b; --text: #f1f5f9; --border: rgba(255,255,255,0.1); }
        * { box-sizing: border-box; margin: 0; padding: 0; }
        body { font-family: system-ui, -apple-system, sans-serif; background: var(--bg); color: var(--text); padding: 15px; direction: rtl; }
        .container { max-width: 1200px; margin: 0 auto; }

        /* Glass Panel */
        .glass { background: var(--panel); border: 1px solid var(--border); border-radius: 20px; padding: 20px; margin-bottom: 20px; box-shadow: 0 10px 30px rgba(0,0,0,0.5); }

        /* Header & Buttons */
        .header { display: flex; flex-wrap: wrap; justify-content: space-between; align-items: center; gap: 15px; }
        .btn-group { display: flex; flex-wrap: wrap; gap: 8px; }
        .btn { border: none; border-radius: 10px; padding: 10px 15px; font-weight: bold; cursor: pointer; display: inline-flex; align-items: center; gap: 8px; font-size: 12px; transition: 0.2s; color: white; text-decoration: none; }

        .btn-indigo { background: var(--primary); } .btn-indigo:hover { background: #4338ca; }
        .btn-emerald { background: var(--success); } .btn-emerald:hover { background: #059669; }
        .btn-red { background: var(--danger); } .btn-red:hover { background: #dc2626; }
        .btn-amber { background: var(--warning); color: #000; } .btn-amber:hover { background: #d97706; }
        .btn-ghost { background: rgba(255,255,255,0.05); color: #94a3b8; } .btn-ghost:hover { background: rgba(255,255,255,0.1); color: #fff; }

        .icon { width: 18px; height: 18px; fill: currentColor; }

        /* Path Bar */
        .path-bar { background: rgba(255,255,255,0.03); padding: 10px 20px; border-radius: 50px; font-size: 13px; font-weight: bold; color: var(--primary); margin-bottom: 15px; display: inline-block; }

        /* Table */
        table { width: 100%; border-collapse: collapse; margin-top: 10px; }
        th { text-align: right; padding: 15px; font-size: 11px; color: #64748b; border-bottom: 2px solid var(--border); }
        td { padding: 12px 15px; border-bottom: 1px solid var(--border); }
        .file-row:hover { background: rgba(255,255,255,0.02); }
        .file-item { display: flex; align-items: center; gap: 12px; cursor: pointer; }
        .actions { display: flex; gap: 10px; justify-content: flex-end; }

        /* Modal Editor */
        .modal { position: fixed; inset: 0; background: rgba(0,0,0,0.95); z-index: 1000; display: none; flex-direction: column; padding: 20px; }
        .modal-box { background: #111118; border: 1px solid var(--border); border-radius: 25px; width: 100%; max-width: 1100px; margin: auto; height: 90vh; display: flex; flex-direction: column; }
        .modal-header { padding: 20px; border-bottom: 1px solid var(--border); display: flex; justify-content: space-between; align-items: center; }
        textarea { flex: 1; background: #000; color: #10b981; border: none; padding: 20px; font-family: 'Courier New', monospace; font-size: 14px; resize: none; outline: none; }

        .hidden { display: none; }
        @media (max-width: 600px) { .header { flex-direction: column; align-items: stretch; } .btn { flex: 1; justify-content: center; } }
    </style>
</head>
<body>

    <div class="container">
        <!-- Header Section -->
        <div class="glass header">
            <div style="display:flex; align-items:center; gap:12px;">
                <div style="background:var(--primary); padding:10px; border-radius:12px; color:white;">
                    <svg class="icon" viewBox="0 0 24 24"><path d="M12,15.5A2.5,2.5 0 0,1 9.5,13A2.5,2.5 0 0,1 12,10.5A2.5,2.5 0 0,1 14.5,13A2.5,2.5 0 0,1 12,15.5M19.43,12.97C19.47,12.65 19.5,12.33 19.5,12C19.5,11.67 19.47,11.34 19.43,11.03L21.54,9.37C21.73,9.22 21.78,8.95 21.66,8.73L19.66,5.27C19.54,5.05 19.27,4.96 19.05,5.05L16.56,6.05C16.04,5.66 15.47,5.34 14.86,5.08L14.47,2.44C14.43,2.19 14.22,2 13.97,2H9.97C9.72,2 9.5,2.19 9.47,2.44L9.08,5.08C8.47,5.34 7.9,5.66 7.38,6.05L4.89,5.05C4.67,4.96 4.4,5.05 4.28,5.27L2.28,8.73C2.16,8.95 2.21,9.22 2.4,9.37L4.51,11.03C4.47,12.65 4.5,12.33 4.5,12C4.5,11.67 4.47,11.34 4.51,11.03L2.4,12.63C2.21,12.78 2.16,13.05 2.28,13.27L4.28,16.73C4.4,16.95 4.67,17.04 4.89,16.95L7.38,15.95C7.9,16.34 8.47,16.66 9.08,16.92L9.47,19.56C9.5,19.81 9.72,20 9.97,20H13.97C14.22,20 14.43,19.81 14.47,19.56L14.86,16.92C15.47,16.66 16.04,16.34 16.56,15.95L19.05,16.95C19.27,17.04 19.54,16.95 19.66,16.73L21.66,13.27C21.78,13.05 21.73,12.78 21.54,12.63L19.43,12.97Z"/></svg>
                </div>
                <div>
                    <h2 style="font-size:16px;">مدير الملفات</h2>
                    <p id="storage-txt" style="font-size:10px; color:#64748b; font-family:monospace;"></p>
                </div>
            </div>

            <div class="btn-group">
                <input type="file" id="u-input" class="hidden" onchange="uploadFile()">
                <button class="btn btn-emerald" onclick="document.getElementById('u-input').click()">
                    <svg class="icon" viewBox="0 0 24 24"><path d="M9,16V10H5L12,3L19,10H15V16H9M5,20V18H19V20H5Z"/></svg> رفع
                </button>
                <button class="btn btn-indigo" onclick="createNewFile()">
                    <svg class="icon" viewBox="0 0 24 24"><path d="M13,9V3.5L18.5,9M6,2C4.89,2 4,2.89 4,4V20A2,2 0 0,0 6,22H18A2,2 0 0,0 20,20V8L14,2H6Z"/></svg> ملف
                </button>
                <button class="btn btn-indigo" onclick="createNewDir()">
                    <svg class="icon" viewBox="0 0 24 24"><path d="M10,4L12,6H20A2,2 0 0,1 22,8V18A2,2 0 0,1 20,20H4C2.89,20 2,19.1 2,18V6C2,4.89 2.89,4 4,4H10M15,9V12H12V14H15V17H17V14H20V12H17V9H15Z"/></svg> مجلد
                </button>
                <button class="btn btn-red" onclick="formatFS()">
                    <svg class="icon" viewBox="0 0 24 24"><path d="M20.37,8.91L19.37,10.64L15.03,8.14L16.03,6.4L18.53,7.85L17.53,6.12L15.8,7.12L14.8,5.38L20,2.38L21,4.11L19.27,5.11L20.27,6.85L22.77,5.4L23.77,7.13L18.57,10.13L20.37,8.91M6,19A2,2 0 0,0 8,21H16A2,2 0 0,0 18,19V7H6V19Z"/></svg> تهيئة
                </button>
                <input type="file" id="ota-input" class="hidden" accept=".bin" onchange="otaFs()">
                <button class="btn btn-amber" onclick="document.getElementById('ota-input').click()">
                    <svg class="icon" viewBox="0 0 24 24"><path d="M12,2L4.5,20.29L5.21,21L12,18L18.79,21L19.5,20.29L12,2Z"/></svg> تحديث الواجهة
                </button>
                <button class="btn btn-ghost" onclick="logout()">
                    <svg class="icon" viewBox="0 0 24 24"><path d="M16,17V14H9V10H16V7L21,12L16,17M14,2A2,2 0 0,1 16,4V6H14V4H5V20H14V18H16V20A2,2 0 0,1 14,22H5A2,2 0 0,1 3,20V4A2,2 0 0,1 5,2H14Z"/></svg> خروج
                </button>
            </div>
        </div>

        <!-- Breadcrumbs -->
        <div id="breadcrumbs" class="path-bar"></div>

        <!-- File List -->
        <div class="glass" style="padding:0; overflow-x:auto;">
            <table>
                <thead>
                    <tr>
                        <th style="width:50%">الاسم</th>
                        <th style="text-align:center">الحجم</th>
                        <th style="text-align:center">الإجراءات</th>
                    </tr>
                </thead>
                <tbody id="file-list"></tbody>
            </table>
        </div>
    </div>

    <!-- Modal Editor -->
    <div id="editor-modal" class="modal">
        <div class="modal-box">
            <div class="modal-header">
                <span id="editor-title" style="color:var(--primary); font-family:monospace;"></span>
                <button class="btn btn-ghost" onclick="closeEditor()">إغلاق ✕</button>
            </div>
            <textarea id="editor-content" spellcheck="false"></textarea>
            <div style="padding:15px; border-top:1px solid var(--border); display:flex; justify-content:flex-end; gap:10px;">
                <button class="btn btn-indigo" onclick="saveFile()" style="padding:12px 40px;">حفظ الملف</button>
            </div>
        </div>
    </div>

    <script>
        let currentPath = "/";

        // SVGs as strings for offline use
        const svgDir = '<svg class="icon" style="color:var(--warning)" viewBox="0 0 24 24"><path d="M10,4L12,6H20A2,2 0 0,1 22,8V18A2,2 0 0,1 20,20H4C2.89,20 2,19.1 2,18V6C2,4.89 2.89,4 4,4H10Z"/></svg>';
        const svgFile = '<svg class="icon" style="color:var(--primary)" viewBox="0 0 24 24"><path d="M13,9V3.5L18.5,9M6,2C4.89,2 4,2.89 4,4V20A2,2 0 0,0 6,22H18A2,2 0 0,0 20,20V8L14,2H6Z"/></svg>';
        const svgDL = '<svg class="icon" viewBox="0 0 24 24"><path d="M5,20H19V18H5M19,9H15V3H9V9H5L12,16L19,9Z"/></svg>';
        const svgEdit = '<svg class="icon" viewBox="0 0 24 24"><path d="M20.71,7.04C21.1,6.65 21.1,6.02 20.71,5.63L18.37,3.29C17.97,2.9 17.34,2.9 16.95,3.29L15.12,5.12L18.87,8.87M3,17.25V21H6.75L17.81,9.93L14.06,6.18L3,17.25Z"/></svg>';
        const svgDel = '<svg class="icon" viewBox="0 0 24 24"><path d="M19,4H15.5L14.5,3H9.5L8.5,4H5V6H19V4M6,19A2,2 0 0,0 8,21H16A2,2 0 0,0 18,19V7H6V19Z"/></svg>';

        async function request(url, method = 'GET', body = null) {
            const res = await fetch(url, { method, body });
            if (res.status === 404) { window.location.href="/em"; return null; }
            return res;
        }

        async function loadFiles(path = "/") {
            currentPath = path;
            const res = await request(`/api/list?path=${encodeURIComponent(path)}`);
            if (!res) return;
            const data = await res.json();

            document.getElementById('storage-txt').innerText = `USED: ${(data.used/1024).toFixed(1)}KB / TOTAL: ${(data.total/1024).toFixed(1)}KB`;

            const tbody = document.getElementById('file-list');
            tbody.innerHTML = '';

            updateBreadcrumbs();

            if (path !== "/") {
                const parent = path.substring(0, path.lastIndexOf('/')) || "/";
                addFileRow({name: "..", isDir: true, size: 0}, true, parent);
            }

            data.files.forEach(file => addFileRow(file));
        }

        function addFileRow(file, isBack = false, backPath = "") {
            const tbody = document.getElementById('file-list');
            const row = document.createElement('tr');
            row.className = "file-row";
            const icon = file.isDir ? svgDir : svgFile;
            const fullPath = isBack ? backPath : (currentPath === "/" ? "" : currentPath) + "/" + file.name;

            row.innerHTML = `
                <td>
                    <div class="file-item" onclick="${file.isDir ? `loadFiles('${fullPath}')` : `editFile('${fullPath}')`}">
                        ${icon} <span>${file.name}</span>
                    </div>
                </td>
                <td style="text-align:center; color:#64748b; font-size:11px; font-family:monospace;">${file.isDir ? '--' : (file.size/1024).toFixed(1) + ' KB'}</td>
                <td>
                    <div class="actions ${isBack ? 'hidden' : ''}">
                        <a href="/api/download?path=${encodeURIComponent(fullPath)}" style="color:var(--success)">${svgDL}</a>
                        <button class="btn btn-ghost" onclick="renameItem('${fullPath}')" style="color:var(--warning)">${svgEdit}</button>
                        <button class="btn btn-ghost" onclick="deleteItem('${fullPath}')" style="color:var(--danger)">${svgDel}</button>
                    </div>
                </td>
            `;
            tbody.appendChild(row);
        }

        function updateBreadcrumbs() {
            const parts = currentPath.split('/').filter(p => p);
            let html = `<span style="cursor:pointer;" onclick="loadFiles('/')">الرئيسية</span>`;
            let acc = "";
            parts.forEach(p => { acc += "/" + p; html += ` <span style="color:#334155; margin:0 5px;">/</span> <span style="cursor:pointer;" onclick="loadFiles('${acc}')">${p}</span>`; });
            document.getElementById('breadcrumbs').innerHTML = html;
        }

        async function uploadFile() {
            const file = document.getElementById('u-input').files[0];
            if (!file) return;
            const formData = new FormData();
            formData.append("file", file, (currentPath==="/"?"":currentPath)+"/"+file.name);
            await fetch('/api/upload', { method: 'POST', body: formData });
            loadFiles(currentPath);
        }

        async function createNewFile() {
            const n = prompt("اسم الملف:");
            if (n) {
                const path = (currentPath==="/"?"":currentPath)+"/"+n;
                document.getElementById('editor-title').innerText = path;
                document.getElementById('editor-content').value = "";
                document.getElementById('editor-modal').style.display = 'flex';
            }
        }

        async function createNewDir() {
            const n = prompt("اسم المجلد:");
            if (n) {
                const path = (currentPath==="/"?"":currentPath)+"/"+n;
                await request(`/api/mkdir?path=${encodeURIComponent(path)}`, 'POST');
                loadFiles(currentPath);
            }
        }

        async function editFile(path) {
            const res = await request(`/api/read?path=${encodeURIComponent(path)}`);
            if (!res) return;
            document.getElementById('editor-content').value = await res.text();
            document.getElementById('editor-title').innerText = path;
            document.getElementById('editor-modal').style.display = 'flex';
        }

        async function saveFile() {
            const formData = new FormData();
            formData.append('content', document.getElementById('editor-content').value);
            await request(`/api/save?path=${encodeURIComponent(document.getElementById('editor-title').innerText)}`, 'POST', formData);
            closeEditor(); loadFiles(currentPath);
        }

        async function deleteItem(path) {
            if (confirm("حذف نهائي؟")) { await request(`/api/delete?path=${encodeURIComponent(path)}`, 'DELETE'); loadFiles(currentPath); }
        }

        async function renameItem(path) {
            const n = prompt("الاسم/المسار الجديد:", path);
            if (n) { await request(`/api/rename?old=${encodeURIComponent(path)}&new=${encodeURIComponent(n)}`); loadFiles(currentPath); }
        }

        async function formatFS() {
            if (confirm("تحذير: سيتم مسح الذاكرة بالكامل!")) { await request('/api/format', 'POST'); location.reload(); }
        }

        // تحديث الواجهة (littlefs.bin) عبر OTA بمفتاح الطوارئ
        function otaFs() {
            const inp = document.getElementById('ota-input');
            const f = inp.files[0];
            if (!f) return;
            if (!confirm("تحديث الواجهة يستبدل نظام الملفات بالكامل ويمسح الإعدادات وكلمات المرور. متابعة؟")) { inp.value = ''; return; }
            const st = document.getElementById('storage-txt');
            const x = new XMLHttpRequest();
            x.open('POST', '/api/ota?type=fs');
            x.upload.onprogress = e => { if (e.lengthComputable) st.innerText = 'OTA: ' + Math.round(e.loaded / e.total * 100) + '%'; };
            x.onload = () => {
                if (x.status === 200) { st.innerText = 'تم التحديث، يُعاد تشغيل الجهاز...'; setTimeout(() => location.reload(), 12000); }
                else { alert('فشل التحديث: ' + x.responseText); inp.value = ''; loadFiles(currentPath); }
            };
            x.onerror = () => { alert('انقطع الاتصال'); inp.value = ''; };
            const fd = new FormData(); fd.append('file', f, f.name); x.send(fd);
        }

        function logout() { document.cookie = "session_key=; Max-Age=0; path=/;"; location.href = "/"; }
        function closeEditor() { document.getElementById('editor-modal').style.display = 'none'; }
        window.onload = () => loadFiles('/');
    </script>
</body>
</html>
)rawliteral";

// نافذة إدخال مفتاح الطوارئ: تظهر عند أول دخول أو عند فقدان /app/index.html
static const char login_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="ar" dir="rtl">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>ESP32 - دخول الطوارئ</title>
    <style>
        * { box-sizing: border-box; margin: 0; padding: 0; }
        body { font-family: system-ui, -apple-system, sans-serif; background: #0a0a0f; color: #f1f5f9; min-height: 100vh; display: flex; align-items: center; justify-content: center; padding: 15px; }
        .card { background: #16161e; border: 1px solid rgba(255,255,255,0.1); border-radius: 20px; padding: 25px; width: 100%; max-width: 340px; box-shadow: 0 10px 30px rgba(0,0,0,0.5); text-align: center; }
        h2 { font-size: 16px; margin-bottom: 6px; }
        p { font-size: 12px; color: #94a3b8; margin-bottom: 18px; line-height: 1.6; }
        input { width: 100%; padding: 12px; border-radius: 10px; border: 1px solid rgba(255,255,255,0.15); background: #000; color: #10b981; font-size: 16px; text-align: center; letter-spacing: 3px; outline: none; margin-bottom: 12px; }
        input:focus { border-color: #4f46e5; }
        button { width: 100%; padding: 12px; border: none; border-radius: 10px; background: #4f46e5; color: #fff; font-weight: bold; font-size: 14px; cursor: pointer; }
        button:disabled { opacity: 0.5; }
        #msg { min-height: 18px; margin-top: 12px; font-size: 12px; color: #ef4444; }
    </style>
</head>
<body>
    <div class="card">
        <h2>نافذة الطوارئ</h2>
        <p>أدخل المفتاح السري للدخول إلى مدير الملفات.</p>
        <input id="k" type="password" placeholder="••••" autocomplete="off" autofocus>
        <button id="b" onclick="go()">دخول</button>
        <div id="msg"></div>
    </div>
    <script>
        const k = document.getElementById('k'), b = document.getElementById('b'), m = document.getElementById('msg');
        k.addEventListener('keydown', e => { if (e.key === 'Enter') go(); });
        async function go() {
            if (!k.value) return;
            b.disabled = true; m.innerText = '';
            try {
                const r = await fetch('/em/check?pass=' + encodeURIComponent(k.value));
                if (r.ok) { location.href = '/em'; return; }
                m.innerText = r.status === 429 ? 'محاولات كثيرة، انتظر دقيقة ثم أعد المحاولة' : 'المفتاح غير صحيح';
            } catch (e) { m.innerText = 'تعذر الاتصال بالجهاز'; }
            b.disabled = false; k.value = ''; k.focus();
        }
    </script>
</body>
</html>
)rawliteral";

#endif
