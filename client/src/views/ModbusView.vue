<template>
  <div class="space-y-8">
    <section class="rounded-2xl border border-slate-800/70 bg-slate-900/50 p-6">
      <h1 class="text-2xl font-semibold text-white">Modbus Ultrasonic Sensors</h1>
      <p class="mt-2 max-w-3xl text-sm text-slate-300">
        Kelola daftar slave Modbus RTU yang dipolling oleh controller. Setiap slave mewakili sensor
        ultrasonik <span class="font-medium text-brand-200">A01ANYUB V2</span> dengan register jarak,
        suhu, dan kekuatan sinyal. Perubahan disimpan ke <code class="rounded bg-slate-800 px-1 py-0.5 text-xs">/modbus.json</code>
        pada kartu SD sehingga bertahan setelah reboot.
      </p>
    </section>

    <section v-if="errorMessage" class="rounded-xl border border-rose-500/30 bg-rose-500/10 p-4 text-sm text-rose-100">
      {{ errorMessage }}
    </section>

    <section v-if="notice" :class="notice.class">
      {{ notice.message }}
    </section>

    <div v-if="loading" class="flex items-center gap-3 rounded-xl border border-slate-800/70 bg-slate-900/50 p-6 text-sm text-slate-300">
      <Icon icon="mdi:progress-clock" class="h-5 w-5 animate-spin text-brand-300" />
      Memuat konfigurasi Modbus…
    </div>

    <div v-else class="space-y-6">
      <div class="flex flex-wrap gap-3">
        <button
          class="inline-flex items-center gap-2 rounded-full border border-slate-700 bg-slate-800/70 px-4 py-2 text-sm text-slate-200 transition hover:border-slate-600 hover:text-white"
          type="button"
          @click="reload"
        >
          <Icon icon="mdi:reload" class="h-4 w-4" />
          Muat ulang
        </button>
        <button
          class="inline-flex items-center gap-2 rounded-full border border-brand-600 bg-brand-500/20 px-4 py-2 text-sm text-brand-100 transition hover:bg-brand-500/30"
          type="button"
          @click="addSlave"
        >
          <Icon icon="mdi:plus" class="h-4 w-4" />
          Tambah slave
        </button>
        <button
          class="inline-flex items-center gap-2 rounded-full border border-emerald-500/60 bg-emerald-500/20 px-4 py-2 text-sm text-emerald-100 transition hover:bg-emerald-500/30 disabled:cursor-not-allowed disabled:opacity-60"
          type="button"
          :disabled="saving || !canSave"
          @click="save"
        >
          <Icon icon="mdi:content-save" class="h-4 w-4" />
          <span v-if="saving">Menyimpan…</span>
          <span v-else>Simpan konfigurasi</span>
        </button>
      </div>

      <div v-if="slaves.length === 0" class="rounded-xl border border-slate-800/70 bg-slate-900/50 p-6 text-sm text-slate-300">
        Belum ada slave Modbus yang dikonfigurasi. Tambahkan sensor untuk mulai polling jarak.
      </div>

      <div v-else class="grid gap-6 lg:grid-cols-2">
        <div v-for="(slave, index) in slaves" :key="slave.localId" class="relative flex flex-col gap-4 rounded-2xl border border-slate-800/70 bg-slate-900/60 p-5">
          <div class="flex items-start justify-between gap-3">
            <div>
              <h2 class="text-lg font-semibold text-white">{{ slave.label || `Slave ${index + 1}` }}</h2>
              <p class="text-xs text-slate-400">Alamat {{ slave.address }} &middot; ID {{ slave.id }}</p>
            </div>
            <button
              class="inline-flex items-center gap-1 rounded-full border border-rose-500/60 bg-rose-500/10 px-3 py-1 text-xs text-rose-100 transition hover:bg-rose-500/20"
              type="button"
              @click="removeSlave(index)"
            >
              <Icon icon="mdi:trash-can-outline" class="h-3 w-3" />
              Hapus
            </button>
          </div>

          <div class="grid gap-4 text-sm text-slate-200">
            <label class="grid gap-2">
              <span class="text-xs uppercase tracking-wide text-slate-400">Nama / Label</span>
              <input
                v-model.trim="slave.label"
                type="text"
                class="rounded-lg border border-slate-700 bg-slate-950/60 px-3 py-2 focus:border-brand-500/60 focus:outline-none"
                placeholder="Ultrasonik Intake"
              />
            </label>

            <div class="grid grid-cols-2 gap-4">
              <label class="grid gap-2">
                <span class="text-xs uppercase tracking-wide text-slate-400">Sensor ID</span>
                <input
                  v-model.trim="slave.id"
                  type="text"
                  class="rounded-lg border border-slate-700 bg-slate-950/60 px-3 py-2 focus:border-brand-500/60 focus:outline-none"
                  placeholder="MB201"
                />
              </label>
              <label class="grid gap-2">
                <span class="text-xs uppercase tracking-wide text-slate-400">Alamat Slave (1-247)</span>
                <input
                  v-model.number="slave.address"
                  type="number"
                  min="1"
                  max="247"
                  class="rounded-lg border border-slate-700 bg-slate-950/60 px-3 py-2 focus:border-brand-500/60 focus:outline-none"
                />
              </label>
            </div>

            <div class="rounded-xl border border-slate-800/70 bg-slate-950/50 p-4">
              <h3 class="text-xs font-semibold uppercase tracking-wide text-slate-400">Register yang dipolling</h3>
              <div class="mt-3 grid grid-cols-3 gap-3 text-xs">
                <label class="grid gap-1">
                  <span class="text-slate-400">Jarak (holding)</span>
                  <input
                    v-model.number="slave.distance_reg"
                    type="number"
                    min="0"
                    class="rounded-lg border border-slate-700 bg-slate-950/60 px-3 py-2 focus:border-brand-500/60 focus:outline-none"
                  />
                </label>
                <label class="grid gap-1">
                  <span class="text-slate-400">Suhu (0.1&deg;C)</span>
                  <input
                    v-model.number="slave.temperature_reg"
                    type="number"
                    min="-1"
                    class="rounded-lg border border-slate-700 bg-slate-950/60 px-3 py-2 focus:border-brand-500/60 focus:outline-none"
                    placeholder="2"
                  />
                </label>
                <label class="grid gap-1">
                  <span class="text-slate-400">Sinyal (%)</span>
                  <input
                    v-model.number="slave.signal_reg"
                    type="number"
                    min="-1"
                    class="rounded-lg border border-slate-700 bg-slate-950/60 px-3 py-2 focus:border-brand-500/60 focus:outline-none"
                    placeholder="3"
                  />
                </label>
              </div>
            </div>

            <label class="grid gap-2">
              <span class="text-xs uppercase tracking-wide text-slate-400">Jarak maksimum (meter)</span>
              <input
                v-model.number="slave.max_distance_m"
                type="number"
                min="0"
                step="0.1"
                class="rounded-lg border border-slate-700 bg-slate-950/60 px-3 py-2 focus:border-brand-500/60 focus:outline-none"
              />
            </label>
          </div>
        </div>
      </div>

      <div class="rounded-xl border border-slate-800/60 bg-slate-900/40 p-5 text-xs text-slate-400">
        <p class="font-medium text-slate-200">Catatan</p>
        <ul class="mt-2 list-disc space-y-1 pl-4">
          <li>Gunakan alamat unik 1-247 untuk setiap slave di bus RS485.</li>
          <li>Isi register dengan alamat holding register dari datasheet sensor (hampir selalu 1, 2, dan 3 untuk A01ANYUB).</li>
          <li>Jika kartu SD tidak terpasang, perubahan hanya berlaku sementara sampai reboot.</li>
        </ul>
      </div>
    </div>
  </div>
</template>

<script setup>
import { Icon } from '@iconify/vue';
import { computed, onMounted, reactive, ref } from 'vue';
import { fetchModbusConfig, saveModbusConfig } from '../services/api';

const loading = ref(true);
const saving = ref(false);
const errorMessage = ref('');
const notice = ref(null);
const version = ref(1);
const slaves = reactive([]);

function generateLocalId() {
  if (typeof crypto !== 'undefined' && typeof crypto.randomUUID === 'function') {
    return crypto.randomUUID();
  }
  return `modbus-${Math.random().toString(36).slice(2, 10)}`;
}

const canSave = computed(() => !saving.value && slaves.every(validateSlave));

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

function createDefaultSlave() {
  const taken = new Set(slaves.map((s) => Number(s.address)));
  let nextAddress = 200;
  while (taken.has(nextAddress) && nextAddress < 248) {
    nextAddress += 1;
  }
  const index = slaves.length + 1;
  return {
    localId: generateLocalId(),
    id: `MB${nextAddress}`,
    label: `Ultrasonik ${index}`,
    address: nextAddress,
    distance_reg: 1,
    temperature_reg: 2,
    signal_reg: 3,
    max_distance_m: 10,
  };
}

function applyConfigObject(obj) {
  version.value = typeof obj?.version === 'number' ? obj.version : 1;
  slaves.splice(0, slaves.length);
  const arr = Array.isArray(obj?.slaves) ? obj.slaves : [];
  arr.forEach((item, idx) => {
    slaves.push({
      localId: generateLocalId(),
      id: typeof item?.id === 'string' && item.id.length ? item.id : `MB${item?.address ?? idx + 1}`,
      label: typeof item?.label === 'string' ? item.label : '',
      address: Number.isInteger(item?.address) ? item.address : 1,
      distance_reg: Number.isInteger(item?.distance_reg) ? item.distance_reg : 1,
      temperature_reg: Number.isInteger(item?.temperature_reg) ? item.temperature_reg : 2,
      signal_reg: Number.isInteger(item?.signal_reg) ? item.signal_reg : 3,
      max_distance_m: Number.isFinite(item?.max_distance_m) ? Number(item.max_distance_m) : 10,
    });
  });
}

function validateSlave(slave) {
  if (!slave) return false;
  if (!Number.isInteger(slave.address) || slave.address < 1 || slave.address > 247) {
    return false;
  }
  if (!Number.isInteger(slave.distance_reg) || slave.distance_reg < 0) {
    return false;
  }
  if (!Number.isInteger(slave.temperature_reg) || slave.temperature_reg < -1) {
    return false;
  }
  if (!Number.isInteger(slave.signal_reg) || slave.signal_reg < -1) {
    return false;
  }
  if (!Number.isFinite(slave.max_distance_m) || slave.max_distance_m < 0) {
    return false;
  }
  return true;
}

async function loadConfig() {
  loading.value = true;
  errorMessage.value = '';
  resetNotice();
  try {
    const data = await fetchModbusConfig();
    applyConfigObject(data);
    if (slaves.length === 0) {
      slaves.push(createDefaultSlave());
    }
  } catch (err) {
    errorMessage.value = err?.message ?? 'Gagal memuat konfigurasi Modbus';
  } finally {
    loading.value = false;
  }
}

function addSlave() {
  slaves.push(createDefaultSlave());
}

function removeSlave(index) {
  slaves.splice(index, 1);
}

function buildPayload() {
  const payloadSlaves = slaves.map((slave) => {
    const sanitizedId = slave.id && slave.id.trim().length ? slave.id.trim() : `MB${slave.address}`;
    return {
      id: sanitizedId,
      label: slave.label?.trim?.() ?? '',
      address: Number(slave.address),
      distance_reg: Number(slave.distance_reg),
      temperature_reg: Number(slave.temperature_reg),
      signal_reg: Number(slave.signal_reg),
      max_distance_m: Number(slave.max_distance_m),
    };
  });
  return {
    version: version.value,
    slaves: payloadSlaves,
  };
}

async function save() {
  if (!canSave.value) {
    errorMessage.value = 'Periksa kembali nilai register dan alamat slave.';
    return;
  }
  saving.value = true;
  errorMessage.value = '';
  resetNotice();
  try {
    const payload = buildPayload();
    const response = await saveModbusConfig(payload);
    if (response?.config) {
      applyConfigObject(response.config);
    } else {
      await loadConfig();
    }
    const persisted = response?.persisted === 1;
    const message = response?.message ?? (persisted ? 'Konfigurasi Modbus tersimpan.' : 'Konfigurasi berlaku sementara (tidak tersimpan).');
    setNotice(persisted ? 'success' : 'warning', message);
  } catch (err) {
    errorMessage.value = err?.message ?? 'Gagal menyimpan konfigurasi Modbus';
  } finally {
    saving.value = false;
  }
}

async function reload() {
  await loadConfig();
  setNotice('success', 'Konfigurasi dimuat ulang dari controller.');
}

onMounted(loadConfig);
</script>
