<template>
  <header class="border-b border-slate-800 bg-slate-950/80 backdrop-blur">
    <div class="mx-auto flex max-w-6xl flex-wrap items-center justify-between gap-4 px-6 py-5">
      <div class="flex items-center gap-4">
        <Icon icon="mdi:chip" class="h-11 w-11 text-brand-400" />
        <div>
          <p class="text-lg font-semibold text-white">Press-32 Controller</p>
          <p class="text-sm text-slate-400">
            <span class="font-medium text-slate-100">{{ deviceName }}</span>
            ·
            <span v-if="deviceIp">{{ deviceIp }}</span>
            <span v-else class="italic text-slate-500">offline</span>
          </p>
        </div>
      </div>
      <nav class="flex items-center gap-3 text-sm">
        <button
          class="inline-flex items-center gap-2 rounded-full border border-slate-700/60 bg-slate-900/80 px-4 py-2 transition hover:border-emerald-500/50"
          :disabled="syncing"
          @click="$emit('sync')"
        >
          <Icon icon="mdi:cloud-sync-outline" class="h-4 w-4 text-emerald-400" />
          <span v-if="syncing" class="animate-pulse text-emerald-300">Syncing…</span>
          <span v-else>Sync Now</span>
        </button>
        <button
          class="inline-flex items-center gap-2 rounded-full border border-brand-500/60 bg-brand-500/10 px-4 py-2 text-brand-200 transition hover:bg-brand-500/20 disabled:cursor-not-allowed disabled:opacity-50"
          :disabled="refreshing || syncing"
          @click="$emit('refresh')"
        >
          <Icon :icon="refreshing ? 'mdi:progress-clock' : 'mdi:refresh'" class="h-4 w-4" />
          <span v-if="refreshing" class="animate-pulse">Refreshing…</span>
          <span v-else>Refresh</span>
        </button>
      </nav>
    </div>
  </header>
</template>

<script setup>
import { Icon } from '@iconify/vue';

defineProps({
  deviceName: {
    type: String,
    default: 'Device',
  },
  deviceIp: {
    type: String,
    default: '',
  },
  syncing: {
    type: Boolean,
    default: false,
  },
  refreshing: {
    type: Boolean,
    default: false,
  },
});

defineEmits(['sync', 'refresh']);
</script>
