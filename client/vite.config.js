import { defineConfig } from 'vite';
import vue from '@vitejs/plugin-vue';

export default defineConfig({
  plugins: [vue()],
  server: {
    host: true, // listen on all network interfaces (useful for containers / mobile testing)
    port: 4173,
    open: false,
    proxy: {
      '/api': {
        target: 'http://192.168.111.29',
        changeOrigin: true,
        secure: false,
        // Allow websocket proxying if needed and avoid premature timeouts for long-lived SSE
        ws: true,
        timeout: 0,
      },
      // Proxy SSE endpoint(s) so EventSource can connect to the device during dev
      // Keep only /api proxied; SSE stream will live under /api/sse/stream
    },
  },
});
