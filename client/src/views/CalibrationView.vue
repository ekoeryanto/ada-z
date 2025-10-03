<template>
  <div class="space-y-6">
    <header>
      <h1 class="text-2xl font-bold">Sensor Calibration</h1>
      <p class="mt-1 text-slate-400">
        View and manage calibration settings for all connected sensors.
      </p>
    </header>

    <div v-if="loading" class="text-center">Loading calibration data...</div>
    <div v-if="error" class="text-red-500">{{ error }}</div>

    <div v-if="calibrationData" class="space-y-8">
      <section v-for="(sensors, group) in groupedSensors" :key="group">
        <h2 class="text-lg font-semibold text-white">{{ group }}</h2>
        <div class="mt-4 overflow-x-auto">
          <table class="min-w-full divide-y divide-slate-800">
            <thead class="bg-slate-900">
              <tr>
                <th class="px-4 py-2 text-left text-sm font-semibold text-slate-300">Tag</th>
                <th class="px-4 py-2 text-left text-sm font-semibold text-slate-300">Pin</th>
                <th class="px-4 py-2 text-left text-sm font-semibold text-slate-300">Zero Raw ADC</th>
                <th class="px-4 py-2 text-left text-sm font-semibold text-slate-300">Span Raw ADC</th>
                <th class="px-4 py-2 text-left text-sm font-semibold text-slate-300">Zero Pressure</th>
                <th class="px-4 py-2 text-left text-sm font-semibold text-slate-300">Span Pressure</th>
                <th class="px-4 py-2 text-left text-sm font-semibold text-slate-300">Actions</th>
              </tr>
            </thead>
            <tbody class="divide-y divide-slate-800">
              <tr v-for="sensor in sensors" :key="sensor.tag">
                <td class="px-4 py-2 text-sm text-slate-300">{{ sensor.tag }}</td>
                <td class="px-4 py-2 text-sm text-slate-300">{{ sensor.pin }}</td>
                <td class="px-4 py-2 text-sm text-slate-300">{{ sensor.zero_raw_adc }}</td>
                <td class="px-4 py-2 text-sm text-slate-300">{{ sensor.span_raw_adc }}</td>
                <td class="px-4 py-2 text-sm text-slate-300">{{ sensor.zero_pressure_value }}</td>
                <td class="px-4 py-2 text-sm text-slate-300">{{ sensor.span_pressure_value }}</td>
                <td class="px-4 py-2 text-sm text-slate-300">
                  <button @click="editSensor(sensor)" class="text-blue-500">Edit</button>
                  <button @click="resetSensor(sensor)" class="ml-4 text-red-500">Reset</button>
                </td>
              </tr>
            </tbody>
          </table>
        </div>
      </section>
    </div>
    <CalibrationModal
      v-if="isModalOpen"
      :sensor="selectedSensor"
      @close="isModalOpen = false"
      @saved="handleSaved"
    />
  </div>
</template>

<script setup>
import { fetchCalibrationAll, resetCalibrationPin, autoCalibrateAdc, autoCalibrateAds } from '../services/api';
import CalibrationModal from '../components/CalibrationModal.vue';

const loading = ref(true);
const error = ref(null);
const calibrationData = ref(null);
const isModalOpen = ref(false);
const selectedSensor = ref(null);

const groupedSensors = computed(() => {
  if (!calibrationData.value) return {};
  const groups = {};
  for (const key in calibrationData.value) {
    const sensor = calibrationData.value[key];
    const group = sensor.tag.startsWith('AI') ? 'Analog Inputs' : 'Other';
    if (!groups[group]) {
      groups[group] = [];
    }
    groups[group].push(sensor);
  }
  return groups;
});

async function fetchData() {
  try {
    loading.value = true;
    calibrationData.value = await fetchCalibrationAll();
  } catch (err) {
    error.value = err.message;
  } finally {
    loading.value = false;
  }
}

function editSensor(sensor) {
  selectedSensor.value = sensor;
  isModalOpen.value = true;
}

async function resetSensor(sensor) {
  if (!confirm(`Are you sure you want to reset calibration for ${sensor.tag}?`)) {
    return;
  }
  try {
    await resetCalibrationPin({ pin: sensor.pin });
    await fetchData();
  } catch (err) {
    error.value = err.message;
  }
}

function handleSaved() {
  isModalOpen.value = false;
  fetchData();
}

onMounted(fetchData);
</script>
