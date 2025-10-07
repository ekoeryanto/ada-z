<template>
  <div class="space-y-6">
    <div>
      <h1 class="text-2xl font-bold text-slate-100">SD Card Manager</h1>
      <p class="mt-1 text-sm text-slate-400">
        Browse, upload, and remove files stored on the device SD card.
      </p>
    </div>

    <div v-if="error" class="rounded-lg border border-red-500/30 bg-red-500/10 px-4 py-3 text-sm text-red-200">
      {{ error }}
    </div>
    <div v-else-if="!sdReady" class="rounded-lg border border-amber-500/40 bg-amber-500/15 px-4 py-3 text-sm text-amber-200">
      SD card not detected. Insert a card and refresh.
    </div>

    <div class="rounded-lg border border-slate-800 bg-slate-950/60 p-4">
      <div class="flex flex-wrap items-center justify-between gap-4">
        <div>
          <div class="text-xs uppercase tracking-wide text-slate-400">Current path</div>
          <div class="mt-1 font-mono text-sm text-slate-100">{{ currentPath }}</div>
          <div class="mt-2 flex flex-wrap items-center gap-2 text-xs text-slate-300">
            <button
              v-for="crumb in breadcrumbs"
              :key="crumb.path"
              class="rounded-full border border-slate-700/60 bg-slate-900/60 px-3 py-1 transition hover:border-brand-500/50 hover:text-brand-100 disabled:opacity-50"
              @click="navigate(crumb.path)"
              :disabled="crumb.path === currentPath"
            >
              {{ crumb.label }}
            </button>
          </div>
        </div>
        <div class="flex items-center gap-2">
          <button
            class="rounded-lg border border-slate-700 px-3 py-2 text-sm text-slate-200 transition hover:border-brand-500/60 hover:text-brand-100 disabled:opacity-50"
            @click="goUp"
            :disabled="currentPath === '/' || loading"
          >
            Up
          </button>
          <button
            class="rounded-lg border border-slate-700 px-3 py-2 text-sm text-slate-200 transition hover:border-brand-500/60 hover:text-brand-100 disabled:opacity-50"
            @click="refresh"
            :disabled="loading"
          >
            Refresh
          </button>
        </div>
      </div>
      <div class="mt-4 grid gap-4 sm:grid-cols-3">
        <div class="rounded-lg border border-slate-800 bg-slate-900/70 p-4">
          <div class="text-xs uppercase tracking-wide text-slate-400">Used</div>
          <div class="mt-2 text-lg font-semibold text-slate-100">{{ formatBytes(stats.used_bytes) }}</div>
        </div>
        <div class="rounded-lg border border-slate-800 bg-slate-900/70 p-4">
          <div class="text-xs uppercase tracking-wide text-slate-400">Free</div>
          <div class="mt-2 text-lg font-semibold text-slate-100">{{ formatBytes(stats.free_bytes) }}</div>
        </div>
        <div class="rounded-lg border border-slate-800 bg-slate-900/70 p-4">
          <div class="text-xs uppercase tracking-wide text-slate-400">Total</div>
          <div class="mt-2 text-lg font-semibold text-slate-100">{{ formatBytes(stats.total_bytes) }}</div>
        </div>
      </div>
    </div>

    <div class="rounded-lg border border-slate-800 bg-slate-950/60 p-4">
      <div class="flex flex-col gap-3 md:flex-row md:items-end md:gap-4">
        <div class="flex items-center gap-2">
          <input
            v-model="newFolderName"
            type="text"
            placeholder="New folder name"
            class="w-full rounded-lg border border-slate-800 bg-slate-900/70 px-3 py-2 text-sm text-slate-100 placeholder:text-slate-500 focus:border-brand-500/60 focus:outline-none focus:ring-0 md:w-56"
          />
          <button
            class="rounded-lg border border-brand-500/50 bg-brand-500/20 px-3 py-2 text-sm font-medium text-brand-100 transition hover:bg-brand-500/30 disabled:opacity-50"
            @click="createFolder"
            :disabled="!newFolderName || loading"
          >
            Create
          </button>
        </div>
        <div>
          <label
            class="inline-flex cursor-pointer items-center justify-center gap-2 rounded-lg border border-slate-700 px-3 py-2 text-sm text-slate-200 transition hover:border-brand-500/60 hover:text-brand-100"
          >
            <Icon icon="mdi:upload" class="h-4 w-4" />
            <span>Upload File</span>
            <input type="file" class="hidden" @change="handleUpload" />
          </label>
        </div>
      </div>
    </div>

    <div class="overflow-hidden rounded-lg border border-slate-800 bg-slate-950/40">
      <table class="min-w-full divide-y divide-slate-800 text-sm">
        <thead class="bg-slate-900/60 text-xs uppercase text-slate-400">
          <tr>
            <th class="px-4 py-3 text-left font-medium">Name</th>
            <th class="px-4 py-3 text-left font-medium">Type</th>
            <th class="px-4 py-3 text-right font-medium">Size</th>
            <th class="px-4 py-3 text-right font-medium">Actions</th>
          </tr>
        </thead>
        <tbody>
          <tr v-if="loading">
            <td colspan="4" class="px-4 py-6 text-center text-slate-400">Loading...</td>
          </tr>
          <tr v-else-if="!entries.length">
            <td colspan="4" class="px-4 py-6 text-center text-slate-500">Folder is empty.</td>
          </tr>
          <tr v-for="entry in entries" :key="entry.path" class="divide-y divide-slate-900/40">
            <td class="px-4 py-3">
              <button
                v-if="entry.is_dir"
                class="flex items-center gap-2 text-left text-slate-100 transition hover:text-brand-200"
                @click="open(entry)"
              >
                <Icon icon="mdi:folder" class="h-4 w-4 text-brand-300" />
                <span class="font-medium">{{ entry.name }}</span>
              </button>
              <div v-else class="flex items-center gap-2 text-slate-100">
                <Icon icon="mdi:file" class="h-4 w-4 text-slate-400" />
                <span class="font-medium">{{ entry.name }}</span>
              </div>
            </td>
            <td class="px-4 py-3 text-slate-400">
              {{ entry.is_dir ? 'Directory' : 'File' }}
            </td>
            <td class="px-4 py-3 text-right text-slate-300">
              {{ entry.is_dir ? '--' : formatBytes(entry.size) }}
            </td>
            <td class="px-4 py-3 text-right">
              <div class="flex justify-end gap-2">
                <button
                  v-if="!entry.is_dir"
                  class="rounded-lg border border-slate-700 px-3 py-1 text-xs text-slate-200 transition hover:border-brand-500/60 hover:text-brand-100"
                  @click="download(entry)"
                >
                  Download
                </button>
                <button
                  class="rounded-lg border border-red-500/40 px-3 py-1 text-xs text-red-200 transition hover:border-red-500/70 hover:text-red-100"
                  @click="removeEntry(entry)"
                >
                  Delete
                </button>
              </div>
            </td>
          </tr>
        </tbody>
      </table>
    </div>
  </div>
</template>

<script setup>
import { computed, onMounted, ref } from 'vue';
import { Icon } from '@iconify/vue';
import {
  buildSdFileUrl,
  createSdDirectory,
  deleteSdEntry,
  listSdFiles,
  uploadSdFile,
} from '../services/api';

const currentPath = ref('/');
const entries = ref([]);
const stats = ref({ total_bytes: 0, used_bytes: 0, free_bytes: 0 });
const loading = ref(false);
const error = ref('');
const sdReady = ref(true);
const newFolderName = ref('');

const formatBytes = (value) => {
  const bytes = Number(value || 0);
  const units = ['B', 'KB', 'MB', 'GB', 'TB'];
  let size = bytes;
  let unitIndex = 0;
  while (size >= 1024 && unitIndex < units.length - 1) {
    size /= 1024;
    unitIndex += 1;
  }
  const formatted = size >= 100 || unitIndex === 0 ? size.toFixed(0) : size.toFixed(1);
  return `${formatted} ${units[unitIndex]}`;
};

const parentPath = (path) => {
  if (!path || path === '/') return '/';
  const last = path.lastIndexOf('/');
  if (last <= 0) return '/';
  return path.substring(0, last);
};

const breadcrumbs = computed(() => {
  const crumbs = [{ label: 'root', path: '/' }];
  const segments = currentPath.value.split('/').filter(Boolean);
  let acc = '';
  segments.forEach((segment) => {
    acc += `/${segment}`;
    crumbs.push({ label: segment, path: acc });
  });
  return crumbs;
});

const normalizeEntries = (items) => {
  if (!Array.isArray(items)) return [];
  const normalized = items.map((item) => ({
    name: item.name || '',
    path: item.path || '/',
    is_dir: item.is_dir === 1 || item.is_dir === true,
    size: Number(item.size || 0),
  }));
  normalized.sort((a, b) => {
    if (a.is_dir !== b.is_dir) return a.is_dir ? -1 : 1;
    return a.name.localeCompare(b.name);
  });
  return normalized;
};

const loadEntries = async (path = currentPath.value) => {
  loading.value = true;
  error.value = '';
  try {
    const data = await listSdFiles(path);
    currentPath.value = data.path || path;
    sdReady.value = data.sd_ready === 1;
    entries.value = normalizeEntries(data.entries);
    stats.value = {
      total_bytes: Number(data.total_bytes || 0),
      used_bytes: Number(data.used_bytes || 0),
      free_bytes: Number(data.free_bytes || 0),
    };
  } catch (err) {
    const message = err instanceof Error ? err.message : String(err);
    const cardMissing = message.toLowerCase().includes('sd card not ready');
    sdReady.value = !cardMissing ? sdReady.value : false;
    entries.value = [];
    stats.value = { total_bytes: 0, used_bytes: 0, free_bytes: 0 };
    error.value = cardMissing ? '' : message;
  } finally {
    loading.value = false;
  }
};

const refresh = () => loadEntries(currentPath.value);

const goUp = () => {
  const parent = parentPath(currentPath.value);
  if (parent !== currentPath.value) {
    loadEntries(parent);
  }
};

const navigate = (path) => {
  if (path === currentPath.value) return;
  loadEntries(path);
};

const open = (entry) => {
  if (entry.is_dir) {
    loadEntries(entry.path);
  }
};

const download = (entry) => {
  const url = buildSdFileUrl(entry.path, { download: true });
  window.open(url, '_blank');
};

const removeEntry = async (entry) => {
  if (!confirm(`Delete ${entry.is_dir ? 'directory' : 'file'} "${entry.name}"?`)) return;
  try {
    await deleteSdEntry(entry.path);
    await loadEntries(currentPath.value);
  } catch (err) {
    alert(`Failed to delete entry: ${err instanceof Error ? err.message : err}`);
  }
};

const createFolder = async () => {
  const name = newFolderName.value.trim();
  if (!name) return;
  try {
    await createSdDirectory(currentPath.value, name);
    newFolderName.value = '';
    await loadEntries(currentPath.value);
  } catch (err) {
    alert(`Failed to create folder: ${err instanceof Error ? err.message : err}`);
  }
};

const handleUpload = async (event) => {
  const files = event.target.files;
  if (!files || !files.length) return;
  const file = files[0];
  try {
    await uploadSdFile(currentPath.value, file);
    await loadEntries(currentPath.value);
  } catch (err) {
    alert(`Failed to upload file: ${err instanceof Error ? err.message : err}`);
  } finally {
    event.target.value = '';
  }
};

onMounted(() => {
  loadEntries('/');
});
</script>
