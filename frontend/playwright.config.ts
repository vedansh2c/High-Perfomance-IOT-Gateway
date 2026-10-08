import { defineConfig } from "@playwright/test";
export default defineConfig({
  testDir: "./tests",
  workers: 1,
  timeout: 30000,
  use: {
    baseURL: process.env.PULSE_URL || "http://localhost:3000",
    viewport: { width: 1440, height: 1050 },
    headless: true,
  },
  reporter: "list",
});
