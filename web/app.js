// Page logic: connects the 3D cube, the buttons and the solver API.
import * as Cube from "./cube-model.js";
import { CubeRenderer, STICKER_COLORS } from "./renderer.js";

const $ = (id) => document.getElementById(id);
const reduceMotion = window.matchMedia("(prefers-reduced-motion: reduce)").matches;
const renderer = new CubeRenderer($("cube"));

const state = {
  stickers: Cube.fromFacelets(Cube.SOLVED),
  busy: false, // a move or a sequence is animating
  playing: false,
  stopRequested: false,
  solution: null, // { moves, index, start }  start = facelets before move 0
  history: [], // your own turns, for undo
  future: [], // undone turns, for redo
  practice: { armed: false, running: false, start: 0, moves: 0 },
};

// ----------------------------------------------------------------- helpers
function setStickers(stickers) {
  state.stickers = stickers;
  renderer.setStickers(stickers);
}

function facelets() {
  return Cube.toFacelets(state.stickers);
}

function moveMs() {
  if (reduceMotion) return 0;
  return [620, 400, 250, 150, 85][Number($("speed").value) - 1];
}

function setStatus(text, isError = false) {
  const el = $("status");
  el.textContent = text;
  el.classList.toggle("error", isError);
}

async function api(path, options) {
  let res;
  try {
    res = await fetch(path, options);
  } catch {
    throw new Error(
      "Can't reach the solver server. If you run it yourself, start ./build/cubesolver_server and open http://localhost:8080.",
    );
  }
  const body = await res.json().catch(() => ({}));
  if (!res.ok) throw new Error(body.error ? `${capitalize(body.error)}.` : `Server error ${res.status}.`);
  return body;
}

const capitalize = (s) => s.charAt(0).toUpperCase() + s.slice(1);

const compact = new Intl.NumberFormat("en", { notation: "compact", maximumFractionDigits: 1 });

// Turns one face with animation, then commits the new stickers.
async function turn(move, ms = moveMs()) {
  await renderer.animateMove(move, ms);
  setStickers(Cube.applyMove(state.stickers, move));
}

// Every action that animates or talks to the server runs through here, so
// only one runs at a time. A second click while busy is ignored, which
// prevents stale solutions and overlapping animations.
async function exclusive(action) {
  if (state.busy) return;
  state.busy = true;
  updateControls();
  try {
    await action();
  } finally {
    state.busy = false;
    state.playing = false;
    state.stopRequested = false;
    updateControls();
  }
}

// Plays moves one after another (call inside exclusive()).
async function runSequence(moves, ms, afterEach) {
  for (const m of moves) {
    if (state.stopRequested) break;
    await turn(m, ms);
    afterEach?.(m);
  }
}

// ----------------------------------------------------------------- your own turns: undo, redo
function clearHistory() {
  state.history = [];
  state.future = [];
}

// A turn you make (swipe, key or button). It can be undone, and it counts
// toward the practice timer.
function userTurn(move) {
  return exclusive(async () => {
    clearSolution();
    await turn(move);
    state.history.push(move);
    state.future = [];
    practiceAfterTurn();
  });
}

function undo() {
  return exclusive(async () => {
    const move = state.history.pop();
    if (!move) return;
    await turn(Cube.inverse(move));
    state.future.push(move);
    practiceAfterTurn();
  });
}

function redo() {
  return exclusive(async () => {
    const move = state.future.pop();
    if (!move) return;
    await turn(move);
    state.history.push(move);
    practiceAfterTurn();
  });
}

// ----------------------------------------------------------------- practice timer
const BEST_KEY = "cubesolver.bestMs";

function loadBest() {
  try {
    const v = Number(localStorage.getItem(BEST_KEY));
    return v > 0 ? v : null;
  } catch {
    return null;
  }
}

function formatTime(ms) {
  const total = Math.floor(ms / 10); // hundredths
  const min = Math.floor(total / 6000);
  const sec = Math.floor((total % 6000) / 100);
  return `${min}:${String(sec).padStart(2, "0")}.${String(total % 100).padStart(2, "0")}`;
}

function renderPractice(note) {
  const p = state.practice;
  $("move-count").textContent = String(p.moves);
  const best = loadBest();
  $("best").textContent = best ? formatTime(best) : "-";
  $("timer").parentElement.parentElement.classList.toggle("running", p.running);
  if (note !== undefined) $("practice-note").textContent = note;
}

function tickTimer() {
  const p = state.practice;
  if (!p.running) return;
  $("timer").textContent = formatTime(performance.now() - p.start);
  requestAnimationFrame(tickTimer);
}

// Called after a scramble: the timer waits for your first turn.
function armPractice() {
  state.practice = { armed: true, running: false, start: 0, moves: 0 };
  $("timer").textContent = formatTime(0);
  document.querySelector(".practice").classList.remove("won");
  renderPractice("Ready. The timer starts on your first turn.");
}

function stopPractice(note) {
  const p = state.practice;
  if (!p.armed && !p.running) return;
  p.armed = false;
  p.running = false;
  renderPractice(note);
}

function practiceAfterTurn() {
  const p = state.practice;
  if (!p.armed && !p.running) return;
  if (!p.running) {
    p.running = true;
    p.armed = false;
    p.start = performance.now();
    requestAnimationFrame(tickTimer);
  }
  p.moves++;
  if (facelets() === Cube.SOLVED) {
    const ms = performance.now() - p.start;
    p.running = false;
    $("timer").textContent = formatTime(ms);
    const best = loadBest();
    const isBest = !best || ms < best;
    if (isBest) {
      try {
        localStorage.setItem(BEST_KEY, String(Math.round(ms)));
      } catch {
        // private mode: no best time, that's fine
      }
    }
    document.querySelector(".practice").classList.add("won");
    renderPractice(
      `You solved it in ${formatTime(ms)} with ${p.moves} moves${isBest ? ". That's your best time!" : "."}`,
    );
    setStatus("Solved by you. Nice work! Press Scramble to go again.");
    celebrate();
  } else {
    renderPractice();
  }
}

// ----------------------------------------------------------------- confetti
function celebrate() {
  if (reduceMotion) return;
  const canvas = $("confetti");
  const dpr = window.devicePixelRatio || 1;
  const w = (canvas.width = Math.round(canvas.clientWidth * dpr));
  const h = (canvas.height = Math.round(canvas.clientHeight * dpr));
  const ctx = canvas.getContext("2d");
  const colors = Object.values(STICKER_COLORS);
  const bits = Array.from({ length: 140 }, () => ({
    x: w / 2 + (Math.random() - 0.5) * w * 0.2,
    y: h * 0.45,
    vx: (Math.random() - 0.5) * 16 * dpr,
    vy: (-6 - Math.random() * 12) * dpr,
    size: (5 + Math.random() * 6) * dpr,
    spin: Math.random() * Math.PI,
    color: colors[Math.floor(Math.random() * colors.length)],
  }));
  const startTime = performance.now();
  const frame = (now) => {
    const t = now - startTime;
    ctx.clearRect(0, 0, w, h);
    if (t > 2200) return;
    ctx.globalAlpha = Math.min(1, (2200 - t) / 600);
    for (const b of bits) {
      b.vy += 0.45 * dpr;
      b.vx *= 0.985;
      b.x += b.vx;
      b.y += b.vy;
      b.spin += 0.15;
      ctx.save();
      ctx.translate(b.x, b.y);
      ctx.rotate(b.spin);
      ctx.fillStyle = b.color;
      ctx.fillRect(-b.size / 2, -b.size / 3, b.size, (b.size * 2) / 3);
      ctx.restore();
    }
    requestAnimationFrame(frame);
  };
  requestAnimationFrame(frame);
}

// ----------------------------------------------------------------- site counters
const VISITED_KEY = "cubesolver.visited";

function animateCount(el, to) {
  const from = Number(el.dataset.value || 0);
  el.dataset.value = String(to);
  const fmt = new Intl.NumberFormat("en");
  if (reduceMotion || from === to) {
    el.textContent = fmt.format(to);
    return;
  }
  const start = performance.now();
  const step = (now) => {
    const t = Math.min(1, (now - start) / 900);
    el.textContent = fmt.format(Math.round(from + (to - from) * (1 - (1 - t) ** 3)));
    if (t < 1) requestAnimationFrame(step);
  };
  requestAnimationFrame(step);
}

function showCounters(c) {
  animateCount($("count-visitors"), c.visitors);
  animateCount($("count-solved"), c.cubesSolved);
}

// Counts this browser once, then just reads the numbers on later visits.
async function loadCounters() {
  let firstVisit = true;
  try {
    firstVisit = !localStorage.getItem(VISITED_KEY);
  } catch {
    // no storage: count this visit
  }
  try {
    const c = firstVisit ? await api("/api/visit", { method: "POST" }) : await api("/api/counters");
    if (firstVisit) {
      try {
        localStorage.setItem(VISITED_KEY, "1");
      } catch {
        // ignore
      }
    }
    showCounters(c);
  } catch {
    // the health check already tells the person if the server is down
  }
}

// ----------------------------------------------------------------- solution tape
function clearSolution() {
  state.solution = null;
  $("stats").hidden = true;
  renderTape();
}

function renderTape() {
  const tape = $("tape");
  tape.replaceChildren();
  const sol = state.solution;
  $("tape-empty").hidden = !!sol;
  if (sol) {
    if (sol.moves.length === 0) {
      $("tape-empty").hidden = false;
      $("tape-empty").textContent = "Nothing to do: the cube is already solved.";
    }
    sol.moves.forEach((m, i) => {
      const li = document.createElement("li");
      const b = document.createElement("button");
      b.type = "button";
      b.className = "token" + (i < sol.index ? " done" : i === sol.index ? " current" : "");
      b.innerHTML = `${Cube.moveName(m)}<small>${i + 1}</small>`;
      b.setAttribute("aria-label", `Move ${i + 1}: ${Cube.moveName(m)}. Jump here.`);
      b.addEventListener("click", () => jumpTo(i));
      li.append(b);
      tape.append(li);
    });
  } else {
    $("tape-empty").textContent = "Press Solve to see the solution here. Then play it move by move.";
  }
  updateControls();
}

function updateControls() {
  const sol = state.solution;
  const busy = state.busy;
  for (const id of ["solve", "scramble", "reset", "edit"]) $(id).disabled = busy;
  $("undo").disabled = busy || state.history.length === 0;
  $("redo").disabled = busy || state.future.length === 0;
  $("scramble-form").querySelector("button").disabled = busy;
  document.querySelectorAll("#movepad button").forEach((b) => (b.disabled = busy));
  document.querySelectorAll(".token").forEach((b) => (b.disabled = busy));

  const has = !!sol && sol.moves.length > 0;
  $("play").disabled = !has || (busy && !state.playing) || (!state.playing && sol.index >= sol.moves.length);
  $("play").textContent = state.playing ? "Pause" : "Play";
  $("first").disabled = !has || busy || sol.index === 0;
  $("prev").disabled = !has || busy || sol.index === 0;
  $("next").disabled = !has || busy || sol.index >= sol.moves.length;
  $("last").disabled = !has || busy || sol.index >= sol.moves.length;
}

function play() {
  const sol = state.solution;
  if (!sol) return;
  return exclusive(async () => {
    state.playing = true;
    updateControls();
    await runSequence(sol.moves.slice(sol.index), moveMs(), () => {
      sol.index++;
      renderTape();
    });
    if (sol.index >= sol.moves.length) {
      setStatus("Solved. Press Start to watch it again, or click any move on the tape.");
      if (facelets() === Cube.SOLVED) celebrate();
    }
  });
}

function pause() {
  if (state.playing) state.stopRequested = true;
}

function step(direction) {
  const sol = state.solution;
  if (!sol) return;
  return exclusive(async () => {
    if (direction > 0 && sol.index < sol.moves.length) {
      await turn(sol.moves[sol.index]);
      sol.index++;
    } else if (direction < 0 && sol.index > 0) {
      await turn(Cube.inverse(sol.moves[sol.index - 1]));
      sol.index--;
    }
    renderTape();
  });
}

// Shows the cube as it is after `index` solution moves (no animation).
function jumpTo(index) {
  const sol = state.solution;
  if (!sol || state.busy) return;
  let stickers = Cube.fromFacelets(sol.start);
  for (let i = 0; i < index; i++) stickers = Cube.applyMove(stickers, sol.moves[i]);
  setStickers(stickers);
  sol.index = index;
  renderTape();
}

// ----------------------------------------------------------------- actions
function solve() {
  return exclusive(async () => {
    const start = facelets();
    if (start === Cube.SOLVED) {
      setStatus("The cube is already solved. Scramble it first.");
      return;
    }
    stopPractice("Timer stopped: you asked the solver for help.");
    setStatus("Searching...");
    try {
      const params = new URLSearchParams({ facelets: start, target: $("target").value, timeout: $("timeout").value });
      const r = await api(`/api/solve?${params}`);
      if (facelets() !== start) return; // the cube changed meanwhile; this answer is stale
      showSolution(r, start);
    } catch (e) {
      setStatus(e.message, true);
    }
  });
}

function showSolution(r, start) {
  const moves = Cube.parseMoves(r.solution);
  state.solution = { moves, index: 0, start };
  clearHistory();
  $("stat-moves").textContent = String(r.length);
  $("stat-time").textContent = r.cached ? "cached" : `${r.timeMs < 10 ? r.timeMs.toFixed(1) : Math.round(r.timeMs)} ms`;
  $("stat-nodes").textContent = r.cached ? "-" : compact.format(r.nodes);
  $("stat-searches").textContent = String(r.searches);
  $("stats").hidden = false;
  setStatus(
    r.targetReached
      ? `Found a ${r.length}-move solution. Press Play to watch it.`
      : `Found a ${r.length}-move solution when time ran out. Press Play to watch it.`,
  );
  renderTape();
  if (moves.length > 0) api("/api/counters").then(showCounters).catch(() => {});
}

function scramble() {
  return exclusive(async () => {
    setStatus("Making a random scramble...");
    try {
      const r = await api("/api/scramble");
      clearSolution();
      setStickers(Cube.fromFacelets(Cube.SOLVED));
      $("scramble-input").value = r.scramble;
      await runSequence(Cube.parseMoves(r.scramble), Math.min(moveMs(), 70));
      if (facelets() !== r.facelets) setStickers(Cube.fromFacelets(r.facelets)); // safety net
      clearHistory();
      armPractice();
      setStatus(`Scrambled with ${r.length} moves. Solve it yourself, or press Solve.`);
    } catch (e) {
      setStatus(e.message, true);
    }
  });
}

function applyScramble(text) {
  let moves;
  try {
    moves = Cube.parseMoves(text);
  } catch (e) {
    if (!state.busy) setStatus(e.message, true);
    return;
  }
  return exclusive(async () => {
    clearSolution();
    setStickers(Cube.fromFacelets(Cube.SOLVED));
    await runSequence(moves, Math.min(moveMs(), 90));
    clearHistory();
    if (facelets() !== Cube.SOLVED) armPractice();
    else stopPractice();
    setStatus(moves.length ? `Applied ${moves.length} moves. Press Solve.` : "Type some moves first, like R U R' U'.");
  });
}

function reset() {
  if (state.busy) return;
  clearSolution();
  clearHistory();
  stopPractice();
  document.querySelector(".practice").classList.remove("won");
  $("timer").textContent = formatTime(0);
  $("practice-note").textContent = "Scramble the cube, then solve it yourself. The timer starts on your first turn.";
  setStickers(Cube.fromFacelets(Cube.SOLVED));
  updateControls();
  setStatus("Reset to a solved cube.");
}

// ----------------------------------------------------------------- move pad
function buildMovePad() {
  const pad = $("movepad");
  for (const face of Cube.FACES) {
    const row = document.createElement("div");
    row.className = "row";
    const chip = document.createElement("span");
    chip.className = "chip";
    chip.style.background = STICKER_COLORS[face];
    chip.setAttribute("aria-hidden", "true");
    row.append(chip);
    for (const turns of [1, 3, 2]) {
      const move = { face, turns };
      const b = document.createElement("button");
      b.type = "button";
      b.textContent = Cube.moveName(move);
      b.addEventListener("click", () => userTurn(move));
      row.append(b);
    }
    pad.append(row);
  }
}

// ----------------------------------------------------------------- sticker editor
const editor = { colors: [], paint: "U" };
const FACE_NAMES = { U: "White", R: "Red", F: "Green", D: "Yellow", L: "Orange", B: "Blue" };
// Where each face sits in the 12 x 9 net.
const NET_ORIGIN = { U: [0, 3], L: [3, 0], F: [3, 3], R: [3, 6], B: [3, 9], D: [6, 3] };

function openEditor() {
  editor.colors = facelets().split("");
  $("editor-error").textContent = "";
  renderPalette();
  renderNet();
  $("editor").showModal();
}

function renderPalette() {
  const counts = Object.fromEntries([...Cube.FACES].map((f) => [f, 0]));
  editor.colors.forEach((c) => counts[c]++);
  const palette = $("palette");
  palette.replaceChildren();
  for (const f of Cube.FACES) {
    const b = document.createElement("button");
    b.type = "button";
    b.className = "swatch";
    b.setAttribute("role", "radio");
    b.setAttribute("aria-checked", String(editor.paint === f));
    b.innerHTML = `<span style="background:${STICKER_COLORS[f]}"></span>${FACE_NAMES[f]} <span class="count">${counts[f]}/9</span>`;
    b.addEventListener("click", () => {
      editor.paint = f;
      renderPalette();
    });
    palette.append(b);
  }
}

function renderNet() {
  const net = $("net");
  net.replaceChildren();
  const cells = new Array(9 * 12).fill(null);
  [...Cube.FACES].forEach((face, fi) => {
    const [r0, c0] = NET_ORIGIN[face];
    for (let i = 0; i < 9; i++) cells[(r0 + Math.floor(i / 3)) * 12 + c0 + (i % 3)] = fi * 9 + i;
  });
  for (const idx of cells) {
    if (idx === null) {
      net.append(document.createElement("span"));
      continue;
    }
    const b = document.createElement("button");
    b.type = "button";
    const isCenter = idx % 9 === 4;
    b.className = isCenter ? "center" : "";
    b.style.background = STICKER_COLORS[editor.colors[idx]];
    b.setAttribute("aria-label", `${FACE_NAMES[Cube.FACES[Math.floor(idx / 9)]]} face, sticker ${(idx % 9) + 1}: ${FACE_NAMES[editor.colors[idx]]}`);
    if (isCenter) b.disabled = true;
    b.addEventListener("click", () => {
      editor.colors[idx] = editor.paint;
      renderNet();
      renderPalette();
      net.querySelectorAll("button")[cells.filter((c) => c !== null).indexOf(idx)]?.focus();
    });
    net.append(b);
  }
}

function applyEditor() {
  const counts = {};
  editor.colors.forEach((c) => (counts[c] = (counts[c] || 0) + 1));
  const wrong = [...Cube.FACES].filter((f) => counts[f] !== 9);
  if (wrong.length) {
    $("editor-error").textContent = `Each color needs exactly 9 stickers. Check ${wrong.map((f) => FACE_NAMES[f].toLowerCase()).join(", ")}.`;
    return;
  }
  clearSolution();
  setStickers(Cube.fromFacelets(editor.colors.join("")));
  clearHistory();
  if (facelets() !== Cube.SOLVED) armPractice();
  updateControls();
  $("editor").close();
  setStatus("Stickers applied. Press Solve. If the cube is impossible, the solver will say why.");
}

// ----------------------------------------------------------------- wiring
$("solve").addEventListener("click", solve);
$("scramble").addEventListener("click", scramble);
$("reset").addEventListener("click", reset);
$("edit").addEventListener("click", openEditor);
$("editor-apply").addEventListener("click", applyEditor);
$("scramble-form").addEventListener("submit", (e) => {
  e.preventDefault();
  applyScramble($("scramble-input").value);
});
$("undo").addEventListener("click", undo);
$("redo").addEventListener("click", redo);
renderer.onSwipe = (move) => userTurn(move);
$("play").addEventListener("click", () => (state.playing ? pause() : play()));
$("prev").addEventListener("click", () => step(-1));
$("next").addEventListener("click", () => step(1));
$("first").addEventListener("click", () => jumpTo(0));
$("last").addEventListener("click", () => state.solution && jumpTo(state.solution.moves.length));

document.addEventListener("keydown", (e) => {
  const tag = document.activeElement?.tagName;
  if (tag === "INPUT" || tag === "SELECT" || tag === "TEXTAREA" || $("editor").open) return;
  const key = e.key.toLowerCase();
  if ((e.metaKey || e.ctrlKey) && !e.altKey && (key === "z" || key === "y")) {
    e.preventDefault();
    e.shiftKey || key === "y" ? redo() : undo();
    return;
  }
  if (e.metaKey || e.ctrlKey || e.altKey) return;
  const face = e.key.toUpperCase();
  if (Cube.FACES.includes(face) && face.length === 1) {
    e.preventDefault();
    userTurn({ face, turns: e.shiftKey ? 3 : 1 });
  } else if (e.key === " " && state.solution && tag !== "BUTTON") {
    e.preventDefault();
    state.playing ? pause() : play();
  } else if (e.key.startsWith("Arrow")) {
    e.preventDefault();
    const d = 0.12;
    renderer.nudgeView(e.key === "ArrowLeft" ? -d : e.key === "ArrowRight" ? d : 0, e.key === "ArrowUp" ? -d : e.key === "ArrowDown" ? d : 0);
  }
});

buildMovePad();
setStickers(state.stickers);
renderTape();
renderPractice();
loadCounters();

// Tell the person early if the server is not running.
api("/api/health").catch((e) => setStatus(e.message, true));
