const defaultHeaders = { 'Content-Type': 'application/json' };

export async function apiGet(path) {
  const resp = await fetch(path, { credentials: 'include' });
  if (!resp.ok) throw new Error((await resp.json()).error || resp.statusText);
  return resp.json();
}

export async function apiPost(path, body) {
  const resp = await fetch(path, {
    method: 'POST',
    headers: defaultHeaders,
    credentials: 'include',
    body: JSON.stringify(body ?? {})
  });
  if (!resp.ok) throw new Error((await resp.json()).error || resp.statusText);
  return resp.json();
}
