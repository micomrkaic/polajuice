// Real-DOM page test: runs web/index.html under jsdom (real selects,
// optgroups, styles - the semantics hand-rolled stubs get wrong). Skips
// cleanly when jsdom is not installed: npm install jsdom  enables it.
import { readFile } from "node:fs/promises";
let JSDOM;
try { ({ JSDOM } = await import("jsdom")); }
catch { console.log("test_page_dom: jsdom not installed, skipping"); process.exit(0); }
import { createEngine } from "../web/engine.js";

const webDir = new URL("../web/", import.meta.url);
const html = await readFile(new URL("index.html", webDir), "utf8");
let js = html.split('<script type="module">')[1].split("</script>")[0];
js = js.replace(/^\s*import\s+\{\s*createEngine\s*\}[^\n]*\n/m, "");

const dom = new JSDOM(html.replace(/<script type="module">[\s\S]*?<\/script>/, ""),
                      { runScripts: "outside-only", url: "http://localhost/" });
const { window } = dom;
window.createEngine = createEngine;
window.fetch = async (path) => {
  try {
    const data = await readFile(new URL(path, webDir));
    return { ok: true,
      arrayBuffer: async () => data.buffer.slice(data.byteOffset, data.byteOffset + data.byteLength),
      json: async () => JSON.parse(data.toString()),
      text: async () => data.toString() };
  } catch { return { ok: false, arrayBuffer(){throw 0}, json(){throw 0}, text(){throw 0} }; }
};
window.performance ??= { now: () => Date.now() };
window.URL.createObjectURL = () => "blob:x";
window.eval(js);
await new Promise(r => setTimeout(r, 800));

const doc = window.document;
const fail = (m) => { console.error("test_page_dom:", m); process.exit(1); };
if (/error/.test(doc.getElementById("status").className))
  fail("boot errored: " + doc.getElementById("status").textContent);
if (doc.getElementById("camera").options.length < 13)
  fail("camera dropdown short");
// print selector contract when print stocks are staged
const manifest = await window.fetch("films/films.json");
if (manifest.ok) {
  const families = (await manifest.json()).families ?? [];
  const printFams = families.filter(f => f.process === "print");
  const visible = doc.getElementById("printcontrol").style.display !== "none";
  if (printFams.length && !visible) fail("print stocks staged but selector hidden");
  if (!printFams.length && visible) fail("selector visible with no print stocks");
  const filmValues = [...doc.getElementById("film").options].map(o => o.value);
  if (filmValues.some(v => v.startsWith("synthetic_")))
    fail("print stock leaked into the film dropdown");
  console.log(`real-DOM: print selector ${printFams.length ? "visible" : "hidden"} correctly, no leakage`);
}
console.log("real-DOM page tests passed");
