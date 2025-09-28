const API_BASE = '/api';

async function request(path, { method = 'GET', headers = {}, body, timeoutMs = 8000 } = {}) {
  const controller = new AbortController();
  const timeout = setTimeout(() => controller.abort(), timeoutMs);

  try {
    const finalHeaders = { ...headers };
    if (body && !finalHeaders['Content-Type']) {
      finalHeaders['Content-Type'] = 'application/json';
    }

    const response = await fetch(`${API_BASE}${path}`, {
      method,
      headers: finalHeaders,
      body,
      signal: controller.signal,
    });

    if (!response.ok) {
      const text = await response.text();
      throw new Error(`Request failed (${response.status}): ${text || response.statusText}`);
    }

    if (response.status === 204) {
      return null;
    }

    const contentType = response.headers.get('content-type') || '';
    if (contentType.includes('application/json')) {
      return response.json();
    }

    return response.text();
  } finally {
    clearTimeout(timeout);
  }
}

export function fetchSystemStatus() {
  return request('/system');
}

export function fetchConfig() {
  return request('/config');
}

export function fetchSensorReadings() {
  return request('/sensors/readings');
}

export function triggerTimeSync() {
  return request('/time/sync', { method: 'POST' });
}

export function fetchTimeStatus() {
  return request('/time/status');
}

export function fetchPendingNotifications() {
  return request('/sd/pending_notifications');
}

export function fetchTagMetadata() {
  return request('/tags');
}

export function saveTagMetadata(data) {
  return request('/tags', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(data),
  });
}
