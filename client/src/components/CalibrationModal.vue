<template>
  <div class="fixed inset-0 bg-black bg-opacity-50 flex items-center justify-center">
    <div class="bg-slate-900 rounded-lg p-6 w-full max-w-md">
      <h2 class="text-lg font-semibold text-white">Edit Calibration</h2>
      <div v-if="sensor" class="mt-4 space-y-4">
        <div>
          <label class="block text-sm font-medium text-slate-400">Zero Raw ADC</label>
          <input v-model="form.zero_raw_adc" type="number" class="mt-1 block w-full bg-slate-800 border-slate-700 rounded-md shadow-sm text-white">
        </div>
        <div>
          <label class="block text-sm font-medium text-slate-400">Span Raw ADC</label>
          <input v-model="form.span_raw_adc" type="number" class="mt-1 block w-full bg-slate-800 border-slate-700 rounded-md shadow-sm text-white">
        </div>
        <div>
          <label class="block text-sm font-medium text-slate-400">Zero Pressure</label>
          <input v-model="form.zero_pressure_value" type="number" class="mt-1 block w-full bg-slate-800 border-slate-700 rounded-md shadow-sm text-white">
        </div>
        <div>
          <label class="block text-sm font-medium text-slate-400">Span Pressure</label>
          <input v-model="form.span_pressure_value" type="number" class="mt-1 block w-full bg-slate-800 border-slate-700 rounded-md shadow-sm text-white">
        </div>
      </div>
      <div class="mt-6 flex justify-end space-x-4">
        <button @click="$emit('close')" class="px-4 py-2 text-sm font-medium text-slate-300 bg-slate-800 rounded-md">Cancel</button>
        <button @click="save" class="px-4 py-2 text-sm font-medium text-white bg-blue-600 rounded-md">Save</button>
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref, watch } from 'vue';
import { calibratePin } from '../services/api';

const props = defineProps({
  sensor: {
    type: Object,
    default: null,
  },
});

const emit = defineEmits(['close', 'saved']);

const form = ref({
  pin: null,
  zero_raw_adc: 0,
  span_raw_adc: 0,
  zero_pressure_value: 0,
  span_pressure_value: 0,
});

watch(() => props.sensor, (newSensor) => {
  if (newSensor) {
    form.value = { ...newSensor };
  }
});

async function save() {
  try {
    await calibratePin(form.value);
    emit('saved');
    emit('close');
  } catch (error) {
    console.error(error);
    // TODO: show error to user
  }
}
</script>
