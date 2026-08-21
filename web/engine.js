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
        /* inputBytes: Uint8Array; ext: "jpg"|"png"; cubeText: string|null;
         * maxDim 0 = full resolution. Returns Uint8Array of JPEG bytes. */
        async render({ inputBytes, ext, camera, cubeText, strength = 1.0,
                       seed = 42, age = 0, develop = "normal", maxDim = 0 }) {
            const inName = "in." + (ext === "png" ? "png" : "jpg");
            const workFiles = { [inName]: inputBytes };
            let filmArg = "-";
            if (cubeText != null) {
                workFiles["film.cube"] = new TextEncoder().encode(cubeText);
                filmArg = "work/film.cube";
            }
            const r = await run(
                ["render", "work/" + inName, camera, filmArg,
                 String(strength), String(seed >>> 0), String(age),
                 develop, String(maxDim), "work/out.jpg"], workFiles);
            if (r.code !== 0)
                throw new Error(r.stderr.trim() || `render failed (${r.code})`);
            const out = r.files.get("out.jpg");
            if (!out) throw new Error("render produced no output");
            return out.data;
        },
    };
}
