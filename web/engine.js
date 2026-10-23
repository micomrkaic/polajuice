/*
 * Polajuice browser engine: runs the WASI-compiled core (web/polajuice.wasm)
 * through the vendored browser_wasi_shim. DOM-free on purpose, so the same
 * module is exercised under node in scripts/test_wasm.mjs before every
 * release - the wasm you run in a browser is the wasm that passed that test.
 *
 * The wasm is a WASI *command*: each operation instantiates it fresh with
 * argv and an in-memory directory, runs main(), and reads results back.
 * Instantiation from a compiled module is sub-millisecond; renders dominate.
 */
import { WASI, WASIProcExit, File, PreopenDirectory, ConsoleStdout }
    from "./vendor/browser_wasi_shim/index.js";

export async function createEngine(wasmBytes) {
    const module = await WebAssembly.compile(wasmBytes);

    /* file extension the engine dispatches on; raw formats keep theirs */
function inputExtension(ext) {
    const e = String(ext || "jpg").toLowerCase();
    return e === "png" || e === "nef" || e === "dng" ? e : "jpg";
}

async function run(args, workFiles) {
        const contents = new Map();
        for (const [name, bytes] of Object.entries(workFiles))
            contents.set(name, new File(bytes));
        const work = new PreopenDirectory("work", contents);
        let stdout = "", stderr = "";
        const fds = [
            new File(new Uint8Array()).path_open(0, 0n, 0).fd_obj, // stdin
            ConsoleStdout.lineBuffered(line => { stdout += line + "\n"; }),
            ConsoleStdout.lineBuffered(line => { stderr += line + "\n"; }),
            work,
        ];
        const wasi = new WASI(["polajuice.wasm", ...args], [], fds, { debug: false });
        const instance = await WebAssembly.instantiate(module,
            { wasi_snapshot_preview1: wasi.wasiImport });
        let code = 0;
        try {
            code = wasi.start(instance);
        } catch (e) {
            if (e instanceof WASIProcExit) code = e.code;
            else throw e;
        }
        return { code, stdout, stderr, files: work.dir.contents };
    }

    return {
        async version() {
            const r = await run(["--version"], {});
            if (r.code !== 0) throw new Error(r.stderr || "version failed");
            return r.stdout.trim();
        },
        async cameras() {
            const r = await run(["--list-cameras"], {});
            if (r.code !== 0) throw new Error(r.stderr || "camera listing failed");
            return r.stdout.trim().split("\n").map(line => {
                const [name, film, description, traits, kind, procs]
                    = line.split("\t");
                return { name, film: film || null, description,
                         traits: traits || "", instant: kind === "instant",
                         processes: procs ? procs.split(",") : [] };
            });
        },
        /* inputBytes: Uint8Array; ext: "jpg"|"png"|"nef"|"dng"; cubeText: string|null;
         * maxDim 0 = full resolution. Returns Uint8Array of JPEG bytes. */
        /* The straight print of a raw file (scene-linear decode, a stop
         * up, shoulder), as JPEG bytes: the page's "original" view for a
         * .nef or .dng the browser cannot decode itself. */
        async develop({ inputBytes, ext, maxDim = 0 }) {
            const inName = "in." + inputExtension(ext);
            const r = await run(["develop", "work/" + inName, String(maxDim),
                                 "work/out.jpg"], { [inName]: inputBytes });
            if (r.code !== 0)
                throw new Error(r.stderr.trim() || `develop failed (${r.code})`);
            const out = r.files.get("out.jpg");
            if (!out) throw new Error("develop produced no output");
            return out.data;
        },
        async render({ inputBytes, ext, camera, cubeText, filmProcess = null,
                       filmStem = null, printCubeText = null, filter = null,
                       strength = 1.0, seed = 42, age = 0, develop = "normal",
                       maxDim = 0, grainModel = "classic", edge = 0,
                       ev = 0, halation = 1, printStem = null }) {
            const inName = "in." + inputExtension(ext);
            const workFiles = { [inName]: inputBytes };
            let filmArg = "-";
            if (cubeText != null) {
                workFiles["film.cube"] = new TextEncoder().encode(cubeText);
                filmArg = "work/film.cube";
            }
            let printArg = "-";
            if (printCubeText != null) {
                workFiles["print.cube"] = new TextEncoder().encode(printCubeText);
                printArg = "work/print.cube";
            }
            const r = await run(
                ["render", "work/" + inName, camera, filmArg,
                 filmProcess || "-", filmStem || "-", printArg,
                 filter || "-",
                 String(strength), String(seed >>> 0), String(age),
                 develop, String(maxDim), "work/out.jpg",
                 grainModel || "classic", String(edge),
                 String(ev), String(halation), printStem || "-"], workFiles);
            if (r.code !== 0)
                throw new Error(r.stderr.trim() || `render failed (${r.code})`);
            const out = r.files.get("out.jpg");
            if (!out) throw new Error("render produced no output");
            return out.data;
        },
    };
}

/*
 * The native engine: polajuice serve on the same origin. Same interface
 * as the wasm engine (version, cameras, render, develop) plus what only
 * a local process can do: the folder's files, exports next to the
 * source, batch runs, and looks kept as files. A render names a file in
 * the served folder, or carries the bytes of a dropped one.
 */
export async function probeNativeEngine(base = "") {
    try {
        const r = await fetch(base + "/api/version", { cache: "no-store" });
        if (!r.ok) return null;
        const info = await r.json();
        if (!info || !info.native) return null;
        return createNativeEngine(base, info);
    } catch (e) { return null; }
}

function filmParam(cubeText, filmStem, filmChoice) {
    if (filmChoice === "none") return "none";
    if (filmChoice === "auto") return "auto";
    return filmStem || "auto";
}

export function createNativeEngine(base, info) {
    const api = (path, params) => {
        const q = Object.entries(params || {})
            .filter(([, v]) => v !== null && v !== undefined)
            .map(([k, v]) => encodeURIComponent(k) + "=" + encodeURIComponent(String(v)))
            .join("&");
        return base + path + (q ? "?" + q : "");
    };
    async function expectOk(r) {
        if (r.ok) return r;
        let msg = r.statusText;
        try { const j = await r.json(); if (j && j.error) msg = j.error; } catch (e) { /* not json */ }
        throw new Error(msg);
    }
    function lookParams(o) {
        return { camera: o.camera, film: filmParam(o.cubeText, o.filmStem, o.filmChoice),
                 print: o.printStem || "none", filter: o.filter || "none",
                 strength: o.strength ?? 1, develop: o.develop || "normal", age: o.age ?? 0,
                 grain: o.grainModel || "classic", edge: o.edge ?? 0, ev: o.ev ?? 0,
                 halation: o.halation ?? 1, seed: (o.seed ?? 42) >>> 0 };
    }
    return {
        native: true,
        info,
        async version() { return info.version; },
        async cameras() { return (await expectOk(await fetch(api("/api/cameras")))).json(); },
        async render(o) {
            const params = { ...lookParams(o), max: o.maxDim || 0 };
            let r;
            if (o.name) r = await fetch(api("/api/render", { ...params, name: o.name }), { method: "POST" });
            else r = await fetch(api("/api/render", { ...params, ext: o.ext || "jpg" }),
                                 { method: "POST", body: o.inputBytes });
            return new Uint8Array(await (await expectOk(r)).arrayBuffer());
        },
        async develop(o) {
            const params = { max: o.maxDim || 0 };
            let r;
            if (o.name) r = await fetch(api("/api/develop", { ...params, name: o.name }));
            else r = await fetch(api("/api/develop", { ...params, ext: o.ext || "jpg" }),
                                 { method: "POST", body: o.inputBytes });
            return new Uint8Array(await (await expectOk(r)).arrayBuffer());
        },
        async files() { return (await expectOk(await fetch(api("/api/files"), { cache: "no-store" }))).json(); },
        fileUrl(name) { return api("/api/file", { name }); },
        developUrl(name, maxDim) { return api("/api/develop", { name, max: maxDim || 0 }); },
        async exportFile(o) {
            const r = await fetch(api("/api/export", { ...lookParams(o), name: o.name }), { method: "POST" });
            return (await expectOk(r)).json();
        },
        async batch(o) {
            const r = await fetch(api("/api/batch", lookParams(o)), { method: "POST" });
            return (await expectOk(r)).json();
        },
        async looks() { return (await expectOk(await fetch(api("/api/looks"), { cache: "no-store" }))).json(); },
        async saveLook(name, text) {
            return (await expectOk(await fetch(api("/api/looks", { name }), { method: "PUT", body: text }))).json();
        },
        async deleteLook(name) {
            return (await expectOk(await fetch(api("/api/looks", { name }), { method: "DELETE" }))).json();
        },
    };
}
