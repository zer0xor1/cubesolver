// A small 3D renderer for the cube on a 2D canvas (no WebGL, no libraries).
//
// Each of the 26 visible cubies is a black box with colored stickers. We rotate
// points with the camera, project them with perspective, hide faces that point
// away, and paint from back to front. While a layer turns, the cube is split
// into two blocks (the turning layer and the rest); each block is convex, so
// painting the farther block first is always correct.
//
// The canvas shows two views of the same cube: the main view, and a smaller
// back view from the exact opposite direction. Together they show all six faces.
//
// Dragging a sticker turns its layer (reported through onSwipe); dragging
// anywhere else turns the view.

import { FACES, axisOf, inLayer } from "./cube-model.js";

export const STICKER_COLORS = {
  U: "#F4F4EC",
  R: "#C8102E",
  F: "#009B48",
  D: "#FFD500",
  L: "#FF6A13",
  B: "#0051BA",
};
const BODY = "#121526";
const HALF = 0.475; // half size of a cubie (spacing is 1, so there is a small gap)
const STICKER = 0.39; // half size of a sticker
const CAMERA = 11; // camera distance from the cube center

const add = (a, b) => [a[0] + b[0], a[1] + b[1], a[2] + b[2]];
const scale = (a, s) => [a[0] * s, a[1] * s, a[2] * s];
const dot = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
const cross = (a, b) => [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];

// Rodrigues' rotation of v about unit axis a by angle t.
function rotate(v, a, t) {
  if (t === 0) return v;
  const c = Math.cos(t);
  const s = Math.sin(t);
  const k = dot(a, v) * (1 - c);
  const x = cross(a, v);
  return [v[0] * c + x[0] * s + a[0] * k, v[1] * c + x[1] * s + a[1] * k, v[2] * c + x[2] * s + a[2] * k];
}

const AXES = [
  [1, 0, 0],
  [-1, 0, 0],
  [0, 1, 0],
  [0, -1, 0],
  [0, 0, 1],
  [0, 0, -1],
];

// Two unit vectors perpendicular to normal n, for building a face square.
function tangents(n) {
  const u = Math.abs(n[0]) === 1 ? [0, 1, 0] : [1, 0, 0];
  return [u, cross(n, u)];
}

function shade(hex, amount) {
  const v = parseInt(hex.slice(1), 16);
  const f = (x) => Math.round(Math.max(0, Math.min(255, x * amount)));
  return `rgb(${f(v >> 16)}, ${f((v >> 8) & 255)}, ${f(v & 255)})`;
}

// True if (x, y) is inside a convex polygon (either winding).
function insideConvex(points, x, y) {
  let sign = 0;
  for (let i = 0; i < points.length; i++) {
    const [ax, ay] = points[i];
    const [bx, by] = points[(i + 1) % points.length];
    const c = (bx - ax) * (y - ay) - (by - ay) * (x - ax);
    if (c === 0) continue;
    if (sign === 0) sign = Math.sign(c);
    else if (Math.sign(c) !== sign) return false;
  }
  return true;
}

const easeInOut = (t) => (t < 0.5 ? 2 * t * t : 1 - (-2 * t + 2) ** 2 / 2);

export class CubeRenderer {
  constructor(canvas) {
    this.canvas = canvas;
    this.ctx = canvas.getContext("2d");
    this.yaw = -0.62;
    this.pitch = 0.48;
    this.stickers = [];
    this.anim = null; // { face, angle, start, duration, resolve }
    this.frameRequested = false;
    this.onSwipe = null; // called with a move { face, turns } when a sticker is dragged
    this.faces = []; // every face drawn in the last frame, in painting order, for picking

    this.cubies = [];
    for (let x = -1; x <= 1; x++)
      for (let y = -1; y <= 1; y++)
        for (let z = -1; z <= 1; z++) if (x || y || z) this.cubies.push([x, y, z]);

    new ResizeObserver(() => this.resize()).observe(canvas);
    this.resize();
    this.installPointer();
  }

  setStickers(stickers) {
    this.stickers = stickers;
    this.requestFrame();
  }

  // Animates one face turn. Resolves when the turn has finished drawing.
  animateMove(move, duration) {
    if (duration <= 0) return Promise.resolve();
    const signed = move.turns === 3 ? -1 : move.turns;
    return new Promise((resolve) => {
      this.anim = {
        face: move.face,
        angle: (-Math.PI / 2) * signed, // clockwise = negative about the outward normal
        start: performance.now(),
        duration: duration * (move.turns === 2 ? 1.4 : 1),
        resolve,
      };
      this.requestFrame();
    });
  }

  resize() {
    const dpr = window.devicePixelRatio || 1;
    const { clientWidth: w, clientHeight: h } = this.canvas;
    this.canvas.width = Math.max(1, Math.round(w * dpr));
    this.canvas.height = Math.max(1, Math.round(h * dpr));
    this.requestFrame();
  }

  installPointer() {
    const SWIPE_PX = 10; // CSS pixels of movement before a sticker drag becomes a turn
    let drag = null; // { last, start, hit, mode: "pending" | "orbit" | "done" }
    this.canvas.addEventListener("pointerdown", (e) => {
      const hit = this.onSwipe ? this.pick(e) : null;
      drag = { last: [e.clientX, e.clientY], start: [e.clientX, e.clientY], hit, mode: hit ? "pending" : "orbit" };
      this.canvas.setPointerCapture(e.pointerId);
    });
    this.canvas.addEventListener("pointermove", (e) => {
      if (!drag) {
        if (e.pointerType === "mouse") this.canvas.style.cursor = this.onSwipe && this.pick(e) ? "pointer" : "";
        return;
      }
      if (drag.mode === "pending") {
        const dx = e.clientX - drag.start[0], dy = e.clientY - drag.start[1];
        if (Math.hypot(dx, dy) < SWIPE_PX) return;
        const move = this.swipeMove(drag.hit, dx, dy);
        if (move) {
          drag.mode = "done";
          this.onSwipe(move);
          return;
        }
        drag.mode = "orbit"; // a middle layer: the solver only turns outer faces
      }
      if (drag.mode !== "orbit") return;
      this.yaw += (e.clientX - drag.last[0]) * 0.008;
      this.pitch = Math.max(-1.45, Math.min(1.45, this.pitch + (e.clientY - drag.last[1]) * 0.008));
      drag.last = [e.clientX, e.clientY];
      this.requestFrame();
    });
    const end = () => (drag = null);
    this.canvas.addEventListener("pointerup", end);
    this.canvas.addEventListener("pointercancel", end);
  }

  // The outer face of a cubie under the pointer, or null. Uses the faces of the
  // last frame: the one painted last at that point is the one you see.
  pick(e) {
    const rect = this.canvas.getBoundingClientRect();
    const x = ((e.clientX - rect.left) * this.canvas.width) / rect.width;
    const y = ((e.clientY - rect.top) * this.canvas.height) / rect.height;
    for (let i = this.faces.length - 1; i >= 0; i--) {
      const f = this.faces[i];
      if (insideConvex(f.points, x, y)) return dot(f.p, f.n) === 1 ? f : null;
    }
    return null;
  }

  // Which face turn a drag of (dx, dy) screen pixels on a sticker means, or
  // null for a middle layer.
  swipeMove({ p, n, view }, dx, dy) {
    // The sticker can move along two axes on its face. Pick the one whose
    // screen direction is closest to the drag.
    const center = add(p, scale(n, HALF));
    const c0 = view.project(this.toView(center, view));
    let best = null;
    for (const d of AXES) {
      if (dot(d, n) !== 0) continue;
      const c1 = view.project(this.toView(add(center, scale(d, 0.5)), view));
      const sx = c1[0] - c0[0], sy = c1[1] - c0[1];
      const score = (sx * dx + sy * dy) / (Math.hypot(sx, sy) || 1);
      if (!best || score > best.score) best = { d, score };
    }
    // Turning by +90 degrees about r = n x d moves the sticker along d.
    const r = cross(n, best.d);
    const layer = dot(p, r); // -1, 0 or 1
    if (layer === 0) return null;
    // About its own outward normal, +90 degrees is counterclockwise (turns 3).
    const face = [...FACES].find((f) => dot(axisOf(f), r) === layer);
    return { face, turns: layer > 0 ? 3 : 1 };
  }

  // Lets the keyboard (arrow keys) turn the view too.
  nudgeView(dyaw, dpitch) {
    this.yaw += dyaw;
    this.pitch = Math.max(-1.45, Math.min(1.45, this.pitch + dpitch));
    this.requestFrame();
  }

  requestFrame() {
    if (this.frameRequested) return;
    this.frameRequested = true;
    requestAnimationFrame((now) => {
      this.frameRequested = false;
      this.draw(now);
    });
  }

  toView(v, view) {
    // Turn around the vertical axis (yaw), then tilt (pitch).
    const cy = Math.cos(view.yaw), sy = Math.sin(view.yaw);
    const x1 = v[0] * cy + v[2] * sy;
    const z1 = -v[0] * sy + v[2] * cy;
    const cp = Math.cos(view.pitch), sp = Math.sin(view.pitch);
    return [x1, v[1] * cp - z1 * sp, v[1] * sp + z1 * cp];
  }

  // Where the main view and the back view go on a w x h canvas.
  layout(w, h) {
    const main = { yaw: this.yaw, pitch: this.pitch };
    // Adding pi to yaw and negating pitch points the camera the opposite way.
    const back = { yaw: this.yaw + Math.PI, pitch: -this.pitch, label: "Back" };
    if (w >= 1.6 * h) {
      // Wide canvas: side by side.
      const size = Math.min(w * 0.6, h);
      Object.assign(main, { cx: w * 0.38, cy: h / 2, size });
      Object.assign(back, { cx: w * 0.8, cy: h / 2, size: Math.min(w * 0.36, h) * 0.6 });
    } else {
      // Narrow canvas: main view top left, back view in the bottom right corner.
      const m = Math.min(w, h);
      Object.assign(main, { cx: w * 0.44, cy: h * 0.44, size: m * 0.84 });
      Object.assign(back, { cx: w - m * 0.2, cy: h - m * 0.2, size: m * 0.36 });
    }
    return [main, back];
  }

  draw(now) {
    const { ctx, canvas } = this;
    const w = canvas.width, h = canvas.height;
    ctx.clearRect(0, 0, w, h);

    let turnFace = null, turnAngle = 0, finished = null;
    if (this.anim) {
      const t = Math.min(1, (now - this.anim.start) / this.anim.duration);
      turnFace = this.anim.face;
      turnAngle = this.anim.angle * easeInOut(t);
      if (t >= 1) {
        // Draw the finished turn with the old stickers rotated all the way, which
        // looks exactly like the new state; the caller swaps stickers right after.
        finished = this.anim.resolve;
        this.anim = null;
      } else {
        this.requestFrame();
      }
    }

    const axis = turnFace ? axisOf(turnFace) : null;
    const place = (v, moving) => (moving ? rotate(v, axis, turnAngle) : v);

    ctx.lineJoin = "round";
    this.faces = [];
    for (const view of this.layout(w, h)) this.drawView(view, axis, turnFace, place);
    if (finished) finished();
  }

  drawView(view, axis, turnFace, place) {
    const { ctx } = this;
    const focal = (view.size * 0.45 * CAMERA) / 2.7;
    const project = (v) => {
      const s = focal / (CAMERA - v[2]);
      return [view.cx + v[0] * s, view.cy - v[1] * s];
    };
    view.project = project;

    // Split cubies into the turning layer and the rest. The two blocks are
    // separated by the plane p . axis = 0.5; the block on the camera's side of
    // that plane is nearer, so it is painted last.
    const rest = [], layer = [];
    for (const p of this.cubies) {
      const moving = !!axis && inLayer(p, turnFace);
      (moving ? layer : rest).push({ p, moving, depth: this.toView(place(p, moving), view)[2] });
    }
    let groups = [rest];
    if (axis) {
      const vn = this.toView(axis, view);
      const q = this.toView(scale(axis, 0.5), view);
      const layerNearer = dot(vn, [-q[0], -q[1], CAMERA - q[2]]) > 0;
      groups = layerNearer ? [rest, layer] : [layer, rest];
    }

    for (const g of groups) {
      g.sort((a, b) => a.depth - b.depth);
      for (const item of g) this.drawCubie(item, place, project, view);
    }

    if (view.label) {
      const dpr = window.devicePixelRatio || 1;
      ctx.fillStyle = getComputedStyle(this.canvas).color;
      ctx.font = `600 ${Math.round(13 * dpr)}px system-ui, sans-serif`;
      ctx.textAlign = "center";
      ctx.textBaseline = "bottom";
      ctx.fillText(view.label, view.cx, view.cy - view.size * 0.5);
    }
  }

  drawCubie({ p, moving }, place, project, view) {
    const { ctx } = this;
    const dpr = window.devicePixelRatio || 1;
    for (const n of AXES) {
      const center = place(add(p, scale(n, HALF)), moving);
      const normal = place(n, moving);
      const vc = this.toView(center, view);
      const vn = this.toView(normal, view);
      // Visible if the face points toward the camera at (0, 0, CAMERA).
      const toCamera = [-vc[0], -vc[1], CAMERA - vc[2]];
      if (dot(vn, toCamera) <= 0) continue;

      const [u, v] = tangents(n);
      const square = (size, lift) =>
        [
          [1, 1],
          [-1, 1],
          [-1, -1],
          [1, -1],
        ].map(([a, b]) =>
          project(this.toView(place(add(add(p, scale(n, HALF + lift)), add(scale(u, a * size), scale(v, b * size))), moving), view)),
        );

      const facing = Math.max(0, dot(vn, [0, 0, 1]));
      const body = square(HALF, 0);
      this.faces.push({ p, n, view, points: body });
      this.fillPolygon(body, BODY, BODY, 1 * dpr);

      const color = this.stickerColor(p, n);
      if (color) {
        const fill = shade(color, 0.62 + 0.38 * facing);
        this.fillPolygon(square(STICKER, 0.002), fill, fill, 3.5 * dpr);
      }
    }
  }

  stickerColor(p, n) {
    if (!this.stickerIndex || this.stickerIndexSource !== this.stickers) {
      this.stickerIndex = new Map(this.stickers.map((s) => [`${s.p}|${s.n}`, s.color]));
      this.stickerIndexSource = this.stickers;
    }
    const c = this.stickerIndex.get(`${p}|${n}`);
    return c ? STICKER_COLORS[c] || "#6B7280" : null;
  }

  fillPolygon(points, fill, stroke, lineWidth) {
    const { ctx } = this;
    ctx.beginPath();
    ctx.moveTo(points[0][0], points[0][1]);
    for (let i = 1; i < points.length; i++) ctx.lineTo(points[i][0], points[i][1]);
    ctx.closePath();
    ctx.fillStyle = fill;
    ctx.fill();
    ctx.strokeStyle = stroke;
    ctx.lineWidth = lineWidth;
    ctx.stroke();
  }
}
