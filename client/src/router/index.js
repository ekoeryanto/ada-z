import { createRouter, createWebHistory } from 'vue-router';

const routes = [
  {
    path: '/',
    name: 'dashboard',
    component: () => import('../views/DashboardView.vue'),
  },
  {
    path: '/tags',
    name: 'tags',
    component: () => import('../views/TagsView.vue'),
  },
  {
    path: '/modbus',
    name: 'modbus',
    component: () => import('../views/ModbusView.vue'),
  },
  {
    path: '/modbus-poll',
    name: 'modbus-poll',
    component: () => import('../views/ModbusPollView.vue'),
  },
  {
    path: '/notifications',
    name: 'notifications',
    component: () => import('../views/NotificationsView.vue'),
  },
  {
    path: '/calibration',
    name: 'calibration',
    component: () => import('../views/CalibrationView.vue'),
  },
];

const router = createRouter({
  history: createWebHistory(),
  routes,
  scrollBehavior() {
    return { top: 0 };
  },
});

export default router;
