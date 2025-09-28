<template>
  <section class="space-y-4">
    <div class="flex items-center justify-between">
      <h2 class="text-base font-semibold uppercase tracking-wide text-slate-400">{{ title }}</h2>
      <slot name="headline"></slot>
    </div>

    <p v-if="error" class="rounded-xl border border-rose-500/40 bg-rose-500/10 px-4 py-3 text-sm text-rose-200">
      {{ error }}
    </p>
    <p v-else-if="loading" class="rounded-xl border border-slate-800 bg-slate-950/80 px-4 py-3 text-sm text-slate-300">
      Loading sensor data…
    </p>
    <p v-else-if="!sensors.length" class="rounded-xl border border-slate-800 bg-slate-950/80 px-4 py-3 text-sm text-slate-300">
      No sensors to display.
    </p>

    <div v-else class="grid gap-6 lg:grid-cols-3">
      <article
        v-for="sensor in sensors"
        :key="sensor.id"
        class="rounded-2xl border border-slate-800 bg-slate-950/80 p-6 shadow-lg shadow-slate-950/30 backdrop-blur transition hover:border-brand-500/60"
      >
        <header class="flex items-center justify-between">
          <div class="flex items-center gap-3">
            <Icon :icon="sensor.icon" class="h-8 w-8" :class="sensor.iconColor" />
            <div>
              <h3 class="text-base font-semibold text-white">{{ sensor.label }}</h3>
              <p class="text-xs uppercase tracking-wide text-slate-400">{{ sensor.id }}</p>
            </div>
          </div>
          <span
            :class="sensor.online ? 'border-emerald-500/30 bg-emerald-500/10 text-emerald-200' : 'border-rose-500/30 bg-rose-500/10 text-rose-200'"
            class="rounded-full border px-3 py-1 text-xs font-medium"
          >
            {{ sensor.online ? 'Online' : 'Offline' }}
          </span>
        </header>

        <dl class="mt-6 space-y-3 text-sm">
          <div
            v-for="metric in sensor.metrics"
            :key="metric.label"
            class="flex items-center justify-between"
          >
            <dt class="text-slate-400">{{ metric.label }}</dt>
            <dd :class="metric.emphasis ? 'text-xl font-semibold text-white' : 'text-slate-200'">
              {{ metric.value }}
            </dd>
          </div>
        </dl>

        <footer class="mt-6 flex items-center justify-between text-xs text-slate-500">
          <span class="flex items-center gap-2">
            <Icon icon="mdi:lan-connect" class="h-4 w-4 text-slate-400" />
            {{ sensor.connectionLabel }}
          </span>
          <slot name="actions" :sensor="sensor"></slot>
        </footer>
      </article>
    </div>
  </section>
</template>

<script setup>
import { Icon } from '@iconify/vue';

defineProps({
  title: {
    type: String,
    default: 'Sensors',
  },
  sensors: {
    type: Array,
    default: () => [],
  },
  loading: {
    type: Boolean,
    default: false,
  },
  error: {
    type: String,
    default: '',
  },
});
</script>
