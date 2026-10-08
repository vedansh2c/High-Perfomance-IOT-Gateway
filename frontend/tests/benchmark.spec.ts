import { test, expect } from "@playwright/test";
test("measured benchmark charts and JSON/CSV exports", async ({
  page,
  request,
}) => {
  let current = await (await request.get("/api/benchmark/results")).json();
  if (
    !current.results.length &&
    !["running", "starting"].includes(current.status.state)
  ) {
    const response = await request.post("/api/benchmark/start", {
      data: {
        counts: [100],
        workers: [1],
        scenarios: ["low"],
        duration: 2,
        warmup: 1,
        repetitions: 1,
      },
    });
    expect(response.ok()).toBeTruthy();
  }
  await expect
    .poll(
      async () => {
        current = await (await request.get("/api/benchmark/results")).json();
        return new Set(current.results.map((r: { mode: string }) => r.mode))
          .size;
      },
      { timeout: 20000 },
    )
    .toBeGreaterThanOrEqual(2);
  await page.goto("/#benchmarks");
  for (const title of [
    "Acknowledged telemetry / sec",
    "Round-trip p95 · ms",
    "Gateway CPU · % of one core",
    "Gateway memory · MiB",
  ]) {
    await expect(
      page.getByRole("heading", { name: title, exact: true }),
    ).toBeVisible();
  }
  const jsonDownload = page.waitForEvent("download");
  await page.getByRole("button", { name: "JSON", exact: true }).click();
  expect((await jsonDownload).suggestedFilename()).toBe(
    "iot-gateway-benchmarks.json",
  );
  const csvDownload = page.waitForEvent("download");
  await page.getByRole("link", { name: "CSV", exact: true }).click();
  expect((await csvDownload).suggestedFilename()).toBe("benchmarks.csv");
  await page.mouse.move(0, 0);
  await page
    .locator("#benchmarks")
    .screenshot({ path: "../docs/screenshots/benchmarks.png" });
});
