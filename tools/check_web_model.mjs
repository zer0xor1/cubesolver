// Checks that the browser's sticker model (web/cube-model.js) turns the cube
// exactly like the C++ solver. Usage: node tools/check_web_model.mjs <path to cubesolver>
import { execFileSync } from "node:child_process";
import { fileURLToPath, pathToFileURL } from "node:url";
import path from "node:path";

const here = path.dirname(fileURLToPath(import.meta.url));
const model = await import(pathToFileURL(path.join(here, "..", "web", "cube-model.js")).href);
const cli = process.argv[2];
if (!cli) {
  console.error("usage: node tools/check_web_model.mjs <path to cubesolver executable>");
  process.exit(2);
}

let seed = 12345;
const rand = (n) => {
  seed = (seed * 1103515245 + 12345) % 2147483648;
  return seed % n;
};

let failures = 0;
const trials = 60;
for (let t = 0; t < trials; t++) {
  const length = 1 + rand(25);
  const parts = [];
  for (let i = 0; i < length; i++) parts.push("URFDLB"[rand(6)] + ["", "2", "'"][rand(3)]);
  const scramble = parts.join(" ");

  let stickers = model.fromFacelets(model.SOLVED);
  for (const m of model.parseMoves(scramble)) stickers = model.applyMove(stickers, m);
  const js = model.toFacelets(stickers);
  const cpp = execFileSync(cli, ["facelets", scramble], { encoding: "utf8" }).trim();
  if (js !== cpp) {
    failures++;
    console.error(`MISMATCH for "${scramble}"\n  js:  ${js}\n  c++: ${cpp}`);
  }
}
if (model.toFacelets(model.fromFacelets(model.SOLVED)) !== model.SOLVED) failures++;
console.log(failures === 0 ? `web model matches C++ on ${trials} scrambles` : `${failures} mismatches`);
process.exit(failures === 0 ? 0 : 1);
