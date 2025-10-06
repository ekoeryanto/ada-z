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

    <div v-if="!loading && !error" class="space-y-8">
      <section>
        <h2 class="text-lg font-semibold text-white">Auto Calibration</h2>
        <div class="mt-4 grid grid-cols-1 md:grid-cols-2 gap-6">
          <div class="bg-slate-900 p-6 rounded-lg">
            <h3 class="text-md font-semibold text-white">ADC Sensors (0-10V)</h3>
            <div class="mt-4 space-y-4">
              <div v-for="sensor in groupedSensors['Analog Inputs']" :key="sensor.id" class="flex items-center justify-between">
                <span class="text-sm font-medium text-slate-400">{{ sensor.id }}</span>
                <div class="flex items-center space-x-2">
                  <input v-model.number="adcTargets[sensor.id]" type="number" class="block w-32 bg-slate-800 border-slate-700 rounded-md shadow-sm text-white" placeholder="Target">
                  <button @click="runAdcAutoCalibration(sensor)" class="px-3 py-1 text-sm font-medium text-white bg-blue-600 rounded-md">
                    Calibrate
                  </button>
                </div>
              </div>
            </div>
          </div>
          <div class="bg-slate-900 p-6 rounded-lg">
            <h3 class="text-md font-semibold text-white">ADS Sensors (4-20mA)</h3>
            <div class="mt-4 space-y-4">
              <div v-for="sensor in groupedSensors['ADS Sensors']" :key="sensor.id" class="flex items-center justify-between">
                <span class="text-sm font-medium text-slate-400">{{ sensor.id }}</span>
                <div class="flex items-center space-x-2">
                  <input v-model.number="adsTargets[sensor.id]" type="number" class="block w-32 bg-slate-800 border-slate-700 rounded-md shadow-sm text-white" placeholder="Target">
                  <button @click="runAdsAutoCalibration(sensor)" class="px-3 py-1 text-sm font-medium text-white bg-blue-600 rounded-md">
                    Calibrate
                  </button>
                </div>
              </div>
            </div>
          </div>
        </div>
      </section>

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
              <template v-for="sensor in sensors" :key="sensor.id">
                <tr>
                  <td class="px-4 py-2 text-sm text-slate-300">{{ sensor.id }}</td>
                  <td class="px-4 py-2 text-sm text-slate-300">{{ sensor.port }}</td>
                  <td class="px-4 py-2 text-sm text-slate-300">{{ sensor.zero_raw_adc }}</td>
                  <td class="px-4 py-2 text-sm text-slate-300">{{ sensor.span_raw_adc }}</td>
                  <td class="px-4 py-2 text-sm text-slate-300">{{ sensor.zero_pressure_value }}</td>
                  <td class="px-4 py-2 text-sm text-slate-300">{{ sensor.span_pressure_value }}</td>
                  <td class="px-4 py-2 text-sm text-slate-300">
                    <button @click="editSensor(sensor)" class="text-blue-500">Edit</button>
                    <button @click="resetSensor(sensor)" class="ml-4 text-red-500">Reset</button>
                    <button @click="toggleDebug(sensor)" class="ml-4 text-amber-400">Debug</button>
                  </td>
                </tr>
                <tr v-if="debugVisible[sensor.id]" :key="sensor.id + '-debug'">
                  <td colspan="7" class="px-4 py-2 bg-slate-800">
                    <div class="flex items-center gap-4 mb-2">
                      <button @click="doRebaseline(sensor)" class="text-green-400">Rebaseline</button>
                      <button @click="(async () => { debugData[sensor.id]=null; debugData[sensor.id]=await debugAdc({ tag: sensor.id }); })()" class="text-sky-400">Refresh</button>
                      <button @click="saveBaselineClick(sensor)" class="text-amber-300">Save Baseline</button>
                      <div class="flex items-center space-x-2">
                        <input v-model="vinTargets[sensor.id]" placeholder="Vin (V)" class="w-20 text-slate-200 bg-slate-900 px-2 py-1 text-sm" />
                        <input v-model.number="vinSamples[sensor.id]" placeholder="Samples" class="w-20 text-slate-200 bg-slate-900 px-2 py-1 text-sm" />
                        <button @click="autoCalcClick(sensor)" class="text-indigo-300">Auto-calc Divider</button>
                      </div>
                    </div>
                    <pre class="text-xs text-slate-300 whitespace-pre-wrap">{{ debugData[sensor.id] ? JSON.stringify(debugData[sensor.id], null, 2) : 'Loading...' }}</pre>
                  </td>
                </tr>
              </template>
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
import { ref, onMounted, computed } from 'vue';
import { fetchCalibrationAll, fetchSensorReadings, resetCalibrationPin, autoCalibrateAdc, autoCalibrateAds, debugAdc, rebaselineAdc, saveAdcBaselineForTag, autoCalcDividerForTag } from '../services/api';
import CalibrationModal from '../components/CalibrationModal.vue';

const loading = ref(true);
const error = ref(null);
const calibrationData = ref(null);
const sensorReadings = ref(null);
const isModalOpen = ref(false);
const selectedSensor = ref(null);
const adcTargets = ref({});
const adsTargets = ref({});
const debugVisible = ref({});
const debugData = ref({});
const vinTargets = ref({});
const vinSamples = ref({});

const allSensors = computed(() => {
  if (!sensorReadings.value || !sensorReadings.value.tags) return [];
  const sensors = sensorReadings.value.tags.map(tag => {
    const calData = calibrationData.value ? Object.values(calibrationData.value).find(c => c.pin === tag.port) : null;
    return {
      ...tag,
      ...calData,
    };
  });
  return sensors;
});

const groupedSensors = computed(() => {
  if (!allSensors.value) return {};
  const groups = {
    'Analog Inputs': allSensors.value.filter(s => s.source === 'adc'),
    'ADS Sensors': allSensors.value.filter(s => s.source === 'ads1115'),
  };
  return groups;
});

async function fetchData() {
  try {
    loading.value = true;
    [calibrationData.value, sensorReadings.value] = await Promise.all([
      fetchCalibrationAll(),
      fetchSensorReadings(),
    ]);
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
  if (!confirm(`Are you sure you want to reset calibration for ${sensor.id}?`)) {
    return;
  }
  try {
    await resetCalibrationPin({ pin: sensor.port });
    await fetchData();
  } catch (err) {
    error.value = err.message;
  }
}

async function runAdcAutoCalibration(sensor) {
  const target = adcTargets.value[sensor.id];
  if (target === undefined) {
    window.alert('Please provide a target value.');
    return;
  }
  if (!confirm(`Are you sure you want to run auto-calibration for ${sensor.id} with target ${target} bar?`)) {
    return;
  }
  try {
    await autoCalibrateAdc({ sensors: [{ pin: sensor.port, target: target }] });
    await fetchData();
  } catch (err) {
    error.value = err.message;
  }
}

async function runAdsAutoCalibration(sensor) {
  const target = adsTargets.value[sensor.id];
  if (target === undefined) {
    window.alert('Please provide a target value.');
    return;
  }
  if (!confirm(`Are you sure you want to run auto-calibration for ${sensor.id} with target ${target} bar?`)) {
    return;
  }
  try {
    await autoCalibrateAds({ channels: [{ channel: sensor.port, target: target }] });
    await fetchData();
  } catch (err) {
    error.value = err.message;
  }
}

function handleSaved() {
  isModalOpen.value = false;
  fetchData();
}

// Handler for Save Baseline button
async function saveBaselineClick(sensor) {
  try {
    const resp = await saveAdcBaselineForTag({ tag: sensor.id });
    // use window.alert to avoid Vue resolving alert to _ctx
    window.alert(JSON.stringify(resp));
    if (debugVisible.value[sensor.id]) {
      debugData.value[sensor.id] = null;
      debugData.value[sensor.id] = await debugAdc({ tag: sensor.id });
    }
  } catch (err) {
    window.alert(`Save baseline failed: ${err.message || String(err)}`);
  }
}

// Handler for Auto-calc Divider button
async function autoCalcClick(sensor) {
  try {
    const vin = parseFloat(vinTargets.value[sensor.id]);
    if (!vin || isNaN(vin)) { window.alert('Please provide Vin'); return; }
  const samples = vinSamples.value[sensor.id] ? Math.max(1, Math.min(100, vinSamples.value[sensor.id])) : 1;
  const resp = await autoCalcDividerForTag({ tag: sensor.id, vin, samples });
    window.alert(JSON.stringify(resp));
    if (debugVisible.value[sensor.id]) {
      debugData.value[sensor.id] = null;
      debugData.value[sensor.id] = await debugAdc({ tag: sensor.id });
    }
  } catch (err) {
    window.alert(`Auto-calc failed: ${err.message || String(err)}`);
  }
}

async function toggleDebug(sensor) {
  const id = sensor.id;
  // Toggle visibility
  debugVisible.value[id] = !debugVisible.value[id];
  if (!debugVisible.value[id]) return;
  // If we already have data, keep it; otherwise fetch
  if (debugData.value[id]) return;
  try {
    debugData.value[id] = null; // loading state
    const resp = await debugAdc({ tag: id });
    debugData.value[id] = resp;
  } catch (err) {
    debugData.value[id] = { error: err.message || String(err) };
  }
}

async function doRebaseline(sensor) {
  try {
    // Call rebaseline API
    const resp = await rebaselineAdc();
    // Update debug panel for this sensor if visible
    if (debugVisible.value[sensor.id]) {
      // fetch fresh debug data
      debugData.value[sensor.id] = null;
      const fresh = await debugAdc({ tag: sensor.id });
      debugData.value[sensor.id] = fresh;
    }
    window.alert(`Rebaseline complete: ${JSON.stringify(resp)}`);
  } catch (err) {
    window.alert(`Rebaseline failed: ${err.message || String(err)}`);
  }
}

onMounted(fetchData);
</script>
