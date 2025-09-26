// ES module for Press-32 client
// Import modern Alpine.js and the Alpine Router plugin (CDN, module builds)
import Alpine from 'https://cdn.jsdelivr.net/npm/alpinejs@3/dist/module.esm.js';
window.Alpine = Alpine;

// If a CDN script provided a global router plugin (e.g. @shaun/alpinejs-router),
// try to register it. Try a few common global names used by UMD builds.
const routerPlugin = (typeof window !== 'undefined') && (
  window.AlpineRouter || window.AlpineRouterPlugin || window.alpineRouter || window.Router || window.router
);
if (routerPlugin) {
  Alpine.plugin(routerPlugin);
} else {
  console.info('No global Alpine router plugin detected; continuing without router plugin.');
}

// Do not start Alpine here — start after we wire `clientApp` so component state
// (including `route`) exists before Alpine initializes.

const deviceInput = document.getElementById('deviceIp');
const saveBtn = document.getElementById('saveBtn');
const testBtn = document.getElementById('testBtn');
const currentConfig = document.getElementById('currentConfig');
const refreshBtn = document.getElementById('refreshBtn');
const output = document.getElementById('output');
const status = document.getElementById('status');
const autoRefresh = document.getElementById('autoRefresh');

const STORAGE_KEY = 'press32:device_host';
let autoTimer = null;

function setStatus(s) { status.textContent = s; }

function loadConfig() {
  const host = localStorage.getItem(STORAGE_KEY);
  if (host) {
    deviceInput.value = host;
    currentConfig.textContent = 'Configured device: ' + host;
  } else {
    currentConfig.textContent = 'No device configured.';
  }
}

function saveConfig() {
  const v = deviceInput.value.trim();
  if (!v) return alert('Enter a device IP or host');
  localStorage.setItem(STORAGE_KEY, v);
  loadConfig();
}

async function testFetch() {
  const host = localStorage.getItem(STORAGE_KEY) || deviceInput.value.trim();
  if (!host) return alert('Set device IP first');
  const url = host.startsWith('http') ? host : ('http://' + host);
  try {
    setStatus('Fetching...');
    const r = await fetch(url + '/sensors/readings', { cache: 'no-store' });
    if (!r.ok) throw new Error('HTTP ' + r.status);
    const j = await r.json();
    renderReadings(j);
    output.textContent = JSON.stringify(j, null, 2);
    setStatus('Last fetch: ' + new Date().toLocaleString());
  } catch (e) {
    output.textContent = 'Error: ' + e.message;
    setStatus('Error');
  }
}

saveBtn.addEventListener('click', () => { saveConfig(); });
testBtn.addEventListener('click', () => { saveConfig(); testFetch(); });
refreshBtn.addEventListener('click', () => { testFetch(); });

autoRefresh.addEventListener('change', () => {
  if (autoTimer) { clearInterval(autoTimer); autoTimer = null; }
  const v = Number(autoRefresh.value);
  if (v > 0) {
    autoTimer = setInterval(testFetch, v * 1000);
  }
});

// Initialize
loadConfig();
const savedInterval = localStorage.getItem('press32:auto_refresh');
if (savedInterval) {
  autoRefresh.value = savedInterval;
  autoRefresh.dispatchEvent(new Event('change'));
}
autoRefresh.addEventListener('change', () => {
  localStorage.setItem('press32:auto_refresh', autoRefresh.value);
});

function renderReadings(payload) {
  const tsEl = document.getElementById('ts');
  const rtuEl = document.getElementById('rtu');
  const tagsBody = document.getElementById('tagsBody');
  tsEl.textContent = payload.timestamp || payload.time || '-';
  rtuEl.textContent = payload.rtu || '-';
  tagsBody.innerHTML = '';
  const tags = Array.isArray(payload.tags) ? payload.tags : (payload.tags || []);
  for (const t of tags) {
    const tr = document.createElement('tr');
    tr.innerHTML = `
      <td class="p-2 border-t">${t.id ?? '-'}</td>
      <td class="p-2 border-t">${t.port ?? '-'}</td>
      <td class="p-2 border-t">${t.source ?? '-'}</td>
      <td class="p-2 border-t">${(t.value && t.value.scaled && typeof t.value.scaled.value !== 'undefined') ? t.value.scaled.value : '-'}</td>
      <td class="p-2 border-t">${(t.value && t.value.converted && typeof t.value.converted.from_filtered !== 'undefined') ? t.value.converted.from_filtered : (t.value && t.value.converted && typeof t.value.converted.from_raw !== 'undefined' ? t.value.converted.from_raw : '-')}</td>
    `;
    tagsBody.appendChild(tr);
  }
}

// If Alpine component factory exists on window, wire methods so x-data can call them
if (window.clientApp) {
  const originalFactory = window.clientApp;
  window.clientApp = function() {
    const state = originalFactory();
    // Map functions
    state.saveConfig = saveConfig;
    state.testFetch = testFetch;
    state.onAutoChange = function() {
      if (autoTimer) { clearInterval(autoTimer); autoTimer = null; }
      const v = Number(state.autoInterval || 0);
      if (v > 0) autoTimer = setInterval(testFetch, v * 1000);
      localStorage.setItem('press32:auto_refresh', state.autoInterval);
    };
    // Reactive routing state
    state.route = location.hash.replace('#', '') || '/readings';
    // Keep a global setter for fallback routing to update Alpine state
    window._setAlpineRoute = (r) => { state.route = r; };
    // Reflect simple reactive fields where possible
    state.hostInput = localStorage.getItem(STORAGE_KEY) || '';
    state.rawJson = '';
    state.statusText = 'Idle';
    // Wrap testFetch to update Alpine state fields
    const originalTest = testFetch;
    state.testFetch = async function() {
      try {
        state.statusText = 'Fetching...';
        await originalTest();
        state.rawJson = output.textContent;
        state.statusText = status.textContent;
      } catch (err) {
        // Surface error message to UI
        state.rawJson = output.textContent || ('Error: ' + (err && err.message ? err.message : String(err)));
        state.statusText = 'Error';
        console.warn('testFetch error', err);
      }
    };
    return state;
  }
}

export { };

  // --- Routing and view helpers ---
  function setupRouting() {
    // If Alpine Router plugin registered, use $router; else use simple hash routing
    if (Alpine && Alpine.router) {
      Alpine.router(function (router) {
        router.add('/readings', () => { /* nothing: view shows automatically */ });
        router.add('/config', async () => { await loadConfigView(); });
        router.add('/calibration', async () => { await loadCalView(); });
        router.add('/utils', async () => { await loadUtilsView(); });
        router.start();
      });
    } else {
      // Basic hash routing fallback
      function handleHash() {
        const route = location.hash.replace('#', '') || '/readings';
        if (window._setAlpineRoute) window._setAlpineRoute(route);
        if (route === '/config') loadConfigView();
        if (route === '/calibration') loadCalView();
        if (route === '/utils') loadUtilsView();
      }
      window.addEventListener('hashchange', handleHash);
      handleHash();
    }
  }

  async function loadConfigView() {
    const out = document.getElementById('configView');
    out.textContent = 'Fetching config...';
    try {
      const host = localStorage.getItem(STORAGE_KEY) || '';
      if (!host) { out.textContent = 'No device configured.'; return; }
      // For per-tag (sensor) configuration use the /sensors/config endpoint
      const url = (host.startsWith('http') ? host : ('http://' + host)) + '/sensors/config';
      const r = await fetch(url, { cache: 'no-store' });
      if (!r.ok) throw new Error('HTTP ' + r.status);
      const j = await r.json();
      renderSensorsConfigView(out, j, url);
    } catch (err) {
      out.textContent = 'Error: ' + (err && err.message ? err.message : String(err));
    }
  }

  async function loadCalView() {
    const out = document.getElementById('calView');
    out.textContent = 'Fetching calibration...';
    try {
      const host = localStorage.getItem(STORAGE_KEY) || '';
      if (!host) { out.textContent = 'No device configured.'; return; }
      // Use /calibrate/all to list all per-pin calibrations
      const url = (host.startsWith('http') ? host : ('http://' + host)) + '/calibrate/all';
      const r = await fetch(url, { cache: 'no-store' });
      if (!r.ok) throw new Error('HTTP ' + r.status);
      const j = await r.json();
      out.innerHTML = '<h3 class="font-medium mb-2">Per-Tag Calibration</h3>';
      renderCalibrationList(out, j, host);
    } catch (err) {
      out.textContent = 'Error: ' + (err && err.message ? err.message : String(err));
    }
  }

  function renderConfigForm(containerEl, obj, endpoint, originalObj) {
    // Build form for top-level primitive fields, nested objects as JSON textarea
    const form = document.createElement('div');
    form.className = 'space-y-2';
    const inputs = {};
    for (const key of Object.keys(obj)) {
      const val = obj[key];
      const id = 'cfg_' + key;
      const row = document.createElement('div');
      row.className = 'flex gap-2 items-start';
      const label = document.createElement('label');
      label.className = 'w-40 text-sm text-gray-700';
      label.textContent = key;
      row.appendChild(label);
      if (val === null || ['string','number','boolean'].includes(typeof val)) {
        const input = document.createElement('input');
        input.id = id;
        input.className = 'flex-1 border rounded px-2 py-1';
        input.value = (val === null) ? '' : String(val);
        row.appendChild(input);
        inputs[key] = { type: typeof val, el: input };
      } else {
        const ta = document.createElement('textarea');
        ta.id = id;
        ta.className = 'flex-1 border rounded px-2 py-1 h-32';
        ta.value = JSON.stringify(val, null, 2);
        row.appendChild(ta);
        inputs[key] = { type: 'json', el: ta };
      }
      form.appendChild(row);
    }
    const btnRow = document.createElement('div');
    btnRow.className = 'flex gap-2';
    const saveBtn = document.createElement('button');
    saveBtn.className = 'bg-green-600 text-white px-3 py-1 rounded';
    saveBtn.textContent = 'Save Config';
    const status = document.createElement('div');
    status.className = 'text-sm text-gray-600';
  saveBtn.addEventListener('click', async () => {
  // build new object
  const newObj = Object.assign({}, obj);
      for (const k of Object.keys(inputs)) {
        const info = inputs[k];
        const raw = info.el.value;
        if (info.type === 'json') {
          try {
            newObj[k] = JSON.parse(raw);
          } catch (e) {
            status.textContent = 'Invalid JSON in ' + k;
            console.warn('Invalid JSON in config field', k, e);
            return;
          }
        } else if (info.type === 'number') {
          newObj[k] = Number(raw);
        } else if (info.type === 'boolean') {
          newObj[k] = (raw === 'true' || raw === '1');
        } else {
          newObj[k] = raw;
        }
      }
      status.textContent = 'Saving...';
      try {
        // Send only changed keys compared to originalObj (if provided)
        let payload = newObj;
        if (originalObj) {
          payload = diffObjects(originalObj, newObj);
        }
        let r = await fetch(endpoint, { method: 'PUT', headers: { 'Content-Type':'application/json' }, body: JSON.stringify(payload) });
        if (r.status === 405) {
          r = await fetch(endpoint, { method: 'POST', headers: { 'Content-Type':'application/json' }, body: JSON.stringify(payload) });
        }
        if (!r.ok) throw new Error('HTTP ' + r.status);
        status.textContent = 'Saved';
      } catch (e) {
        status.textContent = 'Save failed: ' + (e && e.message ? e.message : String(e));
      }
    });
    btnRow.appendChild(saveBtn);
    btnRow.appendChild(status);
    form.appendChild(btnRow);
    containerEl.innerHTML = '';
    containerEl.appendChild(form);
  }

  function renderCalibrationForm(containerEl, obj, endpoint) {
    // Similar to config form but highlight numeric fields
    const form = document.createElement('div');
    form.className = 'space-y-2';
    const inputs = {};
    function walk(prefix, o) {
      for (const k of Object.keys(o)) {
        const v = o[k];
        const path = prefix ? (prefix + '.' + k) : k;
        if (v !== null && typeof v === 'object') {
          // nested group header
          const hdr = document.createElement('div'); hdr.className = 'text-sm font-medium mt-2'; hdr.textContent = path; form.appendChild(hdr);
          walk(path, v);
        } else {
          const row = document.createElement('div'); row.className = 'flex gap-2 items-start';
          const label = document.createElement('label'); label.className = 'w-56 text-sm text-gray-700'; label.textContent = path; row.appendChild(label);
          const input = document.createElement('input'); input.className = 'flex-1 border rounded px-2 py-1'; input.value = (v === null) ? '' : String(v);
          row.appendChild(input);
          form.appendChild(row);
          inputs[path] = { el: input, originalPath: path };
        }
      }
    }
    walk('', obj);
    const btnRow = document.createElement('div'); btnRow.className = 'flex gap-2';
    const saveBtn = document.createElement('button'); saveBtn.className = 'bg-green-600 text-white px-3 py-1 rounded'; saveBtn.textContent = 'Save Calibration';
    const status = document.createElement('div'); status.className = 'text-sm text-gray-600';
    saveBtn.addEventListener('click', async () => {
      // build updated object by cloning and setting values by path
      const newObj = JSON.parse(JSON.stringify(obj));
      for (const p of Object.keys(inputs)) {
        const info = inputs[p];
        const raw = info.el.value;
        // navigate into newObj by path
        const parts = p.split('.');
        let cur = newObj;
        for (let i = 0; i < parts.length - 1; i++) {
          const part = parts[i];
          if (!(part in cur)) cur[part] = {};
          cur = cur[part];
        }
        const last = parts[parts.length - 1];
        // try numeric conversion if original looks numeric
        const orig = getByPath(obj, p);
        if (typeof orig === 'number') {
          cur[last] = Number(raw);
        } else if (typeof orig === 'boolean') {
          cur[last] = (raw === 'true' || raw === '1');
        } else {
          cur[last] = raw;
        }
      }
      status.textContent = 'Saving...';
      try {
        // Attempt PUT then fall back to POST
        let r = await fetch(endpoint, { method: 'PUT', headers: { 'Content-Type':'application/json' }, body: JSON.stringify(newObj) });
        if (r.status === 405 || r.status === 404) {
          r = await fetch(endpoint, { method: 'POST', headers: { 'Content-Type':'application/json' }, body: JSON.stringify(newObj) });
        }
        if (!r.ok) throw new Error('HTTP ' + r.status);
        status.textContent = 'Saved';
      } catch (e) {
        status.textContent = 'Save failed: ' + (e && e.message ? e.message : String(e));
      }
    });
  // Render a list of calibrations and allow editing a specific pin via /calibrate/pin
  function renderCalibrationList(containerEl, allObj, host) {
    containerEl.innerHTML = '';
    const table = document.createElement('table');
    table.className = 'w-full text-sm table-auto border-collapse';
    const thead = document.createElement('thead');
    thead.innerHTML = '<tr><th class="p-2">Sensor</th><th class="p-2">Pin</th><th class="p-2">Zero Raw</th><th class="p-2">Span Raw</th><th class="p-2">Zero Pressure</th><th class="p-2">Span Pressure</th><th class="p-2">Actions</th></tr>';
    table.appendChild(thead);
    const tbody = document.createElement('tbody');
    // allObj is an object keyed by index (from server implementation)
    const keys = Object.keys(allObj).sort((a,b)=>Number(a)-Number(b));
    for (const k of keys) {
      const item = allObj[k];
      const tr = document.createElement('tr');
      tr.innerHTML = `
  <td class="p-2 border-t">${item.tag || ('AI' + (item.pin_index !== undefined ? (item.pin_index+1) : k))}</td>
          <td class="p-2 border-t">${item.pin ?? '-'}</td>
        <td class="p-2 border-t">${item.zero_raw_adc ?? '-'}</td>
        <td class="p-2 border-t">${item.span_raw_adc ?? '-'}</td>
        <td class="p-2 border-t">${item.zero_pressure_value ?? '-'}</td>
        <td class="p-2 border-t">${item.span_pressure_value ?? '-'}</td>
        <td class="p-2 border-t"><button class="editCalBtn bg-blue-600 text-white px-2 py-1 rounded">Edit</button></td>
      `;
      const btn = tr.querySelector('.editCalBtn');
      btn.addEventListener('click', async () => {
  // Fetch fresh calibration for this pin (use /calibrate?pin=...)
        const hostUrl = host.startsWith('http') ? host : ('http://' + host);
          let pinNumber = item.pin;
        if (!pinNumber && item.pin_index !== undefined) {
          // derive pin number by calling /sensors/readings or /sensors/config is another option, but prefer existing payload
            pinNumber = item.pin || item.pin_number; // fallback (maintain backward compat with older server responses)
        }
        const calUrl = hostUrl + '/calibrate?pin_index=' + (item.pin_index ?? k);
        try {
          const r = await fetch(calUrl, { cache: 'no-store' });
          if (!r.ok) throw new Error('HTTP ' + r.status);
          const j = await r.json();
          // Open an editor below the table
          const editor = document.createElement('div');
          editor.className = 'mt-4 p-2 border rounded bg-white';
          editor.innerHTML = `<h4 class="font-medium mb-2">Edit ${j.tag || ('Sensor ' + (j.pin_index+1))}</h4>`;
          renderCalibrationForm(editor, j, hostUrl + '/calibrate/pin');
          // Replace or append editor
          const existing = containerEl.querySelector('.calEditor');
          if (existing) existing.remove();
          editor.classList.add('calEditor');
          containerEl.appendChild(editor);
        } catch (e) {
          alert('Failed to fetch calibration: ' + e.message);
        }
      });
      tbody.appendChild(tr);
    }
    table.appendChild(tbody);
    containerEl.appendChild(table);
  }

  // Render sensors table for /sensors/config with editable enabled and notification_interval_ms
  function renderSensorsConfigView(containerEl, cfgObj, endpoint) {
    containerEl.innerHTML = '';
    const title = document.createElement('h3'); title.className = 'font-medium mb-2'; title.textContent = 'Per-Tag Sensor Config';
    containerEl.appendChild(title);
    const table = document.createElement('table'); table.className = 'w-full text-sm table-auto border-collapse';
    const thead = document.createElement('thead'); thead.innerHTML = '<tr><th class="p-2">Index</th><th class="p-2">Pin</th><th class="p-2">Enabled</th><th class="p-2">Notification Interval (ms)</th><th class="p-2">Actions</th></tr>';
    table.appendChild(thead);
    const tbody = document.createElement('tbody');
    const sensors = Array.isArray(cfgObj.sensors) ? cfgObj.sensors : [];
    sensors.forEach((s) => {
      const tr = document.createElement('tr');
      tr.innerHTML = `
        <td class="p-2 border-t">${s.sensor_index}</td>
        <td class="p-2 border-t">${s.sensor_pin}</td>
        <td class="p-2 border-t"><input type="checkbox" ${s.enabled ? 'checked' : ''} class="cfgEnabled"></td>
        <td class="p-2 border-t"><input type="number" value="${s.notification_interval_ms || 0}" class="cfgInterval border rounded px-2 py-1 w-36"></td>
        <td class="p-2 border-t"><button class="saveSensorBtn bg-green-600 text-white px-2 py-1 rounded">Save</button></td>
      `;
      const saveBtn = tr.querySelector('.saveSensorBtn');
      saveBtn.addEventListener('click', async () => {
        const enabled = tr.querySelector('.cfgEnabled').checked;
        const interval = Number(tr.querySelector('.cfgInterval').value || 0);
        const payload = { sensors: [{ sensor_index: s.sensor_index, enabled: enabled, notification_interval_ms: interval }] };
        try {
          const r = await fetch(endpoint, { method: 'POST', headers: { 'Content-Type':'application/json' }, body: JSON.stringify(payload) });
          if (!r.ok) throw new Error('HTTP ' + r.status);
          saveBtn.textContent = 'Saved';
          setTimeout(()=> saveBtn.textContent='Save', 1200);
        } catch (e) { alert('Save failed: ' + e.message); }
      });
      tbody.appendChild(tr);
    });
    table.appendChild(tbody);
    containerEl.appendChild(table);
  }

  // When saving calibration via /calibrate/pin the server expects JSON body with pin or pin_index
  // renderCalibrationForm will POST/PUT to the given endpoint; ensure payload contains pin or pin_index
    btnRow.appendChild(saveBtn); btnRow.appendChild(status); form.appendChild(btnRow);
    containerEl.appendChild(form);
  }

  function getByPath(obj, path) {
    const parts = path.split('.'); let cur = obj;
    for (const p of parts) {
      if (cur == null) return undefined;
      cur = cur[p];
    }
    return cur;
  }

  function diffObjects(a, b) {
    // Return an object containing keys from b that differ from a (shallow/nested)
    if (!a) return b;
    const out = Array.isArray(b) ? [] : {};
    function walk(src, dst, target) {
      for (const k of Object.keys(dst)) {
        const v = dst[k];
        const s = src ? src[k] : undefined;
        if (v && typeof v === 'object' && !Array.isArray(v)) {
          const child = {};
          walk(s || {}, v, child);
          if (Object.keys(child).length) target[k] = child;
        } else if (Array.isArray(v)) {
          // arrays: do naive comparison
          if (!s || JSON.stringify(s) !== JSON.stringify(v)) target[k] = v;
        } else {
          if (typeof s === 'undefined' || s !== v) target[k] = v;
        }
      }
    }
    walk(a, b, out);
    return out;
  }

  async function loadUtilsView() {
    const out = document.getElementById('utilsView');
    out.innerHTML = `
      <div class="space-y-2">
        <button id="btnNtp" class="bg-blue-600 text-white px-3 py-1 rounded">Sync NTP</button>
        <div id="utilsStatus" class="text-sm text-gray-600"></div>
      </div>
    `;
    document.getElementById('btnNtp').addEventListener('click', async () => {
      const host = localStorage.getItem(STORAGE_KEY) || '';
      if (!host) { document.getElementById('utilsStatus').textContent = 'No device configured.'; return; }
      const url = (host.startsWith('http') ? host : ('http://' + host)) + '/time/sync';
      try {
        document.getElementById('utilsStatus').textContent = 'Syncing...';
        const r = await fetch(url, { method: 'POST' });
        document.getElementById('utilsStatus').textContent = r.ok ? 'NTP sync requested' : ('Error ' + r.status);
      } catch (err) {
        document.getElementById('utilsStatus').textContent = 'Error: ' + (err && err.message ? err.message : String(err));
      }
    });
  }

  // Start routing after Alpine is ready
  document.addEventListener('DOMContentLoaded', () => {
    // Now safe to start Alpine — clientApp wrapper is already installed earlier
    Alpine.start();
    // Give Alpine a tick then setup routing
    setTimeout(() => setupRouting(), 0);
  });
