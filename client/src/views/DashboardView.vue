<template>
  <div class="space-y-10">
    <HeaderBar
      :device-name="deviceName"
      :device-ip="deviceIp"
      :syncing="syncing"
      :refreshing="refreshing"
      @sync="handleSync"
      @refresh="refreshData"
    />

    <DashboardPanels
      title="Analog Inputs (0–10 V)"
      :sensors="adcPanels"
      :loading="sensorsLoading"
      :error="combinedError"
    >
      <template #actions="{ sensor }">
        <span class="text-slate-400">
          {{ sensor.calibrationDetail }}
        </span>
      </template>
    </DashboardPanels>

    <DashboardPanels
      v-if="adsPanels.length || sensorsLoading"
      title="Current Loop (4–20 mA)"
      :sensors="adsPanels"
      :loading="sensorsLoading"
      :error="combinedError"
    >
      <template #actions="{ sensor }">
        <span class="text-slate-400">{{ sensor.actionsLabel }}</span>
      </template>
    </DashboardPanels>

    <DashboardPanels
      v-if="modbusPanels.length || sensorsLoading"
      title="Modbus Sensors (RS485)"
      :sensors="modbusPanels"
      :loading="sensorsLoading"
      :error="combinedError"
    >
      <template #actions="{ sensor }">
        <span class="text-slate-400">{{ sensor.actionsLabel }}</span>
      </template>
    </DashboardPanels>

    <section class="rounded-2xl border border-slate-800 bg-slate-950/60 px-6 py-5 text-sm text-slate-300">
      <div class="flex flex-wrap items-center justify-between gap-3">
        <p>Butuh referensi pin dan tag lengkap? Buka katalog tag untuk melihat semua mapping hardware.</p>
        <RouterLink
          to="/tags"
          class="inline-flex items-center gap-2 rounded-full border border-brand-500/40 bg-brand-500/10 px-4 py-2 text-brand-200 transition hover:bg-brand-500/20"
        >
          <Icon icon="mdi:tag-multiple" class="h-4 w-4" />
          Buka Tag Directory
        </RouterLink>
      </div>
    </section>
  </div>
</template>

<script setup>
import { Icon } from '@iconify/vue';
import { computed, onBeforeUnmount, onMounted, ref } from 'vue';
import { RouterLink } from 'vue-router';
import HeaderBar from '../components/HeaderBar.vue';
import DashboardPanels from '../components/DashboardPanels.vue';
import {
  fetchConfig,
  fetchSystemStatus,
  fetchTimeStatus,
  triggerTimeSync,
  openSensorsEventSource,
} from '../services/api';

const loading = ref(true);
const sensorsLoading = ref(true);
const error = ref('');
const sensorsError = ref('');
const syncing = ref(false);
const refreshing = ref(false);
const systemInfo = ref(null);
const timeStatus = ref(null);
const config = ref(null);
const sensorSnapshot = ref(null);
const sensorsEventSource = ref(null);
const refreshTimer = ref(null);

const deviceName = computed(() => systemInfo.value?.hostname || 'press-32');
const deviceIp = computed(() => systemInfo.value?.ip || '');

const palette = ['text-sky-400', 'text-indigo-400', 'text-emerald-400', 'text-amber-400'];

const configTagMap = computed(() => {
  const tags = config.value?.tags || [];
  return new Map(tags.map((tag) => [tag.id, tag]));
});

const snapshotTimestamp = computed(() => sensorSnapshot.value?.timestamp ?? null);
const lastUpdateText = computed(() => (snapshotTimestamp.value ? formatRelativeTime(snapshotTimestamp.value) : '—'));
const allSensors = computed(() => sensorSnapshot.value?.sensors ?? []);

const combinedError = computed(() => sensorsError.value || error.value);

function getConfigForSensor(id) {
  return configTagMap.value.get(id) || {};
}

function titleCase(str = '') {
  return str.replace(/_/g, ' ').replace(/\b\w/g, (c) => c.toUpperCase());
}

function normalizeName(name = '') {
  return name.toLowerCase();
}

function findReading(sensor, name) {
  const lower = normalizeName(name);
  return (sensor.readings || []).find((reading) => normalizeName(reading.name || '') === lower);
}

function formatMeasurement(reading, decimals = 2) {
  if (!reading || reading.status === 'unavailable' || reading.value == null) return '—';
  const value = Number(reading.value);
  if (Number.isNaN(value)) return '—';
  const formatted = typeof decimals === 'number' ? value.toFixed(decimals) : String(value);
  return reading.unit ? `${formatted} ${reading.unit}` : formatted;
}

function formatRawValue(reading, unit, decimals = 2) {
  if (!reading || reading.raw == null) return null;
  const value = Number(reading.raw);
  if (Number.isNaN(value)) return null;
  const formatted = typeof decimals === 'number' ? value.toFixed(decimals) : String(value);
  return unit ? `${formatted} ${unit}` : formatted;
}

function formatStatus(status) {
  const lower = status ? status.toLowerCase() : '';
  switch (lower) {
    case 'ok':
      return 'OK';
    case 'alert':
      return 'Alert';
    case 'pending':
      return 'Pending';
    case 'disabled':
      return 'Disabled';
    default:
      return status ? titleCase(status) : 'Unknown';
  }
}

function statusVariant(status) {
  return (status || '').toLowerCase() || 'unknown';
}

function formatReadingLabel(reading) {
  if (!reading) return 'Value';
  const name = reading.name ? titleCase(reading.name) : 'Value';
  return reading.unit ? `${name} (${reading.unit})` : name;
}

const adcPanels = computed(() => {
  const sensors = allSensors.value.filter((sensor) => sensor.type === 'adc');
  return sensors.map((sensor, index) => {
    const cfg = getConfigForSensor(sensor.id);
    const meta = sensor.meta || {};
    const voltage = findReading(sensor, 'voltage');
    const pressure = findReading(sensor, 'pressure');

    const metrics = [
      {
        label: 'Pressure',
        value: formatMeasurement(pressure, 2),
        emphasis: true,
      },
      {
        label: 'Voltage',
        value: formatMeasurement(voltage, 3),
      },
    ];

    const rawPressure = formatRawValue(pressure, 'bar', 2);
    if (rawPressure) {
      metrics.push({ label: 'Pressure (raw)', value: rawPressure });
    }

    const rawVoltage = formatRawValue(voltage, 'V', 3);
    if (rawVoltage) {
      metrics.push({ label: 'Voltage (raw)', value: rawVoltage });
    }

    metrics.push({ label: 'Status', value: formatStatus(sensor.status) });
    metrics.push({ label: 'Last update', value: lastUpdateText.value });

    const calibrationDetail = meta.cal_span != null
      ? `Span ${Number(meta.cal_span).toFixed(1)} bar`
      : 'Default calibration';

    return {
      id: sensor.id,
      label: cfg.label || `Sensor ${sensor.id}`,
      icon: cfg.icon || 'mdi:water',
      iconColor: cfg.iconColor || palette[index % palette.length],
      connectionLabel: sensor.port != null ? `Pin ${sensor.port}` : '—',
      calibrationDetail,
      actionsLabel: calibrationDetail,
      metrics,
      online: sensor.status === 'ok',
      stateLabel: formatStatus(sensor.status),
      stateVariant: statusVariant(sensor.status),
    };
  });
});

const adsPanels = computed(() => {
  const sensors = allSensors.value.filter((sensor) => sensor.type === 'ads1115');
  return sensors.map((sensor, index) => {
    const metrics = [
      {
        label: 'Pressure',
        value: formatMeasurement(findReading(sensor, 'pressure'), 2),
        emphasis: true,
      },
      {
        label: 'Current',
        value: formatMeasurement(findReading(sensor, 'current'), 3),
      },
      {
        label: 'Voltage',
        value: formatMeasurement(findReading(sensor, 'voltage'), 3),
      },
    ];

    const depthReading = findReading(sensor, 'depth');
    if (depthReading) {
      metrics.push({ label: 'Depth', value: formatMeasurement(depthReading, 0) });
    }

    metrics.push({ label: 'Status', value: formatStatus(sensor.status) });
    metrics.push({ label: 'Last update', value: lastUpdateText.value });

    const actionsLabel = sensor.meta?.tp_scale_mv_per_ma != null
      ? `${Number(sensor.meta.tp_scale_mv_per_ma).toFixed(1)} mV/mA`
      : '';

    return {
      id: sensor.id,
      label: `Channel ${sensor.channel ?? index}`,
      icon: 'mdi:current-dc',
      iconColor: 'text-orange-400',
      connectionLabel: sensor.channel != null ? `ADS ch ${sensor.channel}` : 'ADS',
      metrics,
      actionsLabel,
      online: sensor.status === 'ok',
      stateLabel: formatStatus(sensor.status),
      stateVariant: statusVariant(sensor.status),
    };
  });
});

const modbusPanels = computed(() => {
  const sensors = allSensors.value.filter((sensor) => sensor.type === 'modbus');
  return sensors.map((sensor, index) => {
    const cfg = getConfigForSensor(sensor.id);
    const meta = sensor.meta || {};
    const readings = sensor.readings || [];

    const metrics = readings.map((reading, idx) => ({
      label: formatReadingLabel(reading),
      value: formatMeasurement(reading, 3),
      emphasis: idx === 0,
    }));

    if (!metrics.length) {
      metrics.push({ label: 'Value', value: '—', emphasis: true });
    }

    metrics.push({ label: 'Status', value: formatStatus(sensor.status) });
    metrics.push({ label: 'Last update', value: lastUpdateText.value });

    const baseLabel = cfg.label
      || meta.label
      || (readings[0]?.name ? titleCase(readings[0].name) : `Modbus ${sensor.id}`);

    const connectionParts = [];
    if (meta.slave != null) connectionParts.push(`Slave ${meta.slave}`);
    if (meta.register != null) connectionParts.push(`Reg ${meta.register}`);
    const connectionLabel = connectionParts.join(' · ') || 'Modbus';

    const actionsLabel = meta.data_type ? meta.data_type.toUpperCase() : '';

    return {
      id: sensor.id,
      label: baseLabel,
      icon: 'mdi:radar',
      iconColor: 'text-teal-400',
      connectionLabel,
      metrics,
      actionsLabel,
      online: sensor.status === 'ok',
      stateLabel: formatStatus(sensor.status),
      stateVariant: statusVariant(sensor.status),
    };
  });
});

async function refreshData() {
  if (!loading.value) {
    refreshing.value = true;
  }
  error.value = '';
  try {
    const [system, cfg, time] = await Promise.all([
      fetchSystemStatus(),
      fetchConfig(),
      fetchTimeStatus(),
    ]);

    systemInfo.value = system;
    config.value = cfg;
    timeStatus.value = time;
  } catch (err) {
    error.value = err instanceof Error ? err.message : String(err);
  } finally {
    refreshing.value = false;
    loading.value = false;
  }
}

async function handleSync() {
  try {
    syncing.value = true;
    await triggerTimeSync();
    await refreshData();
  } catch (err) {
    error.value = err instanceof Error ? err.message : String(err);
  } finally {
    syncing.value = false;
  }
}

function scheduleAutoRefresh() {
  clearAutoRefresh();
  refreshTimer.value = setInterval(refreshData, 15000);
}

function clearAutoRefresh() {
  if (refreshTimer.value) {
    clearInterval(refreshTimer.value);
    refreshTimer.value = null;
  }
}

function startSensorsStream() {
  if (typeof EventSource === 'undefined') {
    sensorsError.value = 'EventSource not supported in this browser';
    sensorsLoading.value = false;
    return;
  }

  if (sensorsEventSource.value) {
    sensorsEventSource.value.close();
    sensorsEventSource.value = null;
  }

  const es = openSensorsEventSource();
  sensorsEventSource.value = es;

  es.addEventListener('sensors', (event) => {
    try {
      const data = JSON.parse(event.data);
      sensorSnapshot.value = data;
      sensorsLoading.value = false;
      sensorsError.value = '';
    } catch (err) {
      sensorsError.value = err instanceof Error ? err.message : String(err);
    }
  });

  es.addEventListener('open', () => {
    sensorsError.value = '';
  });

  es.onerror = () => {
    if (!sensorsLoading.value) {
      sensorsError.value = 'Sensor stream disconnected';
    }
  };
}

onMounted(async () => {
  await refreshData();
  startSensorsStream();
  scheduleAutoRefresh();
});

onBeforeUnmount(() => {
  clearAutoRefresh();
  if (sensorsEventSource.value) {
    sensorsEventSource.value.close();
    sensorsEventSource.value = null;
  }
});

function formatRelativeTime(isoString) {
  if (!isoString) return '';
  const date = new Date(isoString);
  if (Number.isNaN(date.getTime())) return '';
  const diffMs = Date.now() - date.getTime();
  const diffSeconds = Math.round(diffMs / 1000);
  if (diffSeconds < 60) return 'Just now';
  const diffMinutes = Math.round(diffSeconds / 60);
  if (diffMinutes < 60) return `${diffMinutes} min ago`;
  const diffHours = Math.round(diffMinutes / 60);
  if (diffHours < 24) return `${diffHours} hr ago`;
  const diffDays = Math.round(diffHours / 24);
  return `${diffDays} day${diffDays > 1 ? 's' : ''} ago`;
}
</script>
