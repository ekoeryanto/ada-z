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
        <div class="grid grid-cols-1 gap-6 md:grid-cols-2 lg:grid-cols-5">
          <label class="grid gap-2 md:col-span-2 lg:col-span-2">
            <span class="text-sm font-medium text-slate-300">Operasi</span>
            <select v-model="form.operation" class="rounded-lg border border-slate-700 bg-slate-950/60 px-3 py-2 focus:border-brand-500/60 focus:outline-none">
              <option value="read_holding">Read Holding Registers</option>
              <option value="read_input">Read Input Registers</option>
              <option value="write_single">Write Single Register</option>
              <option value="write_multiple">Write Multiple Registers</option>
            </select>
          </label>
          <label class="grid gap-2">
            <span class="text-sm font-medium text-slate-300">Alamat Slave</span>
            <input v-model.number="form.slave_address" type="number" min="0" max="247" required class="rounded-lg border border-slate-700 bg-slate-950/60 px-3 py-2 focus:border-brand-500/60 focus:outline-none" />
          </label>
          <label class="grid gap-2">
            <span class="text-sm font-medium text-slate-300">Alamat Register</span>
            <input v-model.number="form.register_address" type="number" min="0" max="65535" required class="rounded-lg border border-slate-700 bg-slate-950/60 px-3 py-2 focus:border-brand-500/60 focus:outline-none" />
          </label>
          <label v-if="isReadOperation" class="grid gap-2">
            <span class="text-sm font-medium text-slate-300">Jumlah</span>
            <input v-model.number="form.count" type="number" min="1" max="125" required class="rounded-lg border border-slate-700 bg-slate-950/60 px-3 py-2 focus:border-brand-500/60 focus:outline-none" />
          </label>
          <label v-if="isWriteSingle" class="grid gap-2">
            <span class="text-sm font-medium text-slate-300">Nilai</span>
            <input v-model.number="form.value" type="number" min="0" max="65535" required class="rounded-lg border border-slate-700 bg-slate-950/60 px-3 py-2 focus:border-brand-500/60 focus:outline-none" />
          </label>
          <label class="grid gap-2">
            <span class="text-sm font-medium text-slate-300">Baud Rate</span>
            <select v-model="form.baud_rate" class="rounded-lg border border-slate-700 bg-slate-950/60 px-3 py-2 focus:border-brand-500/60 focus:outline-none">
              <option value="">Gunakan default</option>
              <option v-for="rate in baudRateOptions" :key="rate" :value="String(rate)">{{ rate }}</option>
            </select>
          </label>
        </div>

        <div v-if="isWriteMultiple" class="grid gap-2">
          <span class="text-sm font-medium text-slate-300">Nilai Register (pisahkan dengan koma atau spasi)</span>
          <textarea v-model="form.valuesText" rows="3" class="rounded-lg border border-slate-700 bg-slate-950/60 px-3 py-2 text-sm text-slate-200 focus:border-brand-500/60 focus:outline-none" placeholder="Contoh: 100, 200, 300"></textarea>
          <span class="text-xs text-slate-500">Maksimum 123 register per permintaan.</span>
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
          <div class="mt-2 space-y-2 text-sm text-slate-200">
            <p><span class="font-semibold text-slate-100">Operasi:</span> {{ result.operation }}</p>
            <p v-if="result.register_type"><span class="font-semibold text-slate-100">Tipe Register:</span> {{ result.register_type }}</p>
            <p v-if="result.count"><span class="font-semibold text-slate-100">Jumlah:</span> {{ result.count }}</p>
            <p v-if="result.write_count"><span class="font-semibold text-slate-100">Jumlah Tulis:</span> {{ result.write_count }}</p>
            <p v-if="result.baud_rate"><span class="font-semibold text-slate-100">Baud Rate:</span> {{ result.baud_rate }}</p>
            <div v-if="Array.isArray(result.data)" class="mt-2 font-mono text-sm text-slate-300">
              <p>Data ({{ result.data.length }} register):</p>
              <div class="mt-1 flex flex-wrap gap-2">
                <span v-for="(val, index) in result.data" :key="index" class="rounded bg-slate-800 px-2 py-1">{{ val }}</span>
              </div>
            </div>
            <div v-if="Array.isArray(result.values)" class="mt-2 font-mono text-sm text-slate-300">
              <p>Nilai yang Dikirim ({{ result.values.length }} register):</p>
              <div class="mt-1 flex flex-wrap gap-2">
                <span v-for="(val, index) in result.values" :key="index" class="rounded bg-slate-800 px-2 py-1">{{ val }}</span>
              </div>
            </div>
            <p v-if="result.message"><span class="font-semibold text-slate-100">Pesan:</span> {{ result.message }}</p>
          </div>
        </div>
        <div v-if="result.status === 'error'" class="rounded-xl bg-rose-500/10 p-4">
          <p class="font-medium text-rose-400">Error</p>
          <p class="mt-1 font-mono text-sm text-slate-300">
            {{ result.error_message ?? result.message ?? 'Terjadi kesalahan Modbus' }}
            <span v-if="result.error_code !== undefined && result.error_code !== null"> (Code: {{ result.error_code }})</span>
          </p>
        </div>
      </div>
    </section>

  </div>
</template>

<script setup>
import { ref, reactive, computed } from 'vue';
import { Icon } from '@iconify/vue';
import { pollModbus } from '../services/api';

const form = reactive({
  operation: 'read_holding',
  slave_address: 1,
  register_address: 0,
  count: 10,
  value: 0,
  valuesText: '',
  baud_rate: '',
});

const polling = ref(false);
const errorMessage = ref('');
const result = ref(null);

const isReadOperation = computed(() => form.operation === 'read_holding' || form.operation === 'read_input');
const isWriteSingle = computed(() => form.operation === 'write_single');
const isWriteMultiple = computed(() => form.operation === 'write_multiple');
const baudRateOptions = [1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200];

async function sendPollRequest() {
  polling.value = true;
  errorMessage.value = '';
  result.value = null;

  try {
    const payload = {
      operation: form.operation,
      slave_address: form.slave_address,
      register_address: form.register_address,
    };

    if (form.baud_rate !== '') {
      const selectedBaud = Number(form.baud_rate);
      if (!Number.isFinite(selectedBaud) || selectedBaud <= 0) {
        throw new Error('Baud rate tidak valid');
      }
      payload.baud_rate = selectedBaud;
    }

    if (isReadOperation.value) {
      if (!form.count || form.count < 1 || form.count > 125) {
        throw new Error('Jumlah register harus 1-125');
      }
      payload.count = form.count;
    } else if (isWriteSingle.value) {
      if (form.value == null) {
        throw new Error('Nilai register tidak boleh kosong');
      }
      if (form.value < 0 || form.value > 0xffff) {
        throw new Error('Nilai register harus 0-65535');
      }
      payload.value = form.value;
    } else if (isWriteMultiple.value) {
      const tokens = form.valuesText
        .split(/[\s,]+/)
        .map((token) => token.trim())
        .filter((token) => token.length > 0);

      if (tokens.length === 0) {
        throw new Error('Masukkan minimal satu nilai register');
      }
      if (tokens.length > 123) {
        throw new Error('Maksimal 123 register per permintaan');
      }

      const values = tokens.map((token) => {
        const parsed = Number(token);
        if (!Number.isFinite(parsed) || Number.isNaN(parsed)) {
          throw new Error(`Nilai tidak valid: ${token}`);
        }
        if (parsed < 0 || parsed > 0xffff) {
          throw new Error(`Nilai berada di luar rentang 0-65535: ${token}`);
        }
        return parsed;
      });

      payload.values = values;
    }

    const response = await pollModbus(payload);
    result.value = typeof response === 'string' ? JSON.parse(response) : response;
  } catch (err) {
    errorMessage.value = err?.message ?? 'Gagal mengirim permintaan poll';
  } finally {
    polling.value = false;
  }
}
</script>
