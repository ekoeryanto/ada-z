const defaultTheme = require('tailwindcss/defaultTheme');

module.exports = {
  content: ['./index.html', './src/**/*.{vue,js,ts,jsx,tsx}'],
  theme: {
    extend: {
      fontFamily: {
        sans: ['Inter', ...defaultTheme.fontFamily.sans],
      },
      colors: {
        brand: {
          50: '#e8f7ff',
          100: '#c6e9ff',
          200: '#99d7ff',
          300: '#66c2ff',
          400: '#33adff',
          500: '#0097fc',
          600: '#0076c9',
          700: '#00559a',
          800: '#00386c',
          900: '#001c3d',
        },
      },
    },
  },
  plugins: [],
};
