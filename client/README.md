# Press-32 Web Client

SPA dashboard for the press-32 controller built with [Vue 3](https://vuejs.org/), [Vite](https://vitejs.dev/), [Tailwind CSS](https://tailwindcss.com/) and [Iconify](https://iconify.design/).

## Getting Started

```bash
# install deps
bun install

# start dev server
bun dev
```

The dev server defaults to http://localhost:4173. (You can still use `npm run dev` if preferred.)

## Build

```bash
bun run build
bun preview
```

## Structure

- `src/App.vue` – layout shell combining summary cards with per-tag panels.
- `src/components/HeaderBar.vue` – top navigation with quick actions.
- `src/components/DashboardPanels.vue` – sample data cards using Iconify icons.
- `src/assets/main.css` – Tailwind entry point plus a few global utilities.

Tailwind `brand` colors roughly match the project palette used in firmware logs.
