<template>
  <div class="space-y-8 pb-12">
    <header class="space-y-2">
      <h1 class="text-2xl font-semibold text-white">Tag Directory</h1>
      <p class="text-sm text-slate-400">
        Daftar lengkap seluruh tag analog, digital, serta interface komunikasi pada controller ini.
        Gunakan referensi ini saat melakukan wiring, konfigurasi, atau troubleshooting di lapangan.
      </p>
      <RouterLink
        to="/"
        class="inline-flex items-center gap-2 rounded-full border border-slate-700/60 bg-slate-900/70 px-4 py-2 text-xs text-slate-200 transition hover:border-brand-500/40 hover:bg-brand-500/10 hover:text-brand-100"
      >
        <Icon icon="mdi:arrow-left" class="h-4 w-4" />
        Kembali ke Dashboard
      </RouterLink>
    </header>

    <TagCatalog />
  </div>
</template>

<script setup>
import { Icon } from '@iconify/vue';
import { RouterLink } from 'vue-router';
import TagCatalog from '../components/TagCatalog.vue';
</script>
