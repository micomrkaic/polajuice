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
if (!/\ndevelop=normal\n/.test(recipe)) fail("default development is not normal: " + recipe);
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

// 1.23: histogram from the view, clipping overlay, wipe divider, bracket sheet, loupe
await page.waitForFunction(() => /clipped high/.test(document.getElementById("histtext").textContent), null, { timeout: 30000 });
await page.click("#clipwarn");
await page.waitForFunction(() => document.getElementById("clipcanvas").style.display === "block", null, { timeout: 30000 });
await page.click("#clipwarn");
await page.click("#wipe");
if (await page.$eval("#wipeimg", e => e.style.display) !== "block") fail("wipe did not show the comparison image");
const vp = await page.$eval("#view", e => { const r = e.getBoundingClientRect(); return { x: r.left, y: r.top, w: r.width, h: r.height }; });
await page.mouse.move(vp.x + vp.w * 0.25, vp.y + vp.h * 0.5);
await page.mouse.down();
await page.mouse.move(vp.x + vp.w * 0.25, vp.y + vp.h * 0.5 + 2);
await page.mouse.up();
const clip = await page.$eval("#wipeimg", e => e.style.clipPath);
const ovW = await page.$eval("#wipeimg", e => parseFloat(e.style.width));
const insetR = parseFloat((clip.match(/inset\([^ ]+ ([\d.]+)px/) || [])[1]);
if (!(insetR > ovW * 0.6)) fail("wipe divider did not follow the pointer to 25%: " + clip + " of " + ovW);
// the comparison image is laid at the input's scale on the render's
// picture area: for super8 (a gate crop of a 4:3 photo) it is taller than
// the view and clipped top and bottom; the recipe carries no geometry line
if (/# geometry/.test(await page.$eval("#recipe", e => e.value))) fail("geometry line leaked into the recipe box");
const ovH = await page.$eval("#wipeimg", e => parseFloat(e.style.height));
const viewH = await page.$eval("#view", e => e.clientHeight);
if (!(ovH > viewH * 1.05)) fail(`wipe image not laid at input scale on super8: overlay ${ovH} vs view ${viewH}`);
const ovClip = await page.$eval("#wipeimg", e => e.style.clipPath);
const insetT = parseFloat((ovClip.match(/inset\(([\d.]+)px/) || [])[1]);
if (!(insetT > 50)) fail("wipe clip does not cut the rows outside the gate: " + ovClip);
await page.selectOption("#wipeagainst", "A");
if (!/blob:/.test(await page.$eval("#wipeimg", e => e.src))) fail("wipe against A shows no image");
await page.keyboard.press("w");
if (await page.$eval("#wipeimg", e => e.style.display) !== "none") fail("w key did not close the wipe");
await page.selectOption("#bracketaxis", "ev");
await page.click("#bracket");
await page.waitForFunction(() => /bracket: ev — click/.test(document.getElementById("status").textContent), null, { timeout: 120000 });
const cells = await page.$$eval("#sheetgrid figure img", imgs => imgs.filter(i => /blob:/.test(i.src)).length);
if (cells !== 9) fail("bracket rendered " + cells + " of 9 cells");
await page.click("#sheetgrid figure:nth-child(9)");
await page.waitForFunction(() => document.getElementById("sheet").className === "", null, { timeout: 5000 });
if (await page.$eval("#ev", e => e.value) !== "2") fail("bracket cell did not set exposure: " + await page.$eval("#ev", e => e.value));
await page.waitForFunction(() => /\nev=2\n/.test(document.getElementById("recipe").value), null, { timeout: 60000 });
await page.click("#loupe-btn");
await page.waitForFunction(() => /loupe: full size \d+×\d+ ready/.test(document.getElementById("status").textContent), null, { timeout: 120000 });
await page.mouse.move(vp.x + vp.w * 0.5, vp.y + vp.h * 0.5);
await page.mouse.move(vp.x + vp.w * 0.5 + 3, vp.y + vp.h * 0.5);
await page.waitForFunction(() => document.getElementById("loupe").style.display === "block", null, { timeout: 10000 });
const bg = await page.$eval("#loupe", e => e.style.backgroundSize);
if (!/\d+px \d+px/.test(bg)) fail("loupe has no full-size image: " + bg);
await page.keyboard.press("l");
await page.$eval("#ev", e => { e.value = "1"; e.dispatchEvent(new Event("input", { bubbles: true })); });
await page.waitForFunction(() => /\nev=1\n/.test(document.getElementById("recipe").value), null, { timeout: 60000 });

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
console.log("workspace in Chromium ok: render, recipe from the JPEG, live re-render, A/B, histogram, clipping, wipe, bracket, loupe, kept looks");
