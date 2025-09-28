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
        v-for="group in tagGroups"
        :key="group.title"
        class="rounded-2xl border border-slate-800 bg-slate-950/70 p-6 shadow-lg shadow-slate-950/30 backdrop-blur"
      >
        <header class="flex items-center justify-between">
          <div class="flex items-center gap-3">
            <Icon :icon="group.icon" class="h-8 w-8" :class="group.iconClass" />
            <div>
              <h3 class="text-base font-semibold text-white">{{ group.title }}</h3>
              <p class="text-xs uppercase tracking-wide text-slate-400">{{ group.subtitle }}</p>
            </div>
          </div>
          <span class="rounded-full border border-slate-700/60 bg-slate-900/80 px-3 py-1 text-xs text-slate-300">
            {{ group.tags.length }} tag
          </span>
        </header>

        <div class="mt-5 space-y-3 text-sm">
          <div
            v-for="tag in group.tags"
            :key="tag.id"
            class="rounded-xl border border-slate-800/80 bg-slate-900/60 px-4 py-3"
          >
            <div class="flex items-center justify-between">
              <div>
                <p class="text-sm font-semibold text-white">{{ tag.id }}</p>
                <p class="text-xs uppercase tracking-wide text-slate-400">{{ tag.type }}</p>
              </div>
              <span class="rounded-full border border-brand-500/40 bg-brand-500/10 px-3 py-1 text-xs text-brand-200">
                {{ tag.direction }}
              </span>
            </div>
            <dl class="mt-3 space-y-1 text-xs text-slate-300">
              <div class="flex justify-between">
                <dt class="text-slate-500">Pin</dt>
                <dd class="text-slate-100">{{ tag.pinLabel }}</dd>
              </div>
              <div v-if="tag.range" class="flex justify-between">
                <dt class="text-slate-500">Rentang</dt>
                <dd class="text-slate-100">{{ tag.range }}</dd>
              </div>
              <div v-if="tag.notes" class="flex justify-between">
                <dt class="text-slate-500">Catatan</dt>
                <dd class="text-right text-slate-200">{{ tag.notes }}</dd>
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

const tagGroups = [
  {
    title: 'Analog Input 0–10 V',
    subtitle: 'AI1 – AI3',
    icon: 'mdi:water',
    iconClass: 'text-sky-400',
    tags: [
      {
        id: 'AI1',
        type: 'Analog voltage',
        direction: 'Input',
        pinLabel: 'GPIO 35',
        range: '0 – 10 V (0 – 10 bar)',
        notes: 'Tekanan reservoir utama (kalibrasi linier)',
      },
      {
        id: 'AI2',
        type: 'Analog voltage',
        direction: 'Input',
        pinLabel: 'GPIO 34',
        range: '0 – 10 V (0 – 10 bar)',
        notes: 'Tekanan reservoir sekunder',
      },
      {
        id: 'AI3',
        type: 'Analog voltage',
        direction: 'Input',
        pinLabel: 'GPIO 36',
        range: '0 – 10 V (0 – 10 bar)',
        notes: 'Cadangan / booster pump',
      },
    ],
  },
  {
    title: 'Analog Current 4–20 mA',
    subtitle: 'ADS_A0 – ADS_A1',
    icon: 'mdi:current-dc',
    iconClass: 'text-amber-400',
    tags: [
      {
        id: 'ADS_A0',
        type: 'Analog current (ADS1115)',
        direction: 'Input',
        pinLabel: 'ADS1115 CH0',
        range: '4 – 20 mA (0 – 10 bar)',
        notes: 'Loop sensor level via TP5551',
      },
      {
        id: 'ADS_A1',
        type: 'Analog current (ADS1115)',
        direction: 'Input',
        pinLabel: 'ADS1115 CH1',
        range: '4 – 20 mA (0 – 10 bar)',
        notes: 'Loop sensor cadangan',
      },
    ],
  },
  {
    title: 'Digital Input',
    subtitle: 'DI1 – DI4',
    icon: 'mdi:flash',
    iconClass: 'text-emerald-400',
    tags: [
      {
        id: 'DI1',
        type: 'Digital input',
        direction: 'Input',
        pinLabel: 'GPIO 27',
        range: '3.3 – 24 V',
        notes: 'Status flow switch / level R1',
      },
      {
        id: 'DI2',
        type: 'Digital input',
        direction: 'Input',
        pinLabel: 'GPIO 26',
        range: '3.3 – 24 V',
        notes: 'Status flow switch / level R2',
      },
      {
        id: 'DI3',
        type: 'Digital input',
        direction: 'Input',
        pinLabel: 'GPIO 25',
        range: '3.3 – 24 V',
        notes: 'Sensor pintu panel / alarm eksternal',
      },
      {
        id: 'DI4',
        type: 'Digital input',
        direction: 'Input',
        pinLabel: 'GPIO 33',
        range: '3.3 – 24 V',
        notes: 'Cadangan digital input',
      },
    ],
  },
  {
    title: 'Digital Output',
    subtitle: 'DO1 – DO4',
    icon: 'mdi:toggle-switch',
    iconClass: 'text-fuchsia-400',
    tags: [
      {
        id: 'DO1',
        type: 'Digital output',
        direction: 'Output',
        pinLabel: 'GPIO 15',
        range: '3.3 V (sink/source)',
        notes: 'Relay pompa utama / valve',
      },
      {
        id: 'DO2',
        type: 'Digital output',
        direction: 'Output',
        pinLabel: 'GPIO 13',
        range: '3.3 V (sink/source)',
        notes: 'Relay pompa cadangan',
      },
      {
        id: 'DO3',
        type: 'Digital output',
        direction: 'Output',
        pinLabel: 'GPIO 12',
        range: '3.3 V (sink/source)',
        notes: 'Sirine / lampu peringatan',
      },
      {
        id: 'DO4',
        type: 'Digital output',
        direction: 'Output',
        pinLabel: 'GPIO 14',
        range: '3.3 V (sink/source)',
        notes: 'Cadangan digital output',
      },
    ],
  },
  {
    title: 'Komunikasi & Antarmuka',
    subtitle: 'RS485 & Periferal',
    icon: 'mdi:access-point-network',
    iconClass: 'text-indigo-400',
    tags: [
      {
        id: 'RS485',
        type: 'Differential bus',
        direction: 'Bidirectional',
        pinLabel: 'RX16 / TX17 / DE4',
        notes: 'Modbus RTU (half-duplex)',
      },
      {
        id: 'I2C',
        type: 'Serial bus',
        direction: 'Bidirectional',
        pinLabel: 'SDA21 / SCL22',
        notes: 'RTC DS3231 & ADS1115',
      },
      {
        id: 'SD',
        type: 'SPI storage',
        direction: 'Bidirectional',
        pinLabel: 'CS5 / MOSI23 / MISO19 / SCK18',
        notes: 'Logging & static assets',
      },
    ],
  },
];
</script>
