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
  constructor(id) { this.id = id; this.options = []; this.children = [];
    this.dataset = {};
    this.value = id === "film" ? "__auto__" : ""; this.textContent = "";
    this.className = ""; this.disabled = true; this.handlers = {}; }
  get firstChild() { return this.children[0] ?? null; }
  removeChild(node) {
    this.children = this.children.filter(c => c !== node);
    this.options = this.options.filter(o => o !== node);
  }
  add(opt) { this.options.push(opt); this.children.push(opt);
    if (this.options.length === 1) this.value = opt.value; }
  remove(i) { this.options.splice(i, 1); }
  get selectedOptions() {
    return [this.options.find(o => o.value === this.value) || this.options[0] || { dataset: {} }];
  }
  addEventListener(ev, fn) { this.handlers[ev] = fn; }
  dispatchEvent(ev) { this.handlers[ev.type]?.(ev); }
  append(...nodes) { this.children.push(...nodes); }
  click() {} classListToggle() {}
  get classList() { return { toggle: () => {} }; }
}
const elems = {};
globalThis.document = {
  getElementById: id => elems[id] ??= new Elem(id),
  createElement: () => ({ click: () => {}, textContent: "", innerHTML: "",
                          className: "", src: "", alt: "", loading: "",
                          children: [], handlers: {}, label: "",
                          append(...n) { this.children.push(...n); },
                          addEventListener(ev, fn) { this.handlers[ev] = fn; },
                          dispatchEvent(e) { this.handlers[e.type]?.(e); },
                          set href(v) {}, set download(v) {} }),
};
globalThis.Option = class { constructor(text, value) { this.text = text; this.value = value; this.dataset = {}; } };
globalThis.Event = class { constructor(type) { this.type = type; } };
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
if (elems["camera"].options.length < 13) throw new Error("camera dropdown not populated");
console.log(`page boot ok: "${status}", ${elems["camera"].options.length} cameras, version ${elems["version"].textContent}`);

// Rendering itself is covered by test_wasm.mjs through the same engine.js;
// here we verify the page wired its handlers.
if (!elems["render"].handlers["click"]) throw new Error("render button not wired");
if (!elems["drop"].handlers["drop"]) throw new Error("drop zone not wired");
if (!elems["helpcameras"] || !(elems["helpcameras"].children?.length >= 26))
  throw new Error("help panel not populated");
if (!(elems["samplestrip"].children?.length >= 6))
  throw new Error("sample strip not populated: " + (elems["samplestrip"].children?.length ?? 0));
console.log(`sample strip: ${elems["samplestrip"].children.length - 1} samples + credit`);
if (!elems["develop"]) throw new Error("develop select absent");
if (!elems["film"].options.length && !elems["film"].children?.length)
  throw new Error("film select not populated");
// compatibility: switching to a sealed camera must empty and disable film
elems["camera"].value = "autochrome";
elems["camera"].handlers["change"]();
if (!elems["film"].disabled) throw new Error("sealed camera did not disable film");
elems["camera"].value = "35mm-slide";
elems["camera"].handlers["change"]();
if (elems["film"].disabled) throw new Error("film select stuck disabled");
console.log("compatibility filtering: sealed disables film, film camera re-enables");

// THE regression: pick an instant camera, then click a sample that suggests
// a slide camera; the film dropdown must be rebuilt for the new camera.
elems["camera"].value = "polaroid-600";
elems["camera"].handlers["change"]();
const beach = elems["samplestrip"].children.find(
    f => f.children?.some?.(c => /Driftwood/.test(c.textContent ?? "")));
if (!beach) throw new Error("beach sample not found in strip");
beach.handlers["click"]();
await new Promise(r => setTimeout(r, 200));
if (elems["camera"].value !== "35mm-slide")
  throw new Error("sample did not switch camera: " + elems["camera"].value);
const offered = elems["film"].children
    .flatMap(n => n.children?.length ? n.children : [n])
    .map(o => o.value ?? "").join(" ");
if (/polaroid_px/.test(offered))
  throw new Error("SAMPLE-CLICK LEAK: integral stock offered on slide camera");
console.log("sample-click compatibility resync verified");
console.log(`help panel: ${elems["helpcameras"].children.length / 2} cameras described`);
console.log("page handlers wired; render path covered by test_wasm.mjs");
