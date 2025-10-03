<template>
  <div class="space-y-8">
    <section class="rounded-2xl border border-slate-800/70 bg-slate-900/50 p-6">
      <h1 class="text-2xl font-semibold text-white">Modbus Poll</h1>
      <p class="mt-2 max-w-3xl text-sm text-slate-300">
        Kirim permintaan Modbus kustom ke perangkat di bus RS485 untuk tujuan debugging atau komisioning.
      </p>
    </section>

    <section v-if="errorMessage" class="rounded-xl border border-rose-500/30 bg-rose-500/10 p-4 text-sm text-rose-100">
      {{ errorMessage }}
    </section>

    <!-- Polling Form -->
    <section class="rounded-2xl border border-slate-800/70 bg-slate-900/60 p-6">
      <form @submit.prevent="sendPollRequest" class="space-y-4">
        <div class="grid grid-cols-1 gap-6 md:grid-cols-2 lg:grid-cols-4">
          <label class="grid gap-2">
            <span class="text-sm font-medium text-slate-300">Alamat Slave</span>
            <input v-model.number="form.slave_address" type="number" min="1" max="247" required class="rounded-lg border border-slate-700 bg-slate-950/60 px-3 py-2 focus:border-brand-500/60 focus:outline-none" />
          </label>
          <label class="grid gap-2">
            <span class="text-sm font-medium text-slate-300">Tipe Register</span>
            <select v-model="form.register_type" class="rounded-lg border border-slate-700 bg-slate-950/60 px-3 py-2 focus:border-brand-500/60 focus:outline-none">
              <option value="holding">Holding</option>
              <option value="input">Input</option>
            </select>
          </label>
          <label class="grid gap-2">
            <span class="text-sm font-medium text-slate-300">Alamat Register</span>
            <input v-model.number="form.register_address" type="number" min="0" required class="rounded-lg border border-slate-700 bg-slate-950/60 px-3 py-2 focus:border-brand-500/60 focus:outline-none" />
          </label>
          <label class="grid gap-2">
            <span class="text-sm font-medium text-slate-300">Jumlah</span>
            <input v-model.number="form.count" type="number" min="1" max="125" required class="rounded-lg border border-slate-700 bg-slate-950/60 px-3 py-2 focus:border-brand-500/60 focus:outline-none" />
          </label>
        </div>
        <div class="flex justify-end">
          <button type="submit" :disabled="polling" class="inline-flex items-center gap-2 rounded-full bg-brand-500 px-6 py-2.5 text-sm font-semibold text-white transition hover:bg-brand-400 disabled:cursor-not-allowed disabled:opacity-70">
            <Icon v-if="polling" icon="mdi:progress-clock" class="h-5 w-5 animate-spin" />
            <span v-else>Kirim</span>
          </button>
        </div>
      </form>
    </section>

    <!-- Result Display -->
    <section v-if="result" class="rounded-2xl border border-slate-800/70 bg-slate-900/50 p-6">
      <h2 class="text-xl font-semibold text-white">Hasil</h2>
      <div class="mt-4 space-y-4">
        <div v-if="result.status === 'success'" class="rounded-xl bg-emerald-500/10 p-4">
          <p class="font-medium text-emerald-400">Sukses</p>
          <div class="mt-2 font-mono text-sm text-slate-300">
            <p>Data ({{ result.data.length }} register):</p>
            <div class="mt-1 flex flex-wrap gap-2">
              <span v-for="(val, index) in result.data" :key="index" class="rounded bg-slate-800 px-2 py-1">{{ val }}</span>
            </div>
          </div>
        </div>
        <div v-if="result.status === 'error'" class="rounded-xl bg-rose-500/10 p-4">
          <p class="font-medium text-rose-400">Error</p>
          <p class="mt-1 font-mono text-sm text-slate-300">{{ result.error_message }} (Code: {{ result.error_code }})</p>
        </div>
      </div>
    </section>

  </div>
</template>

<script setup>
import { ref, reactive } from 'vue';
import { Icon } from '@iconify/vue';
import { pollModbus } from '../services/api';

const form = reactive({
  slave_address: 1,
  register_type: 'holding',
  register_address: 0,
  count: 10,
});

const polling = ref(false);
const errorMessage = ref('');
const result = ref(null);

async function sendPollRequest() {
  polling.value = true;
  errorMessage.value = '';
  result.value = null;

  try {
    const response = await pollModbus(form);
    result.value = JSON.parse(response);
  } catch (err) {
    errorMessage.value = err?.message ?? 'Gagal mengirim permintaan poll';
  } finally {
    polling.value = false;
  }
}
</script>
