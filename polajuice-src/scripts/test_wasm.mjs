// Verifies web/polajuice.wasm through web/engine.js and the vendored
// browser_wasi_shim - the exact stack the page uses - and byte-compares a
// render against the native CLI. Run: node scripts/test_wasm.mjs [native.jpg in.png]
import { readFile } from "node:fs/promises";
import { createEngine } from "../web/engine.js";

const engine = await createEngine(await readFile(new URL("../web/polajuice.wasm", import.meta.url)));
const version = await engine.version();
const cameras = await engine.cameras();
if (cameras.length < 13) throw new Error("camera enumeration short: " + cameras.length);
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
console.log("wasm engine tests passed");
