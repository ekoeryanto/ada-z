<template>
  <div class="space-y-8">
    <section class="rounded-2xl border border-slate-800/70 bg-slate-900/50 p-6">
      <h1 class="text-2xl font-semibold text-white">Modbus RTU</h1>
      <p class="mt-2 max-w-3xl text-sm text-slate-300">
        Kelola dan pantau perangkat Modbus RTU yang terhubung. Konfigurasi disimpan sebagai <code class="rounded bg-slate-800 px-1 py-0.5 text-xs">/modbus.json</code> di kartu SD.
      </p>
    </section>

    <section v-if="errorMessage" class="rounded-xl border border-rose-500/30 bg-rose-500/10 p-4 text-sm text-rose-100">
      {{ errorMessage }}
    </section>

    <section v-if="notice" :class="notice.class">
      {{ notice.message }}
    </section>

    <!-- Live Data -->
    <section class="space-y-6">
      <h2 class="text-xl font-semibold text-white">Live Data</h2>
      <div v-if="liveDataLoading" class="flex items-center gap-3 rounded-xl border border-slate-800/70 bg-slate-900/50 p-6 text-sm text-slate-300">
        <Icon icon="mdi:progress-clock" class="h-5 w-5 animate-spin text-brand-300" />
        Memuat data live Modbus...
      </div>
      <div v-else-if="slavesData.length === 0" class="rounded-xl border border-slate-800/70 bg-slate-900/50 p-6 text-sm text-slate-300">
        Tidak ada slave Modbus yang dikonfigurasi atau data belum diterima.
      </div>
      <div v-else class="grid gap-6 lg:grid-cols-1 xl:grid-cols-2">
        <div v-for="slave in slavesData" :key="slave.address" class="relative flex flex-col gap-4 rounded-2xl border border-slate-800/70 bg-slate-900/60 p-5">
          <div class="flex items-start justify-between gap-3">
            <div>
              <h3 class="text-lg font-semibold text-white">{{ slave.label || `Slave ${slave.address}` }}</h3>
              <p class="text-xs text-slate-400">Alamat: {{ slave.address }}</p>
            </div>
            <span :class="['inline-flex items-center gap-1.5 rounded-full px-2 py-1 text-xs font-medium', slave.online ? 'bg-emerald-500/10 text-emerald-400' : 'bg-rose-500/10 text-rose-400']">
              <span :class="['h-1.5 w-1.5 rounded-full', slave.online ? 'bg-emerald-500' : 'bg-rose-500']"></span>
              {{ slave.online ? 'Online' : 'Offline' }}
            </span>
          </div>
          <div class="overflow-x-auto">
            <table class="min-w-full text-sm">
              <thead class="text-xs uppercase tracking-wider text-slate-400">
                <tr>
                  <th class="px-3 py-2 text-left">Register</th>
                  <th class="px-3 py-2 text-right">Value</th>
                  <th class="px-3 py-2 text-left">Unit</th>
                </tr>
              </thead>
              <tbody class="divide-y divide-slate-800">
                <tr v-for="reg in slave.registers" :key="reg.key">
                  <td class="whitespace-nowrap px-3 py-2 font-medium text-slate-200">{{ reg.label }}</td>
                  <td class="whitespace-nowrap px-3 py-2 text-right font-mono" :class="{ 'text-slate-500': reg.value === null }">
                    {{ reg.value !== null ? reg.value.toFixed(2) : 'N/A' }}
                  </td>
                  <td class="whitespace-nowrap px-3 py-2 text-slate-400">{{ reg.unit }}</td>
                </tr>
              </tbody>
            </table>
          </div>
        </div>
      </div>
    </section>

    <!-- Configuration -->
    <section class="space-y-4">
      <h2 class="text-xl font-semibold text-white">Konfigurasi</h2>
      <div v-if="configLoading" class="flex items-center gap-3 rounded-xl border border-slate-800/70 bg-slate-900/50 p-6 text-sm text-slate-300">
        <Icon icon="mdi:progress-clock" class="h-5 w-5 animate-spin text-brand-300" />
        Memuat konfigurasi Modbus...
      </div>
      <div v-else class="space-y-4">
        <div class="flex flex-wrap gap-3">
          <button
            class="inline-flex items-center gap-2 rounded-full border border-slate-700 bg-slate-800/70 px-4 py-2 text-sm text-slate-200 transition hover:border-slate-600 hover:text-white"
            type="button"
            @click="reloadConfig"
          >
            <Icon icon="mdi:reload" class="h-4 w-4" />
            Muat Ulang Konfigurasi
          </button>
          <button
            class="inline-flex items-center gap-2 rounded-full border border-emerald-500/60 bg-emerald-500/20 px-4 py-2 text-sm text-emerald-100 transition hover:bg-emerald-500/30 disabled:cursor-not-allowed disabled:opacity-60"
            type="button"
            :disabled="saving || !isConfigValidJson"
            @click="save"
          >
            <Icon icon="mdi:content-save" class="h-4 w-4" />
            <span v-if="saving">Menyimpan…</span>
            <span v-else>Simpan Konfigurasi</span>
          </button>
        </div>
        <div>
          <textarea
            v-model="configContent"
            rows="20"
            class="w-full rounded-lg border bg-slate-950/60 font-mono text-sm focus:border-brand-500/60 focus:outline-none"
            :class="isConfigValidJson ? 'border-slate-700' : 'border-rose-500/50'"
            placeholder="Masukkan konfigurasi Modbus dalam format JSON..."
          ></textarea>
          <p v-if="!isConfigValidJson" class="mt-1 text-xs text-rose-400">JSON tidak valid.</p>
        </div>
      </div>
    </section>

  </div>
</template>

<script setup>
import { Icon } from '@iconify/vue';
import { computed, onMounted, onUnmounted, ref } from 'vue';
import { fetchModbusConfig, saveModbusConfig, fetchModbusSlaves } from '../services/api';

const configLoading = ref(true);
const liveDataLoading = ref(true);
const saving = ref(false);
const errorMessage = ref('');
const notice = ref(null);
const configContent = ref('');
const slavesData = ref([]);

let liveDataInterval = null;

const isConfigValidJson = computed(() => {
  try {
    JSON.parse(configContent.value);
    return true;
  } catch (e) {
    return false;
  }
});

function resetNotice() {
  notice.value = null;
}

function setNotice(type, message) {
  const baseClass =
    type === 'success'
      ? 'rounded-xl border border-emerald-500/40 bg-emerald-500/10 p-4 text-sm text-emerald-50'
      : 'rounded-xl border border-amber-400/40 bg-amber-400/10 p-4 text-sm text-amber-100';
  notice.value = { class: baseClass, message };
}

async function loadConfig() {
  configLoading.value = true;
  errorMessage.value = '';
  resetNotice();
  try {
    const data = await fetchModbusConfig();
    configContent.value = JSON.stringify(data, null, 2);
  } catch (err) {
    errorMessage.value = err?.message ?? 'Gagal memuat konfigurasi Modbus';
  } finally {
    configLoading.value = false;
  }
}

async function fetchLiveData() {
  try {
    slavesData.value = await fetchModbusSlaves();
  } catch (err) {
    console.error('Failed to fetch modbus live data:', err);
    // Optionally show a small, non-intrusive error indicator
  } finally {
    liveDataLoading.value = false;
  }
}

async function save() {
  if (!isConfigValidJson.value) {
    errorMessage.value = 'Konfigurasi bukan JSON yang valid.';
    return;
  }
  saving.value = true;
  errorMessage.value = '';
  resetNotice();
  try {
    const payload = JSON.parse(configContent.value);
    const response = await saveModbusConfig(payload);
    if (response?.config) {
      configContent.value = JSON.stringify(response.config, null, 2);
    }
    const persisted = response?.persisted === 1;
    const message = response?.message ?? (persisted ? 'Konfigurasi Modbus tersimpan.' : 'Konfigurasi berlaku sementara (tidak tersimpan).');
    setNotice(persisted ? 'success' : 'warning', message);
    // reload live data after save
    await fetchLiveData();
  } catch (err) {
    errorMessage.value = err?.message ?? 'Gagal menyimpan konfigurasi Modbus';
  } finally {
    saving.value = false;
  }
}

async function reloadConfig() {
  await loadConfig();
  setNotice('success', 'Konfigurasi dimuat ulang dari controller.');
}

onMounted(async () => {
  await loadConfig();
  await fetchLiveData();
  liveDataInterval = setInterval(fetchLiveData, 20000);
});

onUnmounted(() => {
  if (liveDataInterval) {
    clearInterval(liveDataInterval);
  }
});
</script>