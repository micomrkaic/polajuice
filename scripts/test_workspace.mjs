// The workspace in a real browser: Chromium through Playwright when both
// are available (the sandbox and any machine that ran 'npx playwright
// install'), otherwise skipped. Serves web/ itself. Checks the things the
// fake-DOM suite cannot: a render after a click, the recipe read back from
// the engine's JPEG, a slider triggering a re-render, A/B pinning, and a
// kept look surviving a reload.
import { createServer } from "node:http";
import { readFile } from "node:fs/promises";
import { extname, join } from "node:path";
import { fileURLToPath } from "node:url";

const webDir = fileURLToPath(new URL("../web/", import.meta.url));
let chromium;
try { ({ chromium } = await import("playwright")); }
catch (e) { console.log("test_workspace: playwright not installed, skipping"); process.exit(0); }
const exe = process.env.PLAYWRIGHT_CHROMIUM || "";
const types = { ".html": "text/html", ".js": "text/javascript", ".wasm": "application/wasm",
                ".json": "application/json", ".jpg": "image/jpeg", ".cube": "text/plain" };
const server = createServer(async (req, res) => {
  try {
    const path = join(webDir, decodeURIComponent(req.url.split("?")[0]).replace(/^\/+/, "") || "index.html");
    const data = await readFile(path);
    res.writeHead(200, { "content-type": types[extname(path)] || "application/octet-stream" });
    res.end(data);
  } catch (e) { res.writeHead(404); res.end(); }
});
await new Promise(r => server.listen(0, r));
const port = server.address().port;
let browser;
try {
  browser = await chromium.launch(exe ? { executablePath: exe, args: ["--no-sandbox"] } : { args: ["--no-sandbox"] });
} catch (e) { console.log("test_workspace: no usable Chromium, skipping (" + String(e).split("\n")[0] + ")"); server.close(); process.exit(0); }
const fail = m => { console.error("test_workspace:", m); process.exit(1); };
const page = await browser.newPage({ viewport: { width: 1400, height: 900 } });
const errors = [];
page.on("pageerror", e => errors.push(String(e)));
await page.goto(`http://127.0.0.1:${port}/index.html`);
await page.waitForFunction(() => document.getElementById("status").textContent.startsWith("ready"), null, { timeout: 30000 });
await page.click("#samplestrip figure");
await page.waitForFunction(() => /rendered in/.test(document.getElementById("status").textContent), null, { timeout: 60000 });
const recipe = await page.$eval("#recipe", e => e.value);
if (!recipe.startsWith("# polajuice recipe")) fail("recipe box not filled from the render");
if (!/camera=polaroid-600/.test(recipe)) fail("recipe does not carry the sample's camera");
const cli = await page.$eval("#cli", e => e.textContent);
if (!/^polajuice apply .* -c polaroid-600/.test(cli)) fail("CLI line wrong: " + cli);
await page.$eval("#ev", e => { e.value = "1"; e.dispatchEvent(new Event("input", { bubbles: true })); });
await page.waitForFunction(() => /\nev=1\n/.test(document.getElementById("recipe").value), null, { timeout: 60000 });
await page.click("#pinA");
if (await page.$eval("#viewA", e => e.disabled)) fail("pin A did not enable the A view");
await page.selectOption("#camera", "super8");
await page.waitForFunction(() => /camera=super8/.test(document.getElementById("recipe").value), null, { timeout: 60000 });
await page.waitForFunction(() => /rendered in/.test(document.getElementById("status").textContent) &&
  /super8/.test(document.getElementById("aftercap").textContent), null, { timeout: 60000 });
await page.click("#viewA");
if (!/^A — /.test(await page.$eval("#aftercap", e => e.textContent))) fail("A view caption wrong");
await page.click("#viewB");
await page.fill("#presetname", "ws test");
await page.click("#presetsave");
await page.reload();
await page.waitForFunction(() => document.getElementById("status").textContent.startsWith("ready"), null, { timeout: 30000 });
const kept = await page.$$eval("#presets option", os => os.map(o => o.value));
if (!kept.includes("ws test")) fail("kept look did not survive a reload");
await page.selectOption("#presets", "ws test");
await page.waitForTimeout(200);
if (await page.$eval("#camera", e => e.value) !== "super8") fail("restored look did not set the camera");
if (await page.$eval("#ev", e => e.value) !== "1") fail("restored look did not set exposure");
const real = errors.filter(e => !/404/.test(e));
if (real.length) fail("page errors: " + real.join(" | "));
await browser.close();
server.close();
console.log("workspace in Chromium ok: render, recipe from the JPEG, live re-render, A/B, kept looks");
