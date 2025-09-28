import { createApp } from 'vue';
import App from './App.vue';
import router from './router';
import './assets/main.css';
import { registerCharts } from './plugins/echarts';

const app = createApp(App);

registerCharts(app);
app.use(router);
app.mount('#app');
