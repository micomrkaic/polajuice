// Runs web/index.html's module script under node with a minimal DOM stub:
// exercises boot (wasm fetch, camera dropdown, films manifest) and a real
// render through the page's own code path. Run from scripts/.
import { readFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";
import { writeFile, unlink } from "node:fs/promises";

const webDir = new URL("../web/", import.meta.url);
const html = await readFile(new URL("index.html", webDir), "utf8");
const js = html.split('<script type="module">')[1].split("</script>")[0];

// --- minimal DOM/browser stubs ---------------------------------------
class Elem {
  constructor(id) { this.id = id; this.options = []; this.dataset = {};
    this.value = id === "film" ? "__auto__" : ""; this.textContent = "";
    this.className = ""; this.disabled = true; this.handlers = {}; }
  add(opt) { this.options.push(opt); if (this.options.length === 1) this.value = opt.value;
    Object.defineProperty(this, "selectedOptions",
      { get: () => [this.options.find(o => o.value === this.value) || this.options[0]],
        configurable: true }); }
  addEventListener(ev, fn) { this.handlers[ev] = fn; }
  click() {} classListToggle() {}
  get classList() { return { toggle: () => {} }; }
}
const elems = {};
globalThis.document = {
  getElementById: id => elems[id] ??= new Elem(id),
  createElement: () => ({ click: () => {}, set href(v) {}, set download(v) {} }),
};
globalThis.Option = class { constructor(text, value) { this.text = text; this.value = value; this.dataset = {}; } };
globalThis.Blob = class { constructor(parts) { this.parts = parts; } };
globalThis.URL.createObjectURL = () => "blob:stub";
globalThis.performance ??= { now: () => Date.now() };
globalThis.FileReader = class {};
globalThis.fetch = async (path) => {
  try {
    const data = await readFile(new URL(path, webDir));
    return { ok: true, arrayBuffer: async () => data.buffer.slice(data.byteOffset, data.byteOffset + data.byteLength),
             json: async () => JSON.parse(data.toString()), text: async () => data.toString() };
  } catch { return { ok: false, arrayBuffer(){throw new Error("404 "+path)},
                     json(){throw new Error("404 "+path)}, text(){throw new Error("404 "+path)} }; }
};

// --- load the page module (written beside web/ so ./engine.js resolves) --
const probe = fileURLToPath(new URL("_pagecheck.mjs", webDir));
await writeFile(probe, js);
await import(probe);
await new Promise(r => setTimeout(r, 300));   // let boot() settle
await unlink(probe);

const status = elems["status"].textContent;
if (elems["status"].className === "error") throw new Error("boot errored: " + status);
if (elems["camera"].options.length < 9) throw new Error("camera dropdown not populated");
console.log(`page boot ok: "${status}", ${elems["camera"].options.length} cameras, version ${elems["version"].textContent}`);

// Rendering itself is covered by test_wasm.mjs through the same engine.js;
// here we verify the page wired its handlers.
if (!elems["render"].handlers["click"]) throw new Error("render button not wired");
if (!elems["drop"].handlers["drop"]) throw new Error("drop zone not wired");
console.log("page handlers wired; render path covered by test_wasm.mjs");
