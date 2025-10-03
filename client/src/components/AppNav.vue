<template>
  <nav class="border-b border-slate-800 bg-slate-950/80 backdrop-blur">
    <div class="mx-auto flex max-w-6xl flex-wrap items-center justify-between gap-4 px-6 py-4">
      <RouterLink to="/" class="flex items-center gap-3 text-base font-semibold text-white">
        <Icon icon="mdi:chip" class="h-8 w-8 text-brand-400" />
        <span>press-32</span>
      </RouterLink>
      <div class="flex items-center gap-3 text-sm">
        <RouterLink
          v-for="link in links"
          :key="link.to"
          :to="link.to"
          class="rounded-full border px-4 py-2 transition"
          :class="route.path === link.to
            ? 'border-brand-500/60 bg-brand-500/15 text-brand-200'
            : 'border-transparent text-slate-300 hover:border-brand-500/30 hover:bg-brand-500/10 hover:text-brand-100'"
        >
          <div class="flex items-center gap-2">
            <Icon :icon="link.icon" class="h-4 w-4" />
            <span>{{ link.label }}</span>
          </div>
        </RouterLink>
      </div>
    </div>
  </nav>
</template>

<script setup>
import { Icon } from '@iconify/vue';
import { RouterLink, useRoute } from 'vue-router';

const route = useRoute();

const links = [
  { to: '/', label: 'Dashboard', icon: 'mdi:view-dashboard-outline' },
  { to: '/tags', label: 'Tag Directory', icon: 'mdi:tag-text-outline' },
  { to: '/modbus', label: 'Modbus', icon: 'mdi:serial-port' },
  { to: '/notifications', label: 'Notifications', icon: 'mdi:bell-outline' },
  { to: '/calibration', label: 'Calibration', icon: 'mdi:tune' },
];
</script>
