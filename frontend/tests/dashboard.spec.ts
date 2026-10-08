import { test, expect } from "@playwright/test";
test("live fleet, fault controls, stop, reset, mobile layout", async ({
  page,
  request,
}) => {
  await request.post("/api/simulation/stop", { data: {} });
  const preparation = await request.post("/api/simulation/start", {
    data: { devices: 500, rate: 1, duration: 1 },
  });
  expect(preparation.ok()).toBeTruthy();
  await expect
    .poll(
      async () => {
        const result = await (await request.get("/api/metrics")).json();
        return result.simulation.state;
      },
      { timeout: 10000 },
    )
    .toBe("finished");
  const errors: string[] = [];
  page.on("pageerror", (e) => errors.push(e.message));
  await page.goto("/");
  await expect(
    page.getByText("Live connection", { exact: true }),
  ).toBeVisible();
  await expect(
    page.getByRole("heading", { name: "Every connection. Every pulse." }),
  ).toBeVisible();
  await page.getByLabel("Devices", { exact: true }).fill("100");
  await page.getByLabel("Duration · seconds").fill("30");
  await page
    .getByRole("button", { name: "Start simulation", exact: true })
    .click();
  await expect(
    page.getByRole("button", { name: "Stop", exact: true }),
  ).toBeEnabled();
  await expect(page.getByText("PG-00001", { exact: true })).toBeVisible({
    timeout: 10000,
  });
  await expect(
    page.getByRole("button", { name: "Start simulation", exact: true }),
  ).toBeDisabled();
  await page.evaluate(() => window.scrollTo(0, 0));
  await page.mouse.move(0, 0);
  await page.screenshot({
    path: "../docs/screenshots/dashboard.png",
    fullPage: true,
  });
  await page.getByRole("button", { name: "Stop", exact: true }).click();
  await expect(
    page.getByRole("button", { name: "Start simulation", exact: true }),
  ).toBeEnabled();
  await page
    .getByRole("button", { name: "Reset metrics", exact: true })
    .click();
  await page.getByRole("button", { name: "Fault injection" }).click();
  await expect(page.getByLabel("Partial send · bytes (0 off)")).toBeVisible();
  await page.setViewportSize({ width: 390, height: 844 });
  await expect(
    page.getByRole("button", { name: "Start simulation", exact: true }),
  ).toBeVisible();
  const dimensions = await page.evaluate(() => ({
    viewport: innerWidth,
    width: document.documentElement.scrollWidth,
  }));
  expect(dimensions.width).toBeLessThanOrEqual(dimensions.viewport);
  await page.screenshot({
    path: "../docs/screenshots/mobile.png",
    fullPage: true,
  });
  expect(errors).toEqual([]);
});
