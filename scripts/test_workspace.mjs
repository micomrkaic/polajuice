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
if (await page.$eval("#viewB", e => e.className) !== "on") fail("the first render did not bring the view to current");
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
// a render is what the knobs asked to see: it brings the view to current, caption and all
await page.click("#viewOriginal");
if (!/^original — /.test(await page.$eval("#aftercap", e => e.textContent))) fail("original view caption wrong");
await page.$eval("#ev", e => { e.value = "0.5"; e.dispatchEvent(new Event("input", { bubbles: true })); });
await page.waitForFunction(() => /\nev=0.5\n/.test(document.getElementById("recipe").value) && /rendered in/.test(document.getElementById("status").textContent), null, { timeout: 60000 });
try { await page.waitForFunction(() => document.getElementById("viewB").className === "on", null, { timeout: 10000 }); }
catch (e) { fail("a render did not bring the view to current"); }
if (!/^rendered — /.test(await page.$eval("#aftercap", e => e.textContent))) fail("caption did not follow the render: " + await page.$eval("#aftercap", e => e.textContent));
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
// at 25% the view keeps only its right three quarters and the left image its left quarter
const frame = await page.evaluate(() => window.__polajuice.frame());
if (!frame) fail("no composed frame while wiping");
const frac = (frame.divider - frame.frameLeft) / frame.frameWidth;
if (Math.abs(frac - 0.25) > 0.03) fail("wipe divider did not follow the pointer to 25%: " + frac.toFixed(3));
const vClip = parseFloat((await page.$eval("#view", e => e.style.clipPath)).match(/inset\(0(?:px)? 0(?:px)? 0(?:px)? ([\d.]+)px\)/)[1]);
if (!(vClip > 0)) fail("the view is not clipped left of the divider");
// the comparison image is laid at the input's scale on the render's
// picture area: for super8 (a gate crop of a 4:3 photo) it is taller than
// the view and clipped top and bottom; the recipe carries no geometry line
if (/# geometry/.test(await page.$eval("#recipe", e => e.value))) fail("geometry line leaked into the recipe box");
const ovH = await page.$eval("#wipeimg", e => parseFloat(e.style.height));
const viewH = await page.$eval("#view", e => e.clientHeight);
if (!(ovH > viewH * 1.05)) fail(`wipe image not laid at input scale on super8: overlay ${ovH} vs view ${viewH}`);
const ovClip = await page.$eval("#wipeimg", e => e.style.clipPath);
if (!/^inset\(0(?:px)? [\d.]+px 0(?:px)? 0(?:px)?\)$/.test(ovClip)) fail("left image clip is not the divider alone: " + ovClip);
await page.selectOption("#wipeagainst", "A");
if (!/blob:/.test(await page.$eval("#wipeimg", e => e.src))) fail("wipe against A shows no image");
await page.keyboard.press("w");
if (await page.$eval("#wipeimg", e => e.style.display) !== "none") fail("w key did not close the wipe");

// Placement across views, comparisons and formats: for the image on
// view S and the other image O, the input's center pixel must land on
// the same screen point through both. A is the polaroid pinned above
// (instant border, 1.03:1 crop); the current goes through every format.
async function checkWipeAlignment(label) {
  const res = await page.evaluate(() => {
    const H = window.__polajuice;
    const S = H.shownInfo(), O = H.imageInfo(H.leftKind());
    if (!S || !O) return { skip: true };
    if (document.getElementById("wipeagainst").value !== O.kind) return { lie: true, sel: document.getElementById("wipeagainst").value, O: O.kind };
    const vEl = document.getElementById("view");
    const r = { left: parseFloat(vEl.style.left), top: parseFloat(vEl.style.top), width: parseFloat(vEl.style.width) };
    const ov = document.getElementById("wipeimg");
    if (ov.style.display !== "block") return { hidden: true, S: S.kind, O: O.kind };
    if (!(r.width > 0)) return { hidden: true, S: S.kind, O: O.kind };
    const sc = r.width / S.geometry.outW;
    /* the file's center: invariant under straightening, which is about it */
    const p = [S.geometry.origW / 2, S.geometry.origH / 2];
    const viaS = [r.left + (S.geometry.placeX + p[0] - S.geometry.trimX - S.geometry.cropX) * sc,
                  r.top + (S.geometry.placeY + p[1] - S.geometry.trimY - S.geometry.cropY) * sc];
    const ovL = parseFloat(ov.style.left), ovT = parseFloat(ov.style.top), ovW = parseFloat(ov.style.width);
    const so = ovW / O.geometry.outW;
    const viaO = [ovL + (O.geometry.placeX + p[0] - O.geometry.trimX - O.geometry.cropX) * so,
                  ovT + (O.geometry.placeY + p[1] - O.geometry.trimY - O.geometry.cropY) * so];
    return { S: S.kind, O: O.kind, dx: viaS[0] - viaO[0], dy: viaS[1] - viaO[1], scale: so / sc };
  });
  if (res.skip) fail(label + ": no images to compare");
  if (res.lie) fail(label + `: dropdown says ${res.sel} but the left is ${res.O}`);
  if (res.hidden) fail(label + `: wipe hidden for ${res.S} vs ${res.O}`);
  if (Math.abs(res.dx) > 0.75 || Math.abs(res.dy) > 0.75 || Math.abs(res.scale - 1) > 1e-3)
    fail(label + `: misaligned ${res.S} vs ${res.O}: dx ${res.dx.toFixed(2)} dy ${res.dy.toFixed(2)} scale ${res.scale.toFixed(4)}`);
  return `${res.S}/${res.O}`;
}
const formats = [["35mm-negative", "camera"], ["toy-camera-120", "camera"], ["super8", "camera"],
                 ["polaroid-600", "camera"], ["polaroid-600", "full"], ["polaroid-packfilm", "full"]];
let checked = [];
for (const [cam, crop] of formats) {
  await page.selectOption("#camera", cam);
  if (!await page.$eval("#crop", e => e.disabled)) await page.selectOption("#crop", crop);
  await page.waitForFunction(([c, k]) => new RegExp("camera=" + c + "\\n").test(document.getElementById("recipe").value) &&
      new RegExp("crop=" + k + "\\n").test(document.getElementById("recipe").value) &&
      /rendered in/.test(document.getElementById("status").textContent), [cam, crop], { timeout: 60000 });
  await page.waitForTimeout(300);
  await page.click("#wipe");
  for (const v of ["viewB", "viewOriginal", "viewA"]) {
    await page.click("#" + v);
    for (const against of ["original", "A", "B"]) {
      await page.selectOption("#wipeagainst", against);
      checked.push(await checkWipeAlignment(`${cam}/${crop} view=${v} vs=${against}`));
    }
  }
  await page.click("#wipe");
}
await page.click("#viewB");
await page.selectOption("#camera", "super8");
await page.waitForFunction(() => /camera=super8\n/.test(document.getElementById("recipe").value) && /rendered in/.test(document.getElementById("status").textContent), null, { timeout: 60000 });
console.log(`wipe placement ok: ${formats.length} formats x 3 views x 3 comparisons (${[...new Set(checked)].join(", ")})`);
// 1.24: a scene slider re-renders, lands in the recipe, and brackets
await page.$eval("#temp", e => { e.value = "0.6"; e.dispatchEvent(new Event("input", { bubbles: true })); });
await page.waitForFunction(() => /\ntemp=0.6\n/.test(document.getElementById("recipe").value) && /rendered in/.test(document.getElementById("status").textContent), null, { timeout: 60000 });
if (!/--temp 0.6/.test(await page.$eval("#cli", e => e.textContent))) fail("CLI line lacks --temp");
await page.$eval("#temp", e => { e.value = "0"; e.dispatchEvent(new Event("input", { bubbles: true })); });
await page.waitForFunction(() => /\ntemp=0\n/.test(document.getElementById("recipe").value), null, { timeout: 60000 });
// 1.25: a straightened, trimmed render wipes against the original with the
// left image rotated back about the file's center and offset by the trim
await page.$eval("#rotate", e => { e.value = "5"; e.dispatchEvent(new Event("input", { bubbles: true })); });
await page.$eval("#trimleft", e => { e.value = "10"; e.dispatchEvent(new Event("input", { bubbles: true })); });
await page.waitForFunction(() => /\nrotate=5\n/.test(document.getElementById("recipe").value) && /\ntrimx=0.1\n/.test(document.getElementById("recipe").value) && /rendered in/.test(document.getElementById("status").textContent), null, { timeout: 60000 });
await page.waitForTimeout(300);
await page.click("#wipe"); await page.selectOption("#wipeagainst", "original");
await page.waitForTimeout(300);
{
  const t = await page.$eval("#wipeimg", e => e.style.transform);
  if (!/rotate\(5deg\)/.test(t)) fail("left image not turned with the straightened render: " + t);
  const fr = await page.evaluate(() => window.__polajuice.frame());
  const S = await page.evaluate(() => window.__polajuice.shownInfo().geometry);
  if (!(S.trimX > 0 && S.rot === 5)) fail("shown geometry lacks trim/rot: " + JSON.stringify(S));
  if (!fr) fail("no frame while wiping a trimmed render");
  const al = await checkWipeAlignment("rotate+trim view=B vs=original");
  if (!al) fail("alignment check skipped for the trimmed render");
}
await page.click("#wipe");
await page.$eval("#rotate", e => { e.value = "0"; e.dispatchEvent(new Event("input", { bubbles: true })); });
await page.$eval("#trimleft", e => { e.value = "0"; e.dispatchEvent(new Event("input", { bubbles: true })); });
await page.waitForFunction(() => /\nrotate=0\n/.test(document.getElementById("recipe").value) && /\ntrimw=0\n/.test(document.getElementById("recipe").value) && /rendered in/.test(document.getElementById("status").textContent), null, { timeout: 60000 });
await page.selectOption("#bracketaxis", "ev");
await page.click("#bracket");
await page.waitForFunction(() => /bracket: ev — click/.test(document.getElementById("status").textContent), null, { timeout: 120000 });
const cells = await page.$$eval("#sheetgrid figure img", imgs => imgs.filter(i => /blob:/.test(i.src)).length);
if (cells !== 9) fail("bracket rendered " + cells + " of 9 cells");
const evBefore = parseFloat(await page.$eval("#ev", e => e.value));
await page.click("#sheetgrid figure:nth-child(9)");
await page.waitForFunction(() => document.getElementById("sheet").className === "", null, { timeout: 5000 });
const evWant = String(+(evBefore + 1).toFixed(2));
if (await page.$eval("#ev", e => e.value) !== evWant) fail("bracket cell did not set exposure: " + await page.$eval("#ev", e => e.value) + " wanted " + evWant);
await page.waitForFunction(w => new RegExp("\\nev=" + w + "\\n").test(document.getElementById("recipe").value), evWant, { timeout: 60000 });
await page.click("#loupe-btn");
await page.waitForFunction(() => /loupe: full size \d+×\d+ ready/.test(document.getElementById("status").textContent), null, { timeout: 120000 });
await page.mouse.move(vp.x + vp.w * 0.5, vp.y + vp.h * 0.5);
await page.mouse.move(vp.x + vp.w * 0.5 + 3, vp.y + vp.h * 0.5);
await page.waitForFunction(() => document.getElementById("loupe").style.display === "block", null, { timeout: 10000 });
const bg = await page.$eval("#loupe", e => e.style.backgroundSize);
if (!/\d+px \d+px/.test(bg)) fail("loupe has no full-size image: " + bg);
// the loupe shows what is on view: the original at its own full size, A at full size
await page.click("#viewOriginal");
await page.waitForFunction(() => /loupe: full size \d+×\d+ ready \(original\)/.test(document.getElementById("status").textContent), null, { timeout: 120000 });
await page.mouse.move(vp.x + vp.w * 0.5, vp.y + vp.h * 0.5);
await page.mouse.move(vp.x + vp.w * 0.5 + 3, vp.y + vp.h * 0.5);
await page.waitForFunction(() => /^original 1:1 of/.test(document.getElementById("loupecap").textContent), null, { timeout: 10000 });
const origNat = await page.$eval("#view", e => e.naturalWidth + "×" + e.naturalHeight);
if (!(await page.$eval("#loupecap", e => e.textContent)).includes(origNat)) fail("loupe over the original is not the original's size");
await page.click("#viewA");
await page.waitForFunction(() => /loupe: full size \d+×\d+ ready \(A\)/.test(document.getElementById("status").textContent), null, { timeout: 120000 });
await page.mouse.move(vp.x + vp.w * 0.5, vp.y + vp.h * 0.5);
await page.mouse.move(vp.x + vp.w * 0.5 + 3, vp.y + vp.h * 0.5);
await page.waitForFunction(() => /^A 1:1 of/.test(document.getElementById("loupecap").textContent), null, { timeout: 10000 });
await page.click("#viewB");
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
