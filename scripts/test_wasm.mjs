// Verifies web/polajuice.wasm through web/engine.js and the vendored
// browser_wasi_shim - the exact stack the page uses - and byte-compares a
// render against the native CLI. Run: node scripts/test_wasm.mjs [native.jpg in.png]
import { readFile } from "node:fs/promises";
import { createEngine } from "../web/engine.js";

const engine = await createEngine(await readFile(new URL("../web/polajuice.wasm", import.meta.url)));
// A built-in 64x64 test card (gradient with a light patch) so the engine
// checks below run without staged samples; the shipped sample photo is
// used when present, since it also exercises JPEG/EXIF decoding.
const TEST_CARD = Buffer.from("iVBORw0KGgoAAAANSUhEUgAAAEAAAABACAIAAAAlC+aJAAAAeklEQVR42u3ZsQ3AIAwEQEeCdSkZgpJhmC9FJoCkiKV7UeLi5OrlK6KViONXX8x+NF4idQAAAAAAAAAAAAAAAAAAjgHVBgB+ARijbf1ffSbfQAkAAAAAAAAAAAAAgCd9zb0rb/oNKPU6MQAAAAAAAAAAAAAAAIBSnzo3iMsI6AYmoUAAAAAASUVORK5CYII=", "base64");
async function testInput() {
    try {
        const sample = await readFile(new URL("../web/samples/sleeping-cat.jpg",
                                             import.meta.url));
        return { bytes: sample, ext: "jpg" };
    } catch (e) {
        if (e.code !== "ENOENT") throw e;
        return { bytes: TEST_CARD, ext: "png" };
    }
}
const version = await engine.version();
const cameras = await engine.cameras();
if (cameras.length < 14) throw new Error("camera enumeration short: " + cameras.length);
const polaroid = cameras.find(c => c.name === "polaroid-600");
if (!polaroid || polaroid.film !== "polaroid_px-680")
    throw new Error("canonical film wiring broken");
if (!polaroid.traits || !polaroid.traits.includes("instant-print"))
    throw new Error("traits column missing: " + JSON.stringify(polaroid));
if (!polaroid.instant) throw new Error("instant flag not parsed");
if (cameras.find(c => c.name === "bw-35").instant)
    throw new Error("bw-35 wrongly marked instant");
const auto2 = cameras.find(c => c.name === "autochrome");
if (auto2.processes.length !== 0) throw new Error("autochrome should be sealed");
if (!cameras.find(c => c.name === "super8").processes.includes("slide"))
    throw new Error("super8 should take slide");
if (!polaroid.processes.includes("integral"))
    throw new Error("polaroid-600 should take integral");
console.log(`engine v${version}, ${cameras.length} cameras`);

// error path must throw with the engine's message, not crash
let threw = false;
try { await engine.render({ inputBytes: new Uint8Array([1,2,3]), ext: "jpg",
                            camera: "polaroid-600" }); }
catch (e) { threw = true; }
if (!threw) throw new Error("bad input did not throw");

if (process.argv[3]) {
    const input = await readFile(process.argv[3]);
    const out = await engine.render({ inputBytes: input, ext: "png",
        camera: "super8", cubeText: null, strength: 1.0, seed: 7, age: 0, maxDim: 0 });
    const native = await readFile(process.argv[2]);
    if (Buffer.compare(Buffer.from(out), native) !== 0)
        throw new Error("wasm render differs from native");
    console.log("render BYTE-IDENTICAL to native CLI");
    const preview = await engine.render({ inputBytes: input, ext: "png",
        camera: "super8", strength: 1.0, seed: 7, maxDim: 200 });
    if (preview.length >= out.length) throw new Error("preview not smaller");
    console.log("preview downscale ok");
    const aged = await engine.render({ inputBytes: input, ext: "png",
        camera: "super8", strength: 1.0, seed: 7, age: 1.0, maxDim: 0 });
    if (Buffer.compare(Buffer.from(aged), Buffer.from(out)) === 0)
        throw new Error("age had no effect");
    console.log("age axis ok");
    const pushed = await engine.render({ inputBytes: input, ext: "png",
        camera: "super8", strength: 1.0, seed: 7, develop: "push+2", maxDim: 0 });
    if (Buffer.compare(Buffer.from(pushed), Buffer.from(out)) === 0)
        throw new Error("push+2 had no effect");
    const crossed = await engine.render({ inputBytes: input, ext: "png",
        camera: "super8", strength: 1.0, seed: 7, develop: "cross", maxDim: 0 });
    if (Buffer.compare(Buffer.from(crossed), Buffer.from(pushed)) === 0)
        throw new Error("cross identical to push");
    console.log("develop axis ok (push, cross)");
    // engine-level compatibility: the wasm itself refuses nonsense pairings
    let refused = false;
    try {
        await engine.render({ inputBytes: input, ext: "png",
            camera: "35mm-slide", cubeText: "LUT_3D_SIZE 2\n0 0 0\n1 0 0\n0 1 0\n1 1 0\n0 0 1\n1 0 1\n0 1 1\n1 1 1\n",
            filmProcess: "integral", seed: 7, maxDim: 0 });
    } catch (e) { refused = /takes slide/.test(e.message); }
    if (!refused) throw new Error("engine accepted slide camera + integral film");
    const unknownOk = await engine.render({ inputBytes: input, ext: "png",
        camera: "35mm-slide", cubeText: "LUT_3D_SIZE 2\n0 0 0\n1 0 0\n0 1 0\n1 1 0\n0 0 1\n1 0 1\n0 1 1\n1 1 1\n",
        filmProcess: null, seed: 7, maxDim: 0 });
    if (!unknownOk.length) throw new Error("unknown process wrongly blocked");
    console.log("engine-level compatibility ok (refuses mismatch, allows unknown)");
    const swapCube = "LUT_3D_SIZE 2\n0 0 0\n0 1 0\n1 0 0\n1 1 0\n0 0 1\n0 1 1\n1 0 1\n1 1 1\n";
    const printed = await engine.render({ inputBytes: input, ext: "png",
        camera: "35mm-negative", cubeText: null, printCubeText: swapCube,
        seed: 7, maxDim: 0 });
    const unprinted = await engine.render({ inputBytes: input, ext: "png",
        camera: "35mm-negative", cubeText: null, seed: 7, maxDim: 0 });
    if (Buffer.compare(Buffer.from(printed), Buffer.from(unprinted)) === 0)
        throw new Error("print LUT had no effect");
    console.log("print axis ok (chained transform changes output)");
}
// a shipped sample must render through the engine (EXIF baked at build time,
// so orientation is already correct in the file itself)
try {
    const sample = await readFile(new URL("../web/samples/sleeping-cat.jpg", import.meta.url));
    const rendered = await engine.render({ inputBytes: sample, ext: "jpg",
        camera: "polaroid-600", strength: 1.0, seed: 1, maxDim: 400 });
    if (rendered.length < 1000) throw new Error("sample render suspiciously small");
    console.log("sample photo renders through the engine");
} catch (e) {
    if (e.code === "ENOENT") console.log("(no samples staged; skipping sample render)");
    else throw e;
}
{
  // arbitrary cube text with a grainy stem must render (the custom-cube path)
  const identity = 'LUT_3D_SIZE 2\n0 0 0\n1 0 0\n0 1 0\n1 1 0\n0 0 1\n1 0 1\n0 1 1\n1 1 1\n';
  const { bytes: src, ext: srcExt } = await testInput();
  const out = await engine.render({ inputBytes: src, ext: srcExt,
      camera: "bw-35", cubeText: identity, filmProcess: "bw",
      filmStem: "ilford_delta_3200", seed: 5 });
  if (!out || out.length < 500)
      throw new Error("custom cube render failed");
}
{
  // 1.21: the extended push range, its limit, the format crop and the
  // format labels all reach the engine
  const { bytes: src, ext: srcExt } = await testInput();
  const out = await engine.render({ inputBytes: src, ext: srcExt, camera: "super8", seed: 7 });
  const pushed2 = await engine.render({ inputBytes: src, ext: srcExt, camera: "super8", seed: 7, develop: "push+2" });
  const pushed4 = await engine.render({ inputBytes: src, ext: srcExt, camera: "super8", seed: 7, develop: "push+4" });
  if (Buffer.compare(Buffer.from(pushed4), Buffer.from(pushed2)) === 0)
      throw new Error("push+4 identical to push+2");
  let overPushed = false;
  try { await engine.render({ inputBytes: src, ext: srcExt, camera: "super8", seed: 7, develop: "push+5" }); }
  catch (e) { overPushed = true; }
  if (!overPushed) throw new Error("push+5 accepted");
  const fullFrame = await engine.render({ inputBytes: src, ext: srcExt, camera: "super8", seed: 7, crop: "full" });
  if (Buffer.compare(Buffer.from(fullFrame), Buffer.from(out)) === 0)
      throw new Error("crop=full had no effect on the super8 gate");
  const head = new TextDecoder("latin1").decode(fullFrame.subarray(0, 4096));
  if (!/crop=full/.test(head)) throw new Error("crop=full missing from the embedded recipe");
  if (!/# geometry in=\d+,\d+ crop=\d+,\d+,\d+,\d+ place=\d+,\d+ out=\d+,\d+/.test(head))
      throw new Error("geometry line missing from the embedded comment");
  if (!cameras.find(c => c.name === "super8").format) throw new Error("super8 reports no format label");
  if (cameras.find(c => c.name === "35mm-negative").format) throw new Error("35mm-negative reports a format crop");
  console.log("push+4 differs from +2, push+5 refused, crop=full, format labels ok");
  const warm = await engine.render({ inputBytes: src, ext: srcExt, camera: "super8", seed: 7, scene: { temp: 0.8, grad: 1.5 } });
  if (Buffer.compare(Buffer.from(warm), Buffer.from(out)) === 0) throw new Error("scene adjustments had no effect");
  if (!/\ntemp=0.8\n/.test(new TextDecoder("latin1").decode(warm.subarray(0, 4096)))) throw new Error("temp missing from the embedded recipe");
  const zero = await engine.render({ inputBytes: src, ext: srcExt, camera: "super8", seed: 7, scene: { temp: 0, grad: 0 } });
  if (Buffer.compare(Buffer.from(zero), Buffer.from(out)) !== 0) throw new Error("zero scene adjustments changed the render");
  let over = false;
  try { await engine.render({ inputBytes: src, ext: srcExt, camera: "super8", seed: 7, scene: { grad: 4 } }); } catch (e) { over = true; }
  if (!over) throw new Error("grad=4 accepted by the wasm");
  console.log("scene adjustments through the wasm: effect, recipe, identity at zero, range");
  const trimmed = await engine.render({ inputBytes: src, ext: srcExt, camera: "super8", seed: 7, scene: { rotate: 2, trimx: 0.1, trimy: 0.1, trimw: 0.8, trimh: 0.8 } });
  const head2 = new TextDecoder("latin1").decode(trimmed.subarray(0, 4096));
  if (!/\nrotate=2\n/.test(head2) || !/ trim=\d+,\d+ rot=2\n/.test(head2)) throw new Error("lens keys or geometry missing from the embedded comment");
  console.log("lens and framing through the wasm: recipe and geometry");
}
{
  // filter reaches the engine: red differs from none on the same seed
  const { bytes: src, ext: srcExt } = await testInput();
  const plain = await engine.render({ inputBytes: src, ext: srcExt,
      camera: "bw-35", filmProcess: "bw", seed: 6 });
  const red = await engine.render({ inputBytes: src, ext: srcExt,
      camera: "bw-35", filmProcess: "bw", seed: 6, filter: "red" });
  if (Buffer.compare(Buffer.from(plain), Buffer.from(red)) === 0)
      throw new Error("filter had no effect through wasm");
}
{
  // grain model and edge effects reach the engine; bad values are refused
  const { bytes: src, ext: srcExt } = await testInput();
  const base = { inputBytes: src, ext: srcExt, camera: "bw-35",
                 filmProcess: "bw", seed: 6, maxDim: 300 };
  const plain = await engine.render(base);
  const silver = await engine.render({ ...base, grainModel: "silver" });
  if (Buffer.compare(Buffer.from(plain), Buffer.from(silver)) === 0)
      throw new Error("silver grain had no effect through wasm");
  const edged = await engine.render({ ...base, edge: 1.5 });
  if (Buffer.compare(Buffer.from(plain), Buffer.from(edged)) === 0)
      throw new Error("edge effects had no effect through wasm");
  let refused = false;
  try { await engine.render({ ...base, grainModel: "bromide" }); }
  catch (e) { refused = /unknown grain model/.test(e.message); }
  if (!refused) throw new Error("unknown grain model accepted by wasm");
  console.log("grain model and edge effects ok through wasm");
}
{
  // the raw door: a synthetic NEF and DNG decode in the engine, the
  // straight print comes back as a JPEG, and ev/halation reach render
  const nef = await readFile(new URL("../tests/raw/lossless14.nef", import.meta.url));
  const dng = await readFile(new URL("../tests/raw/bayer_lj92.dng", import.meta.url));
  const straight = await engine.develop({ inputBytes: nef, ext: "nef", maxDim: 0 });
  if (!(straight.length > 100 && straight[0] === 0xFF && straight[1] === 0xD8))
      throw new Error("develop did not return a JPEG");
  const base = { inputBytes: dng, ext: "dng", camera: "35mm-negative",
                 filmProcess: "negative", seed: 6, maxDim: 0 };
  const plain = await engine.render(base);
  const brighter = await engine.render({ ...base, ev: 1 });
  if (Buffer.compare(Buffer.from(plain), Buffer.from(brighter)) === 0)
      throw new Error("ev had no effect through wasm");
  const noglow = await engine.render({ ...base, halation: 0 });
  if (Buffer.compare(Buffer.from(plain), Buffer.from(noglow)) === 0)
      throw new Error("halation 0 had no effect through wasm");
  let refused = false;
  try { await engine.render({ ...base, ev: 9 }); }
  catch (e) { refused = /ev must be/.test(e.message); }
  if (!refused) throw new Error("ev 9 accepted by wasm");
  console.log("raw door ok through wasm (NEF straight print, DNG render, ev, halation)");
}
console.log("wasm engine tests passed");
