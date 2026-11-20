// طبقة الاتصال بالـ API. الرمز يُحفظ في sessionStorage ويُرسل في الترويسة User_Token
export const session = {
  get token() { return sessionStorage.getItem('token') || ''; },
  get role() { return sessionStorage.getItem('role') || ''; },
  set(token, role) { sessionStorage.setItem('token', token); sessionStorage.setItem('role', role); },
  clear() { sessionStorage.removeItem('token'); sessionStorage.removeItem('role'); }
};

export class ApiError extends Error {
  constructor(status) { super('HTTP ' + status); this.status = status; }
}

export async function api(path, body) {
  const opts = { headers: { User_Token: session.token, User_Role: session.role } };
  if (body !== undefined) {
    opts.method = 'POST';
    opts.headers['Content-Type'] = 'application/json';
    opts.body = JSON.stringify(body);
  }
  const res = await fetch(path, opts);
  if (res.status === 401 && path !== '/login' && !path.startsWith('/settings/get_stat') && !path.startsWith('/settings/scan_wifi')) {
    session.clear();
    location.reload();
  }
  if (!res.ok) throw new ApiError(res.status);
  const text = await res.text();
  return text ? JSON.parse(text) : null;
}
