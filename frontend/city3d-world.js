/* ==========================================================================
   OPERA 3D - the city around the road graph (city3d.js builds it once)

   Everything here is generated from the engine's map: the platform fits the
   node positions, streets follow the roads, and buildings, trees, street lamps,
   parks and the lake are placed in the free space between them. A seeded random
   generator (from the map itself) makes the same map always produce the same
   city, so it does not change between page reloads.

   Data-driven touches: a node whose name contains "Park" gets a park around it,
   and one whose name contains "Lake" gets a lake next to it.
   ========================================================================== */

import * as THREE from 'three';
import { RoundedBoxGeometry } from 'three/addons/geometries/RoundedBoxGeometry.js';
import { CSS2DObject } from 'three/addons/renderers/CSS2DRenderer.js';
import { U, std, windowMaterial, textures, figureParts } from './city3d-models.js';

/* Where the dynamic objects sit relative to their node (world units). */
export const HOSP_OFFSET = new THREE.Vector3(2.45, 0, -2.45);
export const PIN_OFFSET = new THREE.Vector3(-1.55, 0, -1.55);
export const PARK_OFFSET = new THREE.Vector3(-2.55, 0, 0.75);

export const ROAD_W = 1.15;       // asphalt width
const WALK_W = 1.85;              // asphalt + both sidewalks
const PLAZA_R = 1.25;             // junction asphalt radius
const MARGIN = 9.5;               // platform beyond the outermost nodes
const BELT = 3.2;                 // green belt around the edge

const esc = s => String(s == null ? '' : s).replace(/[&<>"']/g,
  c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
const round1 = x => Math.round(x * 10) / 10;

function label(className, html, center) {
  const el = document.createElement('div');
  el.className = 'o3d-lbl ' + className;
  el.innerHTML = html;
  const o = new CSS2DObject(el);
  o.center.set(center[0], center[1]);
  return o;
}

/* small, fast, seeded random numbers */
function rng(seedText) {
  let h = 2166136261;
  for (let i = 0; i < seedText.length; i++) h = Math.imul(h ^ seedText.charCodeAt(i), 16777619);
  let a = h >>> 0;
  return () => {
    a = (a + 0x6D2B79F5) | 0;
    let t = Math.imul(a ^ (a >>> 15), 1 | a);
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

function segDist(px, pz, s) {
  const dx = s.b.x - s.a.x, dz = s.b.z - s.a.z;
  const t = Math.max(0, Math.min(1, ((px - s.a.x) * dx + (pz - s.a.z) * dz) / (dx * dx + dz * dz || 1)));
  return Math.hypot(px - (s.a.x + dx * t), pz - (s.a.z + dz * t));
}

/* InstancedMesh from a list of matrices (and optional colours). */
function instanced(geo, mat, items, { shadow = true, receive = true } = {}) {
  const im = new THREE.InstancedMesh(geo, mat, Math.max(1, items.length));
  im.count = items.length;
  items.forEach((it, i) => {
    im.setMatrixAt(i, it.m);
    if (it.c) im.setColorAt(i, it.c);
  });
  im.castShadow = shadow;
  im.receiveShadow = receive;
  im.instanceMatrix.needsUpdate = true;
  if (im.instanceColor) im.instanceColor.needsUpdate = true;
  return im;
}

const Y_AXIS = new THREE.Vector3(0, 1, 0);
const mtx = (x, y, z, yaw = 0, sx = 1, sy = 1, sz = 1) =>
  new THREE.Matrix4().compose(new THREE.Vector3(x, y, z),
    new THREE.Quaternion().setFromAxisAngle(Y_AXIS, yaw), new THREE.Vector3(sx, sy, sz));

/**
 * Build the city.
 * @param nodes, roads, hospitals   engine state
 * @param world(x, y, h)            map position -> THREE.Vector3
 * @returns { group, nodePicks, nodeLabels, weightLabels, size:{w,d}, update(time, night) }
 */
export function buildWorld({ nodes, roads, hospitals, world, seed }) {
  const R = rng(seed);
  const group = new THREE.Group();
  const P = nodes.map(n => world(n.x, n.y));
  const xs = P.map(p => p.x), zs = P.map(p => p.z);
  const minX = Math.min(...xs) - MARGIN, maxX = Math.max(...xs) + MARGIN;
  const minZ = Math.min(...zs) - MARGIN, maxZ = Math.max(...zs) + MARGIN;
  const W = maxX - minX, D = maxZ - minZ, cx = (minX + maxX) / 2, cz = (minZ + maxZ) / 2;

  /* ---------- platform (a city diorama on layered ground) ---------- */
  const top = new THREE.Mesh(new RoundedBoxGeometry(W, 0.36, D, 3, 0.3), std('#C9CCC6', { roughness: 0.95 }));
  top.position.set(cx, -0.18, cz);
  top.receiveShadow = true;
  const earth = new THREE.Mesh(new RoundedBoxGeometry(W - 0.12, 2.3, D - 0.12, 3, 0.3), std('#2A3139', { roughness: 0.85 }));
  earth.position.set(cx, -1.5, cz);
  earth.castShadow = true;
  const rock = new THREE.Mesh(new RoundedBoxGeometry(W - 0.7, 1.0, D - 0.7, 3, 0.3), std('#1B2026', { roughness: 0.85 }));
  rock.position.set(cx, -3.05, cz);
  group.add(top, earth, rock);

  // green belt around the edge
  const grass = std('#6C8B5C', { roughness: 1 });
  const strip = (w, d, x, z) => {
    const m = new THREE.Mesh(new THREE.PlaneGeometry(w, d), grass);
    m.rotation.x = -Math.PI / 2;
    m.position.set(x, 0.012, z);
    m.receiveShadow = true;
    group.add(m);
  };
  strip(W - 0.6, BELT, cx, minZ + BELT / 2 + 0.3);
  strip(W - 0.6, BELT, cx, maxZ - BELT / 2 - 0.3);
  strip(BELT, D - 2 * BELT - 0.6, minX + BELT / 2 + 0.3, cz);
  strip(BELT, D - 2 * BELT - 0.6, maxX - BELT / 2 - 0.3, cz);

  /* ---------- roads ---------- */
  const segs = [];
  roads.forEach(r => {
    const a = P[r.a], b = P[r.b];
    if (!a || !b) return;
    segs.push({ a, b, r, len: a.distanceTo(b), ang: Math.atan2(b.z - a.z, b.x - a.x) });
  });
  const roadDist = (x, z) => segs.reduce((m, s) => Math.min(m, segDist(x, z, s)), Infinity);
  const nearestSeg = (x, z) => segs.reduce((best, s) => {
    const d = segDist(x, z, s);
    return d < best.d ? { d, s } : best;
  }, { d: Infinity, s: null });

  const asphalt = std('#31373C', { roughness: 0.9 });
  const walk = std('#B6BAB4', { roughness: 0.95 });
  const weightLabels = [];
  const dashes = [];
  segs.forEach(s => {
    const mid = s.a.clone().add(s.b).multiplyScalar(0.5);
    const sw = new THREE.Mesh(new THREE.BoxGeometry(s.len, 0.08, WALK_W), walk);
    sw.position.copy(mid).setY(0.04);
    sw.rotation.y = -s.ang;
    sw.receiveShadow = true;
    const rd = new THREE.Mesh(new THREE.BoxGeometry(s.len, 0.1, ROAD_W), asphalt);
    rd.position.copy(mid).setY(0.05);
    rd.rotation.y = -s.ang;
    rd.receiveShadow = true;
    group.add(sw, rd);
    const n = Math.floor((s.len - 2 * PLAZA_R) / 0.9);
    for (let i = 0; i < n; i++) {
      const t = (PLAZA_R + 0.45 + i * 0.9) / s.len;
      if (t > 1 - PLAZA_R / s.len) break;
      const p = s.a.clone().lerp(s.b, t);
      dashes.push({ m: mtx(p.x, 0.105, p.z, -s.ang) });
    }
    const wl = label('o3d-w', esc(round1(s.r.minutes)) + ' min' + (s.r.oneWay ? ' →' : ''), [0.5, 0.5]);
    wl.position.copy(mid).setY(0.5);
    weightLabels.push(wl);
    group.add(wl);
  });
  group.add(instanced(new THREE.BoxGeometry(0.42, 0.012, 0.06), std('#F2F2EA', { roughness: 0.6 }), dashes, { shadow: false }));

  /* ---------- junction plazas: small roundabouts ---------- */
  const nodePicks = [], nodeLabels = new Map();
  const plazaWalk = new THREE.CylinderGeometry(PLAZA_R + 0.38, PLAZA_R + 0.38, 0.09, 48);
  const plazaRoad = new THREE.CylinderGeometry(PLAZA_R, PLAZA_R, 0.11, 48);
  const island = new THREE.CylinderGeometry(0.46, 0.5, 0.16, 32);
  const curb = new THREE.TorusGeometry(0.5, 0.04, 8, 40);
  const islandMat = std('#6E9A5C', { roughness: 1 });
  const curbMat = std('#EDEFEA', { roughness: 0.7 });
  nodes.forEach((n, i) => {
    const p = P[i];
    const w0 = new THREE.Mesh(plazaWalk, walk);
    w0.position.copy(p).setY(0.045);
    const r0 = new THREE.Mesh(plazaRoad, asphalt);
    r0.position.copy(p).setY(0.056);
    r0.receiveShadow = w0.receiveShadow = true;
    r0.userData.pick = { kind: 'node', id: n.id };
    const is = new THREE.Mesh(island, islandMat);
    is.position.copy(p).setY(0.12);
    is.castShadow = true;
    const cb = new THREE.Mesh(curb, curbMat);
    cb.rotation.x = Math.PI / 2;
    cb.position.copy(p).setY(0.12);
    is.userData.pick = r0.userData.pick;
    nodePicks.push(r0, is);
    const nl = label('o3d-node', `<b>${n.id}</b>${esc(n.name)}`, [0.5, -0.55]);
    nl.position.copy(p).setY(0.3);
    nodeLabels.set(n.id, nl);
    group.add(w0, r0, is, cb, nl);
  });

  /* ---------- reserved places (hospitals, pins, parked ambulances) ---------- */
  const keepOut = [];
  hospitals.forEach(h => { if (P[h.node]) keepOut.push({ p: P[h.node].clone().add(HOSP_OFFSET).add(new THREE.Vector3(0.2, 0, 0.5)), r: 2.6 }); });
  P.forEach(p => {
    keepOut.push({ p, r: PLAZA_R + 0.7 });
    keepOut.push({ p: p.clone().add(PIN_OFFSET), r: 1.1 });
    keepOut.push({ p: p.clone().add(PARK_OFFSET), r: 1.8 });
  });

  /* ---------- lake (next to a node called "...Lake...") ---------- */
  let lake = null;
  const lakeNode = nodes.findIndex(n => /lake/i.test(n.name));
  if (lakeNode >= 0) {
    const c = P[lakeNode].clone().add(new THREE.Vector3(0.6, 0, 5.1));
    lake = { x: c.x, z: c.z, rx: 6.2, rz: 3.3 };
    const shape = new THREE.Shape();
    shape.absellipse(0, 0, lake.rx, lake.rz, 0, Math.PI * 2, false, 0);
    const shoreShape = new THREE.Shape();
    shoreShape.absellipse(0, 0, lake.rx + 0.75, lake.rz + 0.6, 0, Math.PI * 2, false, 0);
    const shore = new THREE.Mesh(new THREE.ShapeGeometry(shoreShape, 64), std('#D9CDA8', { roughness: 1 }));
    shore.rotation.x = -Math.PI / 2;
    shore.position.set(c.x, 0.016, c.z);
    shore.receiveShadow = true;
    const waterMat = std('#2A5C74', { roughness: 0.06, metalness: 0.2, envMapIntensity: 1.3 });
    waterMat.onBeforeCompile = shader => {
      shader.uniforms.uTime = U.uTime;
      shader.uniforms.uNight = U.uNight;
      shader.vertexShader = 'varying vec3 vWPosW;\n' + shader.vertexShader.replace('#include <project_vertex>',
        '#include <project_vertex>\n vWPosW = (modelMatrix * vec4(transformed, 1.0)).xyz;');
      shader.fragmentShader = 'varying vec3 vWPosW; uniform float uTime; uniform float uNight;\n' +
        shader.fragmentShader.replace('#include <normal_fragment_begin>', `#include <normal_fragment_begin>
          vec2 wq = vWPosW.xz;
          float w1 = sin(wq.x * 2.3 + uTime * 1.3) + sin(wq.y * 3.1 - uTime * 1.05) + sin((wq.x + wq.y) * 1.7 + uTime * 0.8);
          float w2 = cos(wq.x * 2.8 - uTime * 1.15) + cos(wq.y * 2.2 + uTime * 1.45);
          normal = normalize(normal + vec3(w1, 0.0, w2) * 0.045);`)
          .replace('#include <emissivemap_fragment>', `#include <emissivemap_fragment>
          totalEmissiveRadiance += vec3(0.02, 0.06, 0.12) * uNight;`);
    };
    const water = new THREE.Mesh(new THREE.ShapeGeometry(shape, 64), waterMat);
    water.rotation.x = -Math.PI / 2;
    water.position.set(c.x, 0.03, c.z);
    water.receiveShadow = true;
    group.add(shore, water);
  }
  const inLake = (x, z, pad = 0) => lake &&
    ((x - lake.x) / (lake.rx + 0.9 + pad)) ** 2 + ((z - lake.z) / (lake.rz + 0.8 + pad)) ** 2 < 1;

  /* ---------- parks (around nodes called "...Park...") ---------- */
  const parks = [];
  nodes.forEach((n, i) => {
    if (!/park/i.test(n.name)) return;
    const pk = { x: P[i].x, z: P[i].z, r: 4.2 };
    parks.push(pk);
    const m = new THREE.Mesh(new THREE.CircleGeometry(pk.r, 48), grass);
    m.rotation.x = -Math.PI / 2;
    m.position.set(pk.x, 0.014, pk.z);
    m.receiveShadow = true;
    group.add(m);
  });
  const inPark = (x, z, pad = 0) => parks.some(pk => Math.hypot(x - pk.x, z - pk.z) < pk.r + pad);
  const inBelt = (x, z) => x < minX + BELT + 0.3 || x > maxX - BELT - 0.3 || z < minZ + BELT + 0.3 || z > maxZ - BELT - 0.3;
  const freeOf = (x, z, r) => !keepOut.some(k => Math.hypot(x - k.p.x, z - k.p.z) < k.r + r);

  /* ---------- buildings ---------- */
  // real facade materials: concrete, stone, render and brick tones (no toy pastels)
  const palette = ['#D7D3CB', '#D9A58A', '#8FB8B4', '#E0C27A', '#A9B9D6', '#C98F86', '#A7BF9B', '#CDB8DA',
                   '#E8C9A8', '#7FA7B5', '#D8D0C3', '#B9C4CC'];   // stone, terracotta, teal, mustard, slate, coral, sage, lilac
  const glassTints = ['#7FB2D9', '#7FD1C7', '#C9A27A', '#A895D8', '#86A7E8', '#9AD0E6'];
  const neon = ['#22D3EE', '#F472B6', '#FBBF24', '#A3E635', '#818CF8', '#FB923C'];
  const strips = [];
  const blocks = [], crowns = [], units = [], avLights = [], treeSpots = [], towers = [];
  const step = 2.15;
  for (let x = minX + BELT + 1.1; x < maxX - BELT - 0.9; x += step) {
    for (let z = minZ + BELT + 1.1; z < maxZ - BELT - 0.9; z += step) {
      const px = x + (R() - 0.5) * 0.6, pz = z + (R() - 0.5) * 0.6;
      const near = nearestSeg(px, pz);
      if (!near.s) continue;
      let sx = 0.95 + R() * 0.75, sz = 0.95 + R() * 0.75;
      const dc = Math.hypot(px - cx, pz - cz);
      const downtown = Math.exp(-(dc * dc) / (2 * 11.5 * 11.5));
      const tower = downtown > 0.45 && R() < 0.1;
      if (tower) { sx *= 0.82; sz *= 0.82; }
      const rad = 0.5 * Math.hypot(sx, sz);
      // the side facing the nearest street must clear the sidewalk
      if (near.d < WALK_W / 2 + sz / 2 + 0.18) continue;
      if (segs.some(s => s !== near.s && segDist(px, pz, s) < WALK_W / 2 + rad + 0.12)) continue;
      if (!freeOf(px, pz, rad) || inLake(px, pz, rad) || inPark(px, pz, rad)) continue;
      if (R() < 0.3) { if (R() < 0.6) treeSpots.push([px, pz]); continue; }   // leave gaps: small gardens
      let h = 0.6 + R() * 1.0 + downtown * (0.8 + R() * 3.6);
      if (tower) h += 2.6 + R() * 3.2;
      const yaw = -near.s.ang;
      const tint = new THREE.Color(palette[Math.floor(R() * palette.length)]);
      (tower ? towers : blocks).push({ m: mtx(px, 0, pz, yaw, sx, h, sz),
        c: tower ? new THREE.Color(glassTints[Math.floor(R() * glassTints.length)]) : tint });
      let roof = h;
      if (h > 3.1 && R() < 0.65) {
        const ch = 0.5 + R() * 0.9;
        crowns.push({ m: mtx(px, h, pz, yaw, sx * 0.68, ch, sz * 0.68), c: tint });
        roof = h + ch;
      }
      const nu = R() < 0.75 ? 1 + Math.floor(R() * 2) : 0;
      const cos = Math.cos(-yaw), sin = Math.sin(-yaw);
      for (let k = 0; k < nu; k++) {
        const ox = (R() - 0.5) * sx * 0.45, oz = (R() - 0.5) * sz * 0.45;
        units.push({ m: mtx(px + ox * cos - oz * sin, roof, pz + ox * sin + oz * cos, yaw, 0.22 + R() * 0.2, 0.12 + R() * 0.12, 0.22 + R() * 0.2) });
      }
      if (roof > 4.6) avLights.push({ m: mtx(px, roof + 0.12, pz) });
      // a glowing LED crown on taller buildings
      if (h > 1.8 && R() < 0.55) {
        strips.push({ m: mtx(px, h - 0.12, pz, yaw, sx * 1.03, 0.07, sz * 1.03), c: new THREE.Color(neon[Math.floor(R() * neon.length)]) });
      }
    }
  }
  const unitBox = new RoundedBoxGeometry(1, 1, 1, 2, 0.06);   // softly rounded edges: sleek, not blocky
  unitBox.translate(0, 0.5, 0);
  const bMat = windowMaterial('#FFFFFF');
  // downtown towers in reflective glass
  const glassMat = windowMaterial('#AFC0CE', { glass: '#1F3445', winW: 0.3, winH: 0.36, litShare: 0.5 });
  glassMat.metalness = 0.55; glassMat.roughness = 0.18; glassMat.envMapIntensity = 1.4;
  group.add(instanced(unitBox, bMat, blocks), instanced(unitBox, bMat, crowns), instanced(unitBox, glassMat, towers));
  group.add(instanced(unitBox, std('#80898C', { roughness: 0.7 }), units));
  const stripBox = new THREE.BoxGeometry(1, 1, 1);
  const stripMat = new THREE.MeshBasicMaterial({ color: '#FFFFFF', toneMapped: false });
  group.add(instanced(stripBox, stripMat, strips, { shadow: false, receive: false }));
  const avMat = new THREE.MeshStandardMaterial({ color: '#FF3B30', emissive: '#FF2A20', emissiveIntensity: 0 });
  group.add(instanced(new THREE.SphereGeometry(0.07, 10, 8), avMat, avLights, { shadow: false }));

  /* ---------- trees ---------- */
  const trees = [];
  const tryTree = (x, z) => {
    if (roadDist(x, z) < WALK_W / 2 + 0.35) return;
    if (!freeOf(x, z, 0.35) || inLake(x, z, -0.3)) return;
    trees.push([x, z]);
  };
  for (let x = minX + 0.9; x < maxX - 0.7; x += 1.15) {
    for (let z = minZ + 0.9; z < maxZ - 0.7; z += 1.15) {
      const jx = x + (R() - 0.5) * 0.7, jz = z + (R() - 0.5) * 0.7;
      if (inBelt(jx, jz) && R() < 0.72) tryTree(jx, jz);
    }
  }
  parks.forEach(pk => {
    for (let k = 0; k < 26; k++) {
      const a = R() * Math.PI * 2, d = PLAZA_R + 1.0 + R() * (pk.r - PLAZA_R - 1.2);
      tryTree(pk.x + Math.cos(a) * d, pk.z + Math.sin(a) * d);
    }
  });
  if (lake) {
    for (let k = 0; k < 26; k++) {
      const a = R() * Math.PI * 2, f = 1.2 + R() * 0.45;
      tryTree(lake.x + Math.cos(a) * lake.rx * f, lake.z + Math.sin(a) * lake.rz * f);
    }
  }
  treeSpots.forEach(([x, z]) => {
    for (let k = 0; k < 3; k++) tryTree(x + (R() - 0.5) * 1.1, z + (R() - 0.5) * 1.1);
  });
  const greens = ['#3E6B3A', '#4A7A42', '#355E34', '#56854A', '#2F5530', '#46703F'];
  const blossoms = ['#E58FB0', '#F2B45A', '#B58AD8', '#F28C8C'];   // flowering trees for colour
  const crownItems = [], trunkItems = [];
  trees.forEach(([x, z]) => {
    const s = 0.7 + R() * 0.65;
    trunkItems.push({ m: mtx(x, 0, z, 0, s, s, s) });
    crownItems.push({ m: mtx(x, 0.32 * s + 0.42 * s, z, R() * 6, s, s * (0.9 + R() * 0.4), s), c: new THREE.Color(R() < 0.22 ? blossoms[Math.floor(R() * blossoms.length)] : greens[Math.floor(R() * greens.length)]) });
  });
  const trunkGeo = new THREE.CylinderGeometry(0.05, 0.075, 0.42, 6);
  trunkGeo.translate(0, 0.21, 0);
  const crownGeo = new THREE.IcosahedronGeometry(0.46, 2);   // smooth, natural crowns
  group.add(instanced(trunkGeo, std('#6B4F3A'), trunkItems));
  group.add(instanced(crownGeo, std('#FFFFFF', { roughness: 0.95 }), crownItems));

  /* ---------- street lamps with pools of light ---------- */
  const poles = [], bulbs = [], pools = [];
  segs.forEach(s => {
    const dir = new THREE.Vector3(Math.cos(s.ang), 0, Math.sin(s.ang));
    const perp = new THREE.Vector3(-dir.z, 0, dir.x);
    const n = Math.floor((s.len - 2.6) / 3.0);
    for (let i = 0; i <= n; i++) {
      const along = 1.3 + i * ((s.len - 2.6) / Math.max(1, n));
      const side = i % 2 ? 1 : -1;
      const base = s.a.clone().addScaledVector(dir, along).addScaledVector(perp, side * 0.98);
      if (P.some(p => Math.hypot(base.x - p.x, base.z - p.z) < PLAZA_R + 0.7)) continue;
      const head = base.clone().addScaledVector(perp, -side * 0.24);
      poles.push({ m: mtx(base.x, 0, base.z) });
      bulbs.push({ m: mtx(head.x, 1.22, head.z) });
      const pool = base.clone().addScaledVector(perp, -side * 0.55);
      pools.push({ m: mtx(pool.x, 0.115, pool.z) });
    }
  });
  const poleGeo = new THREE.CylinderGeometry(0.03, 0.045, 1.28, 6);
  poleGeo.translate(0, 0.64, 0);
  group.add(instanced(poleGeo, std('#5C6669', { metalness: 0.5, roughness: 0.4 }), poles, { receive: false }));
  const bulbMat = new THREE.MeshStandardMaterial({ color: '#FFF4DA', emissive: '#FFC27A', emissiveIntensity: 0 });
  group.add(instanced(new THREE.BoxGeometry(0.34, 0.06, 0.14), bulbMat, bulbs, { shadow: false }));
  const poolGeo = new THREE.PlaneGeometry(2.7, 2.7);
  poolGeo.rotateX(-Math.PI / 2);
  const poolMat = new THREE.MeshBasicMaterial({
    map: textures().glow, color: '#FFB866', transparent: true, opacity: 0, depthWrite: false,
    blending: THREE.AdditiveBlending, toneMapped: false
  });
  const poolMesh = instanced(poolGeo, poolMat, pools, { shadow: false, receive: false });
  poolMesh.renderOrder = 1;
  group.add(poolMesh);

  /* ---------- traffic: cars drive the road graph, keeping left ---------- */
  const adj = P.map(() => []);
  segs.forEach((sg, i) => { adj[sg.r.a].push({ seg: i, fromA: true }); adj[sg.r.b].push({ seg: i, fromA: false }); });
  const carColors = ['#D64541', '#2E86DE', '#F5F6FA', '#2D3436', '#F39C12', '#16A085', '#8E44AD', '#BDC3C7', '#E17055', '#0984E3', '#F7F1E3'];
  const cars = [];
  const nCars = segs.length ? Math.min(46, segs.length * 2 + 4) : 0;
  for (let i = 0; i < nCars; i++) {
    cars.push({ seg: Math.floor(R() * segs.length), dir: R() < 0.5 ? 1 : -1, t: R(), speed: 1.3 + R() * 1.1,
                color: new THREE.Color(carColors[Math.floor(R() * carColors.length)]) });
  }
  const carBody = new THREE.BoxGeometry(0.64, 0.2, 0.34);
  const carCabin = new THREE.BoxGeometry(0.34, 0.15, 0.3);
  const carLight = new THREE.BoxGeometry(0.03, 0.05, 0.26);
  const carPool = new THREE.PlaneGeometry(1.5, 0.9);
  carPool.rotateX(-Math.PI / 2);
  const mk = (geo, mat, shadow) => {
    const im = new THREE.InstancedMesh(geo, mat, Math.max(1, nCars));
    im.count = nCars;
    im.castShadow = shadow;
    im.frustumCulled = false;
    group.add(im);
    return im;
  };
  const bodies = mk(carBody, std('#FFFFFF', { roughness: 0.35, metalness: 0.3 }), true);
  const cabins = mk(carCabin, std('#1F2A30', { roughness: 0.2, metalness: 0.5 }), true);
  const headMat = new THREE.MeshStandardMaterial({ color: '#FFFFFF', emissive: '#FFF4D6', emissiveIntensity: 0 });
  const tailMat = new THREE.MeshStandardMaterial({ color: '#7A1010', emissive: '#FF2A1A', emissiveIntensity: 0 });
  const heads = mk(carLight, headMat, false), tails = mk(carLight, tailMat, false);
  const carPoolMat = new THREE.MeshBasicMaterial({ map: textures().glow, color: '#FFE8B8', transparent: true, opacity: 0,
    depthWrite: false, blending: THREE.AdditiveBlending, toneMapped: false });
  const pools2 = mk(carPool, carPoolMat, false);
  pools2.renderOrder = 2;
  cars.forEach((c, i) => bodies.setColorAt(i, c.color));
  if (bodies.instanceColor) bodies.instanceColor.needsUpdate = true;
  const tmpQ = new THREE.Quaternion(), tmpM = new THREE.Matrix4(), one = new THREE.Vector3(1, 1, 1);
  const place = (im, i, pos, yaw, fx, y, fz = 0) => {
    const cosY = Math.cos(yaw), sinY = Math.sin(yaw);
    tmpQ.setFromAxisAngle(Y_AXIS, yaw);
    // local (fx, fz) rotated by yaw around Y
    tmpM.compose(new THREE.Vector3(pos.x + fx * cosY + fz * sinY, y, pos.z - fx * sinY + fz * cosY), tmpQ, one);
    im.setMatrixAt(i, tmpM);
  };
  let lastTime = null;
  function moveTraffic(time, night) {
    const dt = lastTime === null ? 0 : Math.min(0.1, time - lastTime);
    lastTime = time;
    cars.forEach((c, i) => {
      let sg = segs[c.seg];
      c.t += (c.speed * dt) / Math.max(0.1, sg.len);
      if (c.t >= 1) {
        const node = c.dir > 0 ? sg.r.b : sg.r.a;
        const opts = adj[node].filter(o => o.seg !== c.seg);
        const o = opts.length ? opts[Math.floor(Math.random() * opts.length)] : { seg: c.seg, fromA: c.dir < 0 };
        c.seg = o.seg;
        c.dir = o.fromA ? 1 : -1;
        c.t = 0;
        sg = segs[c.seg];
      }
      const from = c.dir > 0 ? sg.a : sg.b, to = c.dir > 0 ? sg.b : sg.a;
      const dx = (to.x - from.x) / sg.len, dz = (to.z - from.z) / sg.len;
      const along = c.t * sg.len, edge = Math.min(along, sg.len - along);
      const lane = 0.3 + 0.55 * Math.max(0, 1 - edge / 1.7);      // swing round the roundabout islands
      const pos = { x: from.x + dx * along + dz * lane, z: from.z + dz * along - dx * lane };
      const yaw = -Math.atan2(dz, dx);
      place(bodies, i, pos, yaw, 0, 0.21);
      place(cabins, i, pos, yaw, -0.05, 0.38);
      place(heads, i, pos, yaw, 0.33, 0.22);
      place(tails, i, pos, yaw, -0.33, 0.22);
      place(pools2, i, pos, yaw, 1.15, 0.12);
    });
    [bodies, cabins, heads, tails, pools2].forEach(m => { m.instanceMatrix.needsUpdate = true; });
    headMat.emissiveIntensity = 0.2 + night * 4;
    tailMat.emissiveIntensity = 0.3 + night * 3;
    carPoolMat.opacity = night * 0.32;
  }

  /* ---------- pedestrians on the sidewalks ---------- */
  const FIG = figureParts();
  const walkers = [];
  const nWalk = segs.length ? Math.min(70, segs.length * 3) : 0;
  for (let i = 0; i < nWalk; i++) {
    walkers.push({ seg: Math.floor(R() * segs.length), dir: R() < 0.5 ? 1 : -1, t: R(), side: R() < 0.5 ? 1 : -1,
                   speed: 0.28 + R() * 0.25, phase: R() * 6 });
  }
  const mkPeople = (geo, colors) => {
    const im = new THREE.InstancedMesh(geo, std('#FFFFFF', { roughness: 0.8 }), Math.max(1, nWalk));
    im.count = nWalk;
    im.castShadow = true;
    im.frustumCulled = false;
    walkers.forEach((w, i) => im.setColorAt(i, new THREE.Color(colors[Math.floor(R() * colors.length)])));
    if (im.instanceColor) im.instanceColor.needsUpdate = true;
    group.add(im);
    return im;
  };
  const walkBodies = mkPeople(FIG.standing, FIG.clothes), walkHeads = mkPeople(FIG.head, FIG.skins);
  const wq = new THREE.Quaternion(), wm = new THREE.Matrix4(), ws = new THREE.Vector3(0.42, 0.42, 0.42);
  let lastWalk = null;
  function moveWalkers(time) {
    const dt = lastWalk === null ? 0 : Math.min(0.1, time - lastWalk);
    lastWalk = time;
    walkers.forEach((w, i) => {
      let sg = segs[w.seg];
      w.t += (w.speed * dt) / Math.max(0.1, sg.len);
      if (w.t >= 1) {
        const node = w.dir > 0 ? sg.r.b : sg.r.a;
        const opts = adj[node];
        const o = opts[Math.floor(Math.random() * opts.length)];
        w.seg = o.seg;
        w.dir = o.fromA ? 1 : -1;
        w.t = 0;
        sg = segs[w.seg];
      }
      const from = w.dir > 0 ? sg.a : sg.b, to = w.dir > 0 ? sg.b : sg.a;
      const dx = (to.x - from.x) / sg.len, dz = (to.z - from.z) / sg.len;
      const along = w.t * sg.len, edge = Math.min(along, sg.len - along);
      const off = w.side * (0.76 + 0.7 * Math.max(0, 1 - edge / 1.9));   // the sidewalk, round the plazas
      const x = from.x + dx * along + dz * off, z = from.z + dz * along - dx * off;
      const bob = Math.abs(Math.sin(time * 7 + w.phase)) * 0.025;
      wq.setFromAxisAngle(Y_AXIS, -Math.atan2(dz, dx) + Math.sin(time * 7 + w.phase) * 0.08);
      wm.compose(new THREE.Vector3(x, 0.09 + bob, z), wq, ws);
      walkBodies.setMatrixAt(i, wm);
      walkHeads.setMatrixAt(i, wm);
    });
    walkBodies.instanceMatrix.needsUpdate = walkHeads.instanceMatrix.needsUpdate = true;
  }

  return {
    group, nodePicks, nodeLabels, weightLabels,
    size: { w: W, d: D, cx, cz },
    counts: { buildings: blocks.length, trees: trees.length, lamps: poles.length, cars: nCars, people: nWalk },
    update(time, night) {
      moveTraffic(time, night);
      moveWalkers(time);
      bulbMat.emissiveIntensity = 0.15 + night * 3.2;
      stripMat.color.setScalar((0.55 + 1.6 * night) * (0.92 + 0.08 * Math.sin(time * 1.5)));   // soft by day, neon at night
      poolMat.opacity = night * 0.32;
      avMat.emissiveIntensity = night * (Math.sin(time * 3.2) > 0.6 ? 6 : 0.4);
    }
  };
}
