<template>
  <div>
    <h1 class="text-2xl font-semibold mb-4">SSE Sensor Debug</h1>

    <section class="mb-6">
      <label class="block text-sm text-slate-300 mb-2">Sensor (pin index)</label>
      <div class="flex items-center gap-3">
        <select v-model="selectedIndex" class="rounded px-3 py-2 bg-slate-800 text-slate-100">
          <option v-for="i in sensorCount" :key="i" :value="i-1">{{ 'AI' + i + ' (index ' + (i-1) + ')' }}</option>
        </select>
        <button @click="triggerDebug" class="rounded bg-brand-500 px-4 py-2 text-sm">Trigger SSE Push</button>
        <button v-if="!connected" @click="connect" class="rounded border px-3 py-2">Connect</button>
        <button v-else @click="disconnect" class="rounded border px-3 py-2">Disconnect</button>
      </div>
        <p class="text-xs text-slate-400 mt-2">Connect to <code>/api/sse/stream</code> and listen for <code>sensor_debug</code> events.</p>
    </section>

    <section>
      <h2 class="text-lg font-medium mb-2">Received events</h2>
      <div class="h-64 overflow-auto rounded bg-slate-900 p-3">
        <div v-if="events.length === 0" class="text-slate-500">No events yet</div>
        <div v-for="(e, idx) in events" :key="idx" class="mb-2 text-sm">
          <div class="text-xs text-slate-400">{{ e.time }}</div>
          <pre class="whitespace-pre-wrap text-slate-200">{{ e.data }}</pre>
        </div>
      </div>
    </section>
  </div>
</template>

<script setup>
import { ref, onUnmounted } from 'vue';

const sensorCount = 4; // adjust if your board has different count
const selectedIndex = ref(0);
const events = ref([]);
const connected = ref(false);
let es = null;

function connect() {
  if (es) return;
  es = new EventSource('/api/sse/stream');
  es.addEventListener('sensor_debug', (ev) => {
    try {
      const parsed = JSON.parse(ev.data);
      events.value.unshift({ time: new Date().toLocaleTimeString(), data: JSON.stringify(parsed, null, 2) });
    } catch (err) {
      events.value.unshift({ time: new Date().toLocaleTimeString(), data: ev.data });
    }
  });
  es.onopen = () => { connected.value = true; };
  es.onerror = () => { connected.value = false; };
}

function disconnect() {
  if (!es) return;
  es.close();
  es = null;
  connected.value = false;
}

async function triggerDebug() {
  const body = { pin_index: selectedIndex.value };
  try {
    const resp = await fetch('/api/sse/debug', {
      method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body)
    });
    const txt = await resp.text();
    events.value.unshift({ time: new Date().toLocaleTimeString(), data: '<ack> ' + txt });
  } catch (err) {
    events.value.unshift({ time: new Date().toLocaleTimeString(), data: '<error> ' + String(err) });
  }
}

onUnmounted(() => disconnect());
</script>

<style scoped>
pre { font-family: ui-monospace, SFMono-Regular, Menlo, Monaco, "Roboto Mono", "Courier New", monospace; font-size: 0.85rem; }
</style>
