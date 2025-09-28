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

    <section class="grid gap-6 lg:grid-cols-2 xl:grid-cols-3">
      <article
        v-for="gauge in gaugeCards"
        :key="gauge.id"
        class="rounded-2xl border border-slate-800 bg-slate-950/80 p-6 shadow-xl shadow-slate-950/30 backdrop-blur"
      >
        <div class="flex items-center justify-between">
          <div>
            <p class="text-sm uppercase tracking-wide text-slate-400">{{ gauge.subtitle }}</p>
            <h2 class="text-xl font-semibold text-white">{{ gauge.title }}</h2>
          </div>
        </div>
        <div class="mt-6 h-60">
          <VChart :option="gauge.option" autoresize class="h-full" />
        </div>
        <div class="mt-4 h-20">
          <VChart :option="getSparklineOption(gauge.id)" autoresize class="h-full" />
        </div>
      </article>
    </section>

   

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

const adcOnline = computed(() =>
  (readings.value?.tags || []).filter((tag) => tag.source === 'adc' && tag.enabled).length,
);
const adcTotal = computed(() => (readings.value?.tags || []).filter((tag) => tag.source === 'adc').length);
const adsTotal = computed(() => (readings.value?.tags || []).filter((tag) => tag.source === 'ads1115').length);

const averagePressureBar = computed(() => {
  const sensors = (readings.value?.tags || []).filter(
    (tag) => tag.source === 'adc' || tag.source === 'ads1115',
  );
  if (!sensors.length) return null;
  const sum = sensors.reduce((acc, tag) => acc + (tag.value?.converted?.filtered ?? tag.value?.converted?.value ?? 0), 0);
  return sum / sensors.length;
});

const wifiRssi = computed(() => systemInfo.value?.rssi ?? null);
const wifiSsid = computed(() => systemInfo.value?.ssid ?? '');
const wifiConnected = computed(() => !!systemInfo.value?.connected);

const lastNtpIso = computed(() => timeStatus.value?.last_ntp_iso || systemInfo.value?.last_ntp_iso || null);

const palette = ['text-sky-400', 'text-indigo-400', 'text-emerald-400', 'text-amber-400'];

const lineOption = computed(() => {
  const data = (readings.value?.tags || [])
    .filter((tag) => tag.source === 'adc')
    .map((tag) => ({
      name: tag.id,
      value: tag.value?.converted?.filtered ?? tag.value?.converted?.value ?? null,
    }))
    .filter((entry) => entry.value != null);

  return {
    tooltip: { trigger: 'axis' },
    legend: { top: 0, textStyle: { color: '#94a3b8' } },
    grid: { left: 40, right: 20, top: 40, bottom: 40 },
    xAxis: {
      type: 'category',
      data: data.map((entry) => entry.name),
      axisLine: { lineStyle: { color: '#334155' } },
      axisLabel: { color: '#94a3b8' },
    },
    yAxis: {
      type: 'value',
      name: 'bar',
      axisLine: { lineStyle: { color: '#334155' } },
      splitLine: { lineStyle: { color: '#1e293b' } },
      axisLabel: { color: '#94a3b8' },
    },
    series: [
      {
        name: 'Pressure',
        type: 'line',
        smooth: true,
        data: data.map((entry) => Number(entry.value.toFixed(2))),
        lineStyle: { color: '#38bdf8' },
        itemStyle: { color: '#0ea5e9' },
        areaStyle: {
          color: {
            type: 'linear',
            x: 0,
            y: 0,
            x2: 0,
            y2: 1,
            colorStops: [
              { offset: 0, color: 'rgba(14,165,233,0.35)' },
              { offset: 1, color: 'rgba(14,165,233,0.05)' },
            ],
          },
        },
      },
    ],
  };
});

const gaugeCards = computed(() => {
  // Include both ADC and ADS sensors so ADS channels also get gauge cards
  const sensors = (readings.value?.tags || []).filter((tag) => tag.source === 'adc' || tag.source === 'ads1115');
  return sensors.map((sensor) => {
    const isAds = sensor.source === 'ads1115';
    const value = isAds
      ? sensor.value?.converted?.filtered ?? sensor.value?.converted?.value ?? 0
      : sensor.value?.converted?.filtered ?? sensor.value?.converted?.value ?? 0;
    return {
      id: sensor.id,
      title: sensor.id,
      subtitle: isAds ? 'Current Loop (mA->bar)' : 'Tekanan',
      option: {
        series: [
          {
            type: 'gauge',
            startAngle: 210,
            endAngle: -30,
            min: 0,
            max: 10,
            radius: '90%',
            axisLine: {
              lineStyle: {
                width: 10,
                color: [
                  [0.4, '#0ea5e9'],
                  [0.7, '#38bdf8'],
                  [1, '#f87171'],
                ],
              },
            },
            axisLabel: {
              color: '#94a3b8',
            },
            pointer: {
              itemStyle: { color: '#e2e8f0' },
            },
            detail: {
              fontSize: 24,
              valueAnimation: true,
              formatter: `${value.toFixed(1)} bar`,
              color: '#e2e8f0',
            },
            title: {
              offsetCenter: [0, '70%'],
              color: '#94a3b8',
            },
            data: [{ value, name: sensor.id }],
          },
        ],
      },
    };
  });
});

// Small history buffer to hold last N samples per gauge for sparklines
const historySize = 40;
const gaugeHistory = new Map(); // id -> Array<number>

function pushGaugeSample(id, value) {
  if (!gaugeHistory.has(id)) gaugeHistory.set(id, []);
  const buf = gaugeHistory.get(id);
  buf.push(Number(value || 0));
  if (buf.length > historySize) buf.shift();
}

function getSparklineOption(id) {
  const data = (gaugeHistory.get(id) || []).slice();
  return {
    grid: { left: 4, right: 4, top: 6, bottom: 6 },
    xAxis: { show: false, type: 'category', data: data.map((_, i) => i) },
    yAxis: { show: false, type: 'value' },
    series: [
      {
        type: 'line',
        showSymbol: false,
        smooth: true,
        lineStyle: { color: '#60a5fa', width: 2 },
        areaStyle: { color: 'rgba(96,165,250,0.12)' },
        data: data,
      },
    ],
  };
}

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
          label: 'Pressure',
          value: converted.filtered != null ? `${Number(converted.filtered).toFixed(2)} bar` : '—',
          emphasis: true,
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
      // update per-gauge histories so sparkline shows newest point
      updateGaugeHistories();
  } catch (err) {
    error.value = err instanceof Error ? err.message : String(err);
  } finally {
    refreshing.value = false;
    loading.value = false;
  }
}

// After each refresh, update gauge histories for sparklines
function updateGaugeHistories() {
  const sensors = (readings.value?.tags || []).filter((t) => t.source === 'adc' || t.source === 'ads1115');
  sensors.forEach((s) => {
    // For ADS, prefer converted.filtered (pressure derived from smoothed voltage)
    const val = s.value?.converted?.filtered ?? s.value?.converted?.value ?? s.value?.scaled?.filtered ?? s.value?.scaled?.value ?? 0;
    pushGaugeSample(s.id, val);
  });
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
