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
}
console.log("wasm engine tests passed");
