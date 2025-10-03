<template>
  <section class="space-y-8">
    <header>
      <h2 class="text-lg font-semibold text-white">Tag Directory</h2>
      <p class="mt-1 text-sm text-slate-400">
        Ringkasan semua tag yang tersedia pada controller ini, lengkap dengan jenis sinyal, nomor pin, dan peran operasionalnya.
      </p>
    </header>

    <div class="grid gap-6 md:grid-cols-2 xl:grid-cols-3">
      <article
        v-for="group in groups"
        :key="group.id || group.title"
        class="rounded-2xl border border-slate-800 bg-slate-950/70 p-6 shadow-lg shadow-slate-950/30 backdrop-blur"
      >
        <header class="flex items-center justify-between">
          <div class="flex items-center gap-3">
            <Icon :icon="group.icon || 'mdi:tag-text-outline'" class="h-8 w-8" :class="group.iconColor || 'text-brand-300'" />
            <div>
              <h3 class="text-base font-semibold text-white">{{ group.title || group.id }}</h3>
              <p class="text-xs uppercase tracking-wide text-slate-400">{{ group.subtitle || '' }}</p>
            </div>
          </div>
          <span class="rounded-full border border-slate-700/60 bg-slate-900/80 px-3 py-1 text-xs text-slate-300">
            {{ group.tags?.length || 0 }} tag
          </span>
        </header>

          <div class="mt-5 space-y-3 text-sm">
          <div
            v-for="tag in group.tags || []"
            :key="tag.id"
            class="rounded-xl border border-slate-800/80 bg-slate-900/60 px-4 py-3"
          >
            <div class="flex items-center justify-between">
              <div>
                <p class="text-sm font-semibold text-white">{{ tag.id }}</p>
                <p class="text-xs uppercase tracking-wide text-slate-400">{{ tag.type || '' }}</p>
              </div>
              <span class="rounded-full border border-brand-500/40 bg-brand-500/10 px-3 py-1 text-xs text-brand-200">
                {{ (tag.direction || 'unknown').toUpperCase() }}
              </span>
            </div>
            <dl class="mt-3 space-y-1 text-xs text-slate-300">
              <div class="flex justify-between">
                <dt class="text-slate-500">Pin</dt>
                <dd class="text-slate-100">{{ tag.pin_label || tag.pin || '-' }}</dd>
              </div>
              <div v-if="tag.location" class="flex justify-between">
                <dt class="text-slate-500">Lokasi</dt>
                <dd class="text-right text-slate-200">{{ tag.location }}</dd>
              </div>
              <div v-if="tag.range" class="flex justify-between">
                <dt class="text-slate-500">Rentang</dt>
                <dd class="text-right text-slate-200">{{ tag.range }}</dd>
              </div>
              <div v-if="tag.notes" class="flex justify-between">
                <dt class="text-slate-500">Catatan</dt>
                <dd class="text-right text-slate-200">{{ tag.notes }}</dd>
              </div>
              <div class="mt-3 flex items-center justify-between">
                <div class="text-xs text-slate-400">Enabled</div>
                <div>
                  <template v-if="isAnalogTag(tag.id)">
                    <input
                      type="checkbox"
                      :checked="getEnabled(tag.id)"
                      @change="toggleTagEnabled(tag)"
                      class="h-4 w-4 rounded"
                    />
                  </template>
                  <template v-else>
                    <span class="text-xs text-slate-500">—</span>
                  </template>
                </div>
              </div>
            </dl>
          </div>
        </div>
      </article>
    </div>
  </section>
</template>

<script setup>
import { Icon } from '@iconify/vue';

defineProps({
  groups: {
    type: Array,
    default: () => [],
  },
});
</script>

<script>
import { saveSensorsConfig } from '../services/api';

export default {
  methods: {
    isAnalogTag(id) {
      return typeof id === 'string' && id.startsWith('AI');
    },
    getEnabled(id) {
      // Default to enabled if no explicit flag present in tag
      const parts = (id || '').match(/AI(\d+)/);
      if (!parts) return false;
      const idx = parseInt(parts[1], 10) - 1;
      // try to read preference from server-side tags metadata if present in tag object
      // Fallback: enabled by default
      return true;
    },
    async toggleTagEnabled(tag) {
      // Build sensors array payload for single tag update
      const parts = (tag.id || '').match(/AI(\d+)/);
      if (!parts) return;
      const idx = parseInt(parts[1], 10) - 1;
      // Toggle based on current (we assume enabled if not explicitly false)
      const currentlyEnabled = tag.enabled !== undefined ? !!tag.enabled : true;
      const payload = { sensors: [ { sensor_index: idx, enabled: !currentlyEnabled } ] };
      try {
        await saveSensorsConfig(payload);
        // reflect change locally
        tag.enabled = !currentlyEnabled ? 1 : 0;
        // Optionally provide user feedback here (toast)
      } catch (err) {
        console.error('Failed to save sensor config', err);
        // revert checkbox state if desired
      }
    },
  },
};
</script>
