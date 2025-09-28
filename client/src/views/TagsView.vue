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

    <div
      v-if="loading"
      class="rounded-2xl border border-slate-800 bg-slate-950/60 px-6 py-5 text-sm text-slate-300"
    >
      Memuat metadata tag...
    </div>
    <div
      v-else
      class="space-y-8"
    >
      <div
        v-if="error"
        class="rounded-2xl border border-rose-500/40 bg-rose-500/10 px-6 py-5 text-sm text-rose-200"
      >
        {{ error }}
      </div>

      <section class="rounded-2xl border border-slate-800 bg-slate-950/60 p-6 shadow-lg shadow-slate-950/30 backdrop-blur">
        <header class="mb-4 flex items-center justify-between">
          <div>
            <h2 class="text-lg font-semibold text-white">Edit Metadata Tag</h2>
            <p class="text-xs uppercase tracking-wide text-slate-400">
              Ubah label, lokasi, dan informasi lainnya lalu simpan ke SD card.
            </p>
          </div>
          <div class="flex items-center gap-2 text-xs">
            <button
              class="inline-flex items-center gap-2 rounded-full border border-slate-700/60 bg-slate-900/70 px-4 py-2 transition hover:border-brand-500/40 hover:bg-brand-500/10"
              @click="handleReload"
              :disabled="saving"
            >
              <Icon icon="mdi:refresh" class="h-4 w-4" />
              Muat Ulang
            </button>
          </div>
        </header>

        <textarea
          v-model="editorText"
          class="w-full rounded-xl border border-slate-800 bg-slate-950/80 px-4 py-3 font-mono text-xs leading-relaxed text-slate-200 focus:border-brand-500/60 focus:outline-none focus:ring-0"
          rows="18"
        ></textarea>

        <div class="mt-3 flex flex-wrap items-center justify-between gap-3 text-xs">
          <div class="space-y-1">
            <p v-if="parseError" class="text-rose-300">Kesalahan JSON: {{ parseError }}</p>
            <p v-else-if="successMessage" class="text-emerald-300">{{ successMessage }}</p>
            <p v-else class="text-slate-400">Gunakan format JSON yang sesuai dengan struktur <code>{ groups: [...] }</code>.</p>
            <p v-if="isDirty && !parseError" class="text-amber-300">Perubahan belum disimpan.</p>
          </div>
          <div class="flex items-center gap-2">
            <button
              class="inline-flex items-center gap-2 rounded-full border border-slate-700/60 bg-slate-900/70 px-4 py-2 transition hover:border-slate-500/60 hover:bg-slate-800/70"
              @click="handleReset"
              :disabled="saving || !isDirty"
            >
              <Icon icon="mdi:undo" class="h-4 w-4" />
              Reset
            </button>
            <button
              class="inline-flex items-center gap-2 rounded-full border border-brand-500/60 bg-brand-500/15 px-4 py-2 text-brand-100 transition hover:bg-brand-500/25 disabled:cursor-not-allowed disabled:opacity-60"
              @click="handleSave"
              :disabled="saving || !!parseError || !isDirty"
            >
              <Icon :icon="saving ? 'mdi:progress-check' : 'mdi:content-save'" class="h-4 w-4" />
              <span v-if="saving" class="animate-pulse">Menyimpan...</span>
              <span v-else>Simpan</span>
            </button>
          </div>
        </div>
      </section>

      <div v-if="saveError" class="rounded-2xl border border-rose-500/40 bg-rose-500/10 px-6 py-5 text-sm text-rose-200">
        {{ saveError }}
      </div>

      <TagCatalog :groups="previewGroups" />
    </div>
  </div>
</template>

<script setup>
import { Icon } from '@iconify/vue';
import { computed, onMounted, ref, watch } from 'vue';
import { RouterLink } from 'vue-router';
import TagCatalog from '../components/TagCatalog.vue';
import { fetchTagMetadata, saveTagMetadata } from '../services/api';

const loading = ref(true);
const error = ref('');
const saveError = ref('');
const successMessage = ref('');
const saving = ref(false);

const editorText = ref('');
const lastSavedText = ref('');
const parseError = ref('');
const preview = ref({ groups: [] });

const previewGroups = computed(() => (Array.isArray(preview.value?.groups) ? preview.value.groups : []));
const isDirty = computed(() => editorText.value.trim() !== lastSavedText.value.trim());

watch(editorText, (val) => {
  try {
    const parsed = JSON.parse(val);
    preview.value = parsed;
    parseError.value = '';
  } catch (err) {
    parseError.value = err instanceof Error ? err.message : String(err);
  }
  successMessage.value = '';
  saveError.value = '';
});

async function loadMetadata() {
  loading.value = true;
  error.value = '';
  try {
    const data = await fetchTagMetadata();
    const normalized = data && typeof data === 'object' ? data : { groups: [] };
    editorText.value = JSON.stringify(normalized, null, 2);
    lastSavedText.value = editorText.value;
    preview.value = normalized;
    parseError.value = '';
  } catch (err) {
    error.value = err instanceof Error ? err.message : String(err);
  } finally {
    loading.value = false;
  }
}

async function handleSave() {
  if (parseError.value) return;
  saveError.value = '';
  successMessage.value = '';
  saving.value = true;
  try {
    await saveTagMetadata(preview.value);
    lastSavedText.value = editorText.value;
    successMessage.value = 'Metadata tag berhasil disimpan.';
  } catch (err) {
    saveError.value = err instanceof Error ? err.message : String(err);
  } finally {
    saving.value = false;
  }
}

function handleReset() {
  editorText.value = lastSavedText.value;
  successMessage.value = '';
  saveError.value = '';
}

async function handleReload() {
  await loadMetadata();
  successMessage.value = '';
  saveError.value = '';
}

onMounted(async () => {
  await loadMetadata();
});
</script>
