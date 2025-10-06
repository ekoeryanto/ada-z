import { defineConfig } from 'vite';
import vue from '@vitejs/plugin-vue';

export default defineConfig({
  plugins: [vue()],
  server: {
    port: 4173,
    open: false,
    proxy: {
      '/api': {
        target: 'http://192.168.111.29',
        changeOrigin: true,
        secure: false,
      },
    },
  },
});
