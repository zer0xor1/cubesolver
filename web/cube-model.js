// Sticker model of the cube for the browser.
//
// Every sticker has a cubie position p (x, y, z in -1..1), an outward normal n,
// and a color letter (U R F D L B). Axes: x points to R, y to U, z to F.
// A clockwise quarter turn of a face rotates every sticker in that layer by
// v -> v x a + (v . a) a, where a is the face normal. The C++ solver uses a
// different (cubie) model; tools/check_web_model.mjs checks that both agree.

export const FACES = "URFDLB";

const NORMALS = {
  U: [0, 1, 0],
  R: [1, 0, 0],
  F: [0, 0, 1],
  D: [0, -1, 0],
  L: [-1, 0, 0],
  B: [0, 0, -1],
};

// Cubie position of sticker (row r, column c) on each face, matching the
// standard facelet order U1..U9 R1..R9 F1..F9 D1..D9 L1..L9 B1..B9.
const SLOT = {
  U: (r, c) => [c - 1, 1, r - 1],
  R: (r, c) => [1, 1 - r, 1 - c],
  F: (r, c) => [c - 1, 1 - r, 1],
  D: (r, c) => [c - 1, -1, 1 - r],
  L: (r, c) => [-1, 1 - r, c - 1],
  B: (r, c) => [1 - c, 1 - r, -1],
};

const key = (p, n) => `${p[0]},${p[1]},${p[2]}|${n[0]},${n[1]},${n[2]}`;

// The 54 sticker slots in facelet order.
export const SLOTS = [];
for (const f of FACES) {
  for (let r = 0; r < 3; r++) {
    for (let c = 0; c < 3; c++) SLOTS.push({ face: f, p: SLOT[f](r, c), n: NORMALS[f] });
  }
}

export const SOLVED = "UUUUUUUUURRRRRRRRRFFFFFFFFFDDDDDDDDDLLLLLLLLLBBBBBBBBB";

export function fromFacelets(facelets) {
  return SLOTS.map((s, i) => ({ p: [...s.p], n: [...s.n], color: facelets[i] }));
}

export function toFacelets(stickers) {
  const byKey = new Map(stickers.map((s) => [key(s.p, s.n), s.color]));
  return SLOTS.map((s) => byKey.get(key(s.p, s.n))).join("");
}

const dot = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
const cross = (a, b) => [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];

// Clockwise quarter turn about outward normal a.
function quarterTurn(v, a) {
  const c = cross(v, a);
  const d = dot(v, a);
  return [c[0] + d * a[0], c[1] + d * a[1], c[2] + d * a[2]];
}

export function axisOf(face) {
  return NORMALS[face];
}

// True if a cubie at position p belongs to the layer that `face` turns.
export function inLayer(p, face) {
  return dot(p, NORMALS[face]) === 1;
}

// move = { face: "R", turns: 1 | 2 | 3 }  (3 = counterclockwise)
export function applyMove(stickers, move) {
  const a = NORMALS[move.face];
  return stickers.map((s) => {
    if (!inLayer(s.p, move.face)) return s;
    let p = s.p;
    let n = s.n;
    for (let i = 0; i < move.turns; i++) {
      p = quarterTurn(p, a);
      n = quarterTurn(n, a);
    }
    return { p, n, color: s.color };
  });
}

export function parseMoves(text) {
  const moves = [];
  const re = /\s*([URFDLB])(2'?|'|)\s*/gy;
  let m;
  let pos = 0;
  text = text.replace(/,/g, " ");
  while (pos < text.length) {
    re.lastIndex = pos;
    m = re.exec(text);
    if (!m) {
      if (/^\s*$/.test(text.slice(pos))) break;
      throw new Error(`Unknown move at "${text.slice(pos).trim().slice(0, 6)}". Use U R F D L B with 2 or '.`);
    }
    const turns = m[2].startsWith("2") ? 2 : m[2] === "'" ? 3 : 1;
    moves.push({ face: m[1], turns });
    pos = re.lastIndex;
  }
  return moves;
}

export function moveName(move) {
  return move.face + (move.turns === 2 ? "2" : move.turns === 3 ? "'" : "");
}

export function inverse(move) {
  return { face: move.face, turns: 4 - move.turns };
}
