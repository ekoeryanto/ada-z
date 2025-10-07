<template>
  <div class="p-4">
    <h1 class="text-2xl font-bold">Notifications</h1>

    <div class="mt-4">
      <h2 class="text-xl font-semibold">Configuration</h2>
      <div class="mt-2 space-y-4">
        <div>
          <label for="mode" class="block text-sm font-medium">Mode</label>
          <select id="mode" v-model="config.mode" class="rounded-lg border border-slate-700 bg-slate-950/60 px-3 py-2 focus:border-brand-500/60 focus:outline-none">
            <option :value="0">Disabled</option>
            <option :value="1">HTTP</option>
            <option :value="2">SD Card</option>
            <option :value="3">HTTP and SD Card</option>
          </select>
        </div>
        <div>
          <label for="payload_type" class="block text-sm font-medium">Payload Type</label>
          <select id="payload_type" v-model="config.payload_type" class="rounded-lg border border-slate-700 bg-slate-950/60 px-3 py-2 focus:border-brand-500/60 focus:outline-none">
            <option :value="0">Simple</option>
            <option :value="1">Full</option>
          </select>
        </div>
        <button @click="saveConfig" class="inline-flex items-center px-4 py-2 border border-transparent text-sm font-medium rounded-md shadow-sm text-white bg-indigo-600 hover:bg-indigo-700 focus:outline-none focus:ring-2 focus:ring-offset-2 focus:ring-indigo-500">
          Save Configuration
        </button>
      </div>
    </div>

    <div class="mt-8">
      <h2 class="text-xl font-semibold">Trigger Notification</h2>
      <div class="mt-2">
        <button @click="triggerNotification" class="inline-flex items-center px-4 py-2 border border-transparent text-sm font-medium rounded-md shadow-sm text-white bg-green-600 hover:bg-green-700 focus:outline-none focus:ring-2 focus:ring-offset-2 focus:ring-green-500">
          Trigger Batch Notification
        </button>
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref, onMounted } from 'vue';
import { getNotificationsConfig, saveNotificationsConfig, triggerBatchNotification } from '../services/api';

const config = ref({
  mode: 0,
  payload_type: 0,
});

const fetchConfig = async () => {
  try {
    config.value = await getNotificationsConfig();
  } catch (error) {
    console.error('Error fetching notification config:', error);
  }
};

const saveConfig = async () => {
  try {
    await saveNotificationsConfig(config.value);
    alert('Configuration saved successfully!');
  } catch (error) {
    console.error('Error saving notification config:', error);
    alert('Failed to save configuration.');
  }
};

const triggerNotification = async () => {
  try {
    await triggerBatchNotification();
    alert('Batch notification triggered successfully!');
  } catch (error) {
    console.error('Error triggering notification:', error);
    alert('Failed to trigger notification.');
  }
};

onMounted(() => {
  fetchConfig();
});
</script>
