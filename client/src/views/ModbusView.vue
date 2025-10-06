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

            <details class="mt-3 rounded-lg border border-slate-800/60 bg-slate-950/40 p-3 text-xs">
              <summary class="cursor-pointer font-medium text-slate-200">Advanced: custom registers</summary>
              <div class="mt-3 space-y-2">
                <div v-if="!slave.registers || slave.registers.length === 0" class="text-xs text-slate-400">No custom registers defined. Add one to configure arbitrary registers (32-bit, floats, scaling).</div>
                <div v-for="(reg, ri) in (slave.registers || [])" :key="reg.localId || ri" class="grid grid-cols-6 gap-2 items-center text-xs">
                  <input v-model.trim="reg.id" placeholder="id (MB201_dist)" class="col-span-1 rounded-lg border border-slate-700 bg-slate-950/60 px-2 py-1" />
                  <input v-model.trim="reg.name" placeholder="name" class="col-span-1 rounded-lg border border-slate-700 bg-slate-950/60 px-2 py-1" />
                  <input v-model.trim="reg.unit" placeholder="unit" class="col-span-1 rounded-lg border border-slate-700 bg-slate-950/60 px-2 py-1" />
                  <input v-model.number="reg.reg" type="number" placeholder="reg" class="col-span-1 rounded-lg border border-slate-700 bg-slate-950/60 px-2 py-1" />
                  <select v-model.number="reg.count" class="col-span-1 rounded-lg border border-slate-700 bg-slate-950/60 px-2 py-1">
                    <option :value="1">1</option>
                    <option :value="2">2</option>
                  </select>
                  <div class="col-span-1 flex items-center gap-2">
                    <input v-model.number="reg.scale" type="number" step="any" placeholder="scale" class="rounded-lg border border-slate-700 bg-slate-950/60 px-2 py-1 w-24" />
                    <button @click.prevent="removeRegister(slaves.indexOf(slave), ri)" class="rounded-full bg-rose-500/20 px-2 py-1 text-rose-100">Remove</button>
                  </div>
                </div>
                <div class="mt-2">
                  <button @click.prevent="addRegister(slaves.indexOf(slave))" class="inline-flex items-center gap-2 rounded-full border border-brand-600 bg-brand-500/20 px-3 py-1 text-xs text-brand-100">Add register</button>
                </div>
              </div>
            </details>

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
      // map optional registers[] if present
      registers: Array.isArray(item?.registers) ? item.registers.map((r) => ({
        localId: generateLocalId(),
        id: typeof r?.id === 'string' ? r.id : '',
        name: typeof r?.name === 'string' ? r.name : '',
        unit: typeof r?.unit === 'string' ? r.unit : '',
        reg: Number.isInteger(r?.reg) ? r.reg : -1,
        count: Number.isInteger(r?.count) ? r.count : 1,
        type: Number.isInteger(r?.type) ? r.type : 0,
        scale: Number.isFinite(r?.scale) ? Number(r.scale) : 1,
        ema_alpha: Number.isFinite(r?.ema_alpha) ? Number(r.ema_alpha) : 0,
      })) : [],
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

function addRegister(slaveIndex) {
  if (slaveIndex < 0 || slaveIndex >= slaves.length) return;
  const reg = {
    localId: generateLocalId(),
    id: '',
    name: '',
    unit: '',
    reg: 0,
    count: 1,
    type: 0,
    scale: 1,
    ema_alpha: 0,
  };
  if (!Array.isArray(slaves[slaveIndex].registers)) slaves[slaveIndex].registers = [];
  slaves[slaveIndex].registers.push(reg);
}

function removeRegister(slaveIndex, regIndex) {
  if (slaveIndex < 0 || slaveIndex >= slaves.length) return;
  if (!Array.isArray(slaves[slaveIndex].registers)) return;
  slaves[slaveIndex].registers.splice(regIndex, 1);
}

function buildPayload() {
  const payloadSlaves = slaves.map((slave) => {
    const sanitizedId = slave.id && slave.id.trim().length ? slave.id.trim() : `MB${slave.address}`;
    const base = {
      id: sanitizedId,
      label: slave.label?.trim?.() ?? '',
      address: Number(slave.address),
      distance_reg: Number(slave.distance_reg),
      temperature_reg: Number(slave.temperature_reg),
      signal_reg: Number(slave.signal_reg),
      max_distance_m: Number(slave.max_distance_m),
    };
    if (Array.isArray(slave.registers) && slave.registers.length > 0) {
      base.registers = slave.registers.map((r) => ({
        id: r.id && r.id.trim().length ? r.id.trim() : undefined,
        name: r.name && r.name.trim().length ? r.name.trim() : undefined,
        unit: r.unit && r.unit.trim().length ? r.unit.trim() : undefined,
        reg: Number.isInteger(r.reg) ? r.reg : -1,
        count: Number.isInteger(r.count) ? r.count : 1,
        type: Number.isInteger(r.type) ? r.type : 0,
        scale: Number.isFinite(r.scale) ? Number(r.scale) : 1,
        ema_alpha: Number.isFinite(r.ema_alpha) ? Number(r.ema_alpha) : 0,
      }));
    }
    return base;
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
