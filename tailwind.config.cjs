/** @type {import('tailwindcss').Config} */
module.exports = {
  content: ['./index.html', './src/renderer/**/*.{ts,tsx}'],
  theme: {
    extend: {
      colors: {
        ink: {
          950: '#050505',
          900: '#090909',
          850: '#0d0d0e',
          800: '#151515',
          700: '#242424',
        },
      },
      boxShadow: {
        panel: '0 24px 64px rgba(0, 0, 0, 0.28)',
        glow: '0 0 32px rgba(255, 255, 255, 0.04)',
      },
      fontFamily: {
        sans: ['Inter', 'Noto Sans', 'Segoe UI', 'sans-serif'],
      },
    },
  },
  plugins: [],
};
