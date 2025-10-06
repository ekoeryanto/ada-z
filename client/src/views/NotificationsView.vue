<template>
  <div class="p-4">
    <h1 class="text-2xl font-bold">Notifications</h1>

    <div class="mt-4">
      <h2 class="text-xl font-semibold">Configuration</h2>
      <div class="mt-2 space-y-4">
        <div>
          <label for="mode" class="block text-sm font-medium text-gray-700">Mode</label>
          <select id="mode" v-model="config.mode" class="mt-1 block w-full pl-3 pr-10 py-2 text-base border-gray-300 focus:outline-none focus:ring-indigo-500 focus:border-indigo-500 sm:text-sm rounded-md">
            <option :value="0">Disabled</option>
            <option :value="1">HTTP</option>
            <option :value="2">SD Card</option>
            <option :value="3">HTTP and SD Card</option>
          </select>
        </div>
        <div>
          <label for="payload_type" class="block text-sm font-medium text-gray-700">Payload Type</label>
          <select id="payload_type" v-model="config.payload_type" class="mt-1 block w-full pl-3 pr-10 py-2 text-base border-gray-300 focus:outline-none focus:ring-indigo-500 focus:border-indigo-500 sm:text-sm rounded-md">
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
    window.alert('Configuration saved successfully!');
  } catch (error) {
    console.error('Error saving notification config:', error);
    window.alert('Failed to save configuration.');
  }
};

const triggerNotification = async () => {
    try {
    await triggerBatchNotification();
    window.alert('Batch notification triggered successfully!');
  } catch (error) {
    console.error('Error triggering notification:', error);
    window.alert('Failed to trigger notification.');
  }
};

onMounted(() => {
  fetchConfig();
});
</script>
