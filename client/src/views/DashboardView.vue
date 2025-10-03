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
      :loading="loading"
      :error="error"
    >
      <template #actions="{ sensor }">
        <span class="text-slate-400">
          {{ sensor.calibrationDetail }}
        </span>
      </template>
    </DashboardPanels>

    <DashboardPanels
      v-if="adsPanels.length || loading"
      title="Current Loop (4–20 mA)"
      :sensors="adsPanels"
      :loading="loading"
      :error="error"
    >
      <template #actions="{ sensor }">
        <span class="text-slate-400">{{ sensor.actionsLabel }}</span>
      </template>
    </DashboardPanels>

    <DashboardPanels
      v-if="modbusPanels.length || loading"
      title="Modbus Sensors (RS485)"
      :sensors="modbusPanels"
      :loading="loading"
      :error="error"
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
  fetchSensorReadings,
  fetchSystemStatus,
  fetchTimeStatus,
  triggerTimeSync,
} from '../services/api';

const loading = ref(true);
const error = ref('');
const syncing = ref(false);
const refreshing = ref(false);
const systemInfo = ref(null);
const timeStatus = ref(null);
const config = ref(null);
const readings = ref(null);
const refreshTimer = ref(null);

const deviceName = computed(() => systemInfo.value?.hostname || 'press-32');
const deviceIp = computed(() => systemInfo.value?.ip || '');

const palette = ['text-sky-400', 'text-indigo-400', 'text-emerald-400', 'text-amber-400'];

const adcPanels = computed(() => {
  const sensorReadings = (readings.value?.tags || []).filter((tag) => tag.source === 'adc');
  const tagConfigMap = new Map((config.value?.tags || []).map((tag) => [tag.id, tag]));

  return sensorReadings.map((sensor, index) => {
    const cfg = tagConfigMap.get(sensor.id) || {};
    const converted = sensor.value?.converted || {};
    const scaled = sensor.value?.scaled || {};
    const meta = sensor.meta || cfg.calibration || {};
    const pin = sensor.port ?? cfg.pin ?? '-';

    const calibrationDetail = meta.cal_span_pressure_value != null
      ? `Span ${Number(meta.cal_span_pressure_value).toFixed(1)} bar`
      : 'Default calibration';

    return {
      id: sensor.id,
      label: cfg.label || `Sensor ${sensor.id}`,
      icon: cfg.icon || 'mdi:water',
      iconColor: cfg.iconColor || palette[index % palette.length],
      pin,
      online: !!sensor.enabled,
      metrics: [
        {
          label: 'Pressure (filtered)',
          value: converted.filtered != null ? `${Number(converted.filtered).toFixed(2)} bar` : '—',
          emphasis: true,
        },
        {
          label: 'Pressure (raw)',
          value: converted.raw != null ? `${Number(converted.raw).toFixed(2)} bar` : '—',
        },
        {
          label: 'Voltage',
          value: scaled.filtered != null ? `${Number(scaled.filtered).toFixed(2)} V` : '—',
        },
        {
          label: 'Calibration',
          value: calibrationDetail,
        },
        {
          label: 'Last reading',
          value: readings.value?.timestamp ? formatRelativeTime(readings.value.timestamp) : '—',
        },
      ],
      calibrationDetail,
      connectionLabel: `Pin ${pin}`,
    };
  });
});

const adsPanels = computed(() => {
  const sensors = (readings.value?.tags || []).filter((tag) => tag.source === 'ads1115');
  if (!sensors.length) return [];
  return sensors.map((sensor, index) => {
    const converted = sensor.value?.converted || {};
    const scaled = sensor.value?.scaled || {};
    const meta = sensor.meta || {};
    const measurement = meta.measurement || {};
    const ma = meta.ma_smoothed ?? measurement.ma;
    const depth = meta.depth_mm;

    return {
      id: sensor.id,
      label: `Channel ${sensor.port ?? index}`,
      icon: 'mdi:current-dc',
      iconColor: 'text-orange-400',
      online: true,
      metrics: [
        {
          label: 'Pressure',
          value: converted.filtered != null ? `${Number(converted.filtered).toFixed(2)} bar` : '—',
          emphasis: true,
        },
        {
          label: 'Loop Current',
          value: ma != null ? `${Number(ma).toFixed(2)} mA` : '—',
        },
        {
          label: 'Voltage',
          value: scaled.filtered != null ? `${Number(scaled.filtered).toFixed(2)} V` : '—',
        },
        {
          label: 'Depth',
          value: depth != null ? `${Number(depth).toFixed(0)} mm` : '—',
        },
      ],
      actionsLabel: meta.tp_scale_mv_per_ma ? `${Number(meta.tp_scale_mv_per_ma).toFixed(1)} mV/mA` : '',
      connectionLabel: `ADS ch ${sensor.port ?? index}`,
    };
  });
});

const modbusPanels = computed(() => {
  const sensors = (readings.value?.tags || []).filter((tag) => tag.source === 'modbus');
  if (!sensors.length) return [];
  return sensors.map((sensor, index) => {
    const value = sensor.value || {};
    const meta = sensor.meta || {};

    return {
      id: sensor.id,
      label: `Modbus ${sensor.id}`,
      icon: 'mdi:radar',
      iconColor: 'text-teal-400',
      online: sensor.enabled,
      metrics: [
        {
          label: 'Distance (filtered)',
          value: value.filtered != null ? `${Number(value.filtered).toFixed(0)} mm` : '—',
          emphasis: true,
        },
        {
          label: 'Distance (raw)',
          value: value.raw != null ? `${Number(value.raw).toFixed(0)} mm` : '—',
        },
        {
          label: 'Temperature',
          value: meta.temperature_c != null ? `${Number(meta.temperature_c).toFixed(1)} °C` : '—',
        },
        {
          label: 'Signal Strength',
          value: meta.signal_strength != null ? meta.signal_strength : '—',
        },
      ],
      actionsLabel: '',
      connectionLabel: `Modbus Address ${sensor.port}`,
    };
  });
});

async function refreshData() {
  if (!loading.value) {
    refreshing.value = true;
  }
  error.value = '';
  try {
    const [system, cfg, sensors, time] = await Promise.all([
      fetchSystemStatus(),
      fetchConfig(),
      fetchSensorReadings(),
      fetchTimeStatus(),
    ]);

    systemInfo.value = system;
    config.value = cfg;
    readings.value = sensors;
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

onMounted(async () => {
  await refreshData();
  scheduleAutoRefresh();
});

onBeforeUnmount(() => {
  clearAutoRefresh();
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