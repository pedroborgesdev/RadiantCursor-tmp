import { resolve } from "node:path";

import react from "@vitejs/plugin-react";
import { defineConfig } from "vite";

const rendererTarget = process.env.RADIANTCURSOR_RENDERER === "studio" ? "studio" : "normal";

export default defineConfig({
  base: "./",
  plugins: [react()],
  resolve: {
    alias: {
      "@shared": resolve(import.meta.dirname, "src/shared"),
    },
  },
  build: {
    outDir: `dist/renderer-${rendererTarget}`,
    emptyOutDir: true,
    rollupOptions: {
      input: resolve(import.meta.dirname, `${rendererTarget}.html`),
    },
  },
  server: {
    host: "127.0.0.1",
    port: 5173,
    strictPort: true,
  },
});
