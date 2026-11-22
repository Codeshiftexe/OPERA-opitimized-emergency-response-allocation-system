/* ==========================================================================
   OPERA 3D - models: ambulance, hospital, incident beacon, route
   Used by city3d.js. Pure drawing code: no engine logic here.

   U holds the uniforms shared by every animated material:
     uTime   seconds since the map opened (animations)
     uNight  0 = day, 1 = night (windows, lamps, glows fade in and out)
   ========================================================================== */

import * as THREE from 'three';
import { RoundedBoxGeometry } from 'three/addons/geometries/RoundedBoxGeometry.js';
import { mergeGeometries } from 'three/addons/utils/BufferGeometryUtils.js';

export const U = {
  uTime: { value: 0 }, uNight: { value: 1 },
  // x-ray cutaway: buildings between the camera and the tracked ambulance turn see-through
  uCam: { value: new THREE.Vector3() }, uFocus: { value: new THREE.Vector3() }, uCut: { value: 0 }
};

export const SEVERITY = {
  Cardiac:  { color: '#E5352B', label: 'Severe' },
  Trauma:   { color: '#F2994A', label: 'Serious' },
  Transfer: { color: '#27AE7A', label: 'Not severe' }
};
export const severityOf = type => SEVERITY[type] || { color: '#7A8B90', label: 'Unknown' };

export const std = (color, extra = {}) =>
  new THREE.MeshStandardMaterial({ color, roughness: 0.75, metalness: 0, ...extra });

/* ---------------- textures drawn on a canvas ---------------- */
function canvasTexture(size, draw) {
  const c = document.createElement('canvas');
  c.width = c.height = size;
  draw(c.getContext('2d'), size);
  const t = new THREE.CanvasTexture(c);
  t.colorSpace = THREE.SRGBColorSpace;
  return t;
}

let TEX = null;
export function textures() {
  if (TEX) return TEX;
  TEX = {
    // soft round glow, white in the middle
    glow: canvasTexture(128, (g, s) => {
      const r = g.createRadialGradient(s / 2, s / 2, 0, s / 2, s / 2, s / 2);
      r.addColorStop(0, 'rgba(255,255,255,1)');
      r.addColorStop(0.25, 'rgba(255,255,255,0.55)');
      r.addColorStop(0.6, 'rgba(255,255,255,0.12)');
      r.addColorStop(1, 'rgba(255,255,255,0)');
      g.fillStyle = r;
      g.fillRect(0, 0, s, s);
    }),
    // helipad: circle with an H
    helipad: canvasTexture(256, (g, s) => {
      g.fillStyle = '#2F3B40';
      g.beginPath(); g.arc(s / 2, s / 2, s / 2 - 4, 0, Math.PI * 2); g.fill();
      g.strokeStyle = '#F5F7F7'; g.lineWidth = 10;
      g.beginPath(); g.arc(s / 2, s / 2, s / 2 - 22, 0, Math.PI * 2); g.stroke();
      g.fillStyle = '#F5F7F7';
      g.font = `bold ${s * 0.5}px sans-serif`;
      g.textAlign = 'center'; g.textBaseline = 'middle';
      g.fillText('H', s / 2, s / 2 + 4);
    })
  };
  return TEX;
}

/* A flat glowing disc on the ground (light pools, ambulance glow). */
export function glowDecal(color, size, opacity = 1) {
  const m = new THREE.MeshBasicMaterial({
    map: textures().glow, color, transparent: true, opacity, depthWrite: false,
    blending: THREE.AdditiveBlending, toneMapped: false
  });
  const mesh = new THREE.Mesh(new THREE.PlaneGeometry(size, size), m);
  mesh.rotation.x = -Math.PI / 2;
  mesh.renderOrder = 2;
  return mesh;
}

/* ---------------- window shader (buildings and hospitals) ----------------
   Draws a grid of windows on the side walls of any box mesh whose geometry is
   a unit box standing on y = 0 (scaled per building). By day the windows are
   dark glass; at night about half of them light up, warm or cool. */
export function windowMaterial(baseColor = '#FFFFFF', { glass = '#4E6A7E', winW = 0.5, winH = 0.62, litShare = 0.38 } = {}) {
  const m = std(baseColor, { roughness: 0.82 });
  m.onBeforeCompile = shader => {
    shader.uniforms.uNight = U.uNight;
    shader.uniforms.uGlass = { value: new THREE.Color(glass) };
    shader.uniforms.uWin = { value: new THREE.Vector2(winW, winH) };
    shader.uniforms.uLit = { value: litShare };
    shader.uniforms.uCam = U.uCam;
    shader.uniforms.uFocus = U.uFocus;
    shader.uniforms.uCut = U.uCut;
    shader.vertexShader = 'varying vec3 vWPos; varying vec3 vWNorm; varying float vSeed; varying float vTop;\n' +
      shader.vertexShader.replace('#include <project_vertex>', `#include <project_vertex>
        mat4 bm = modelMatrix;
        #ifdef USE_INSTANCING
          bm = modelMatrix * instanceMatrix;
        #endif
        vec4 wp = bm * vec4(transformed, 1.0);
        vWPos = wp.xyz;
        vWNorm = normalize(mat3(bm) * objectNormal);
        vSeed = bm[3].x * 12.9898 + bm[3].z * 78.233;
        vTop = bm[3].y + length(bm[1].xyz);`);
    shader.fragmentShader = `varying vec3 vWPos; varying vec3 vWNorm; varying float vSeed; varying float vTop;
      uniform float uNight; uniform vec3 uGlass; uniform vec2 uWin; uniform float uLit;
      uniform vec3 uCam; uniform vec3 uFocus; uniform float uCut;
      float wHash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
      ` + shader.fragmentShader
      .replace('#include <color_fragment>', `#include <color_fragment>
        if (uCut > 0.5) {                              // see-through where it would hide the ambulance
          vec3 ab = uCam - uFocus;
          float tt = clamp(dot(vWPos - uFocus, ab) / max(dot(ab, ab), 1e-4), 0.0, 1.0);
          float dd = length(vWPos - (uFocus + ab * tt));
          if (tt > 0.03 && dd < 1.1 + 2.6 * tt && mod(floor(gl_FragCoord.x) + floor(gl_FragCoord.y), 2.0) < 1.0) discard;
        }
        vec3 wn = normalize(vWNorm);
        float side = 1.0 - step(0.5, abs(wn.y));
        vec2 tang = normalize(vec2(-wn.z, wn.x) + 1e-5);
        vec2 q = vec2(dot(vWPos.xz, tang) / uWin.x, vWPos.y / uWin.y);
        vec2 cell = floor(q), f = fract(q);
        float win = step(0.2, f.x) * step(f.x, 0.8) * step(0.26, f.y) * step(f.y, 0.8);
        win *= side * step(0.5, vWPos.y) * step(vWPos.y, vTop - 0.22);
        float h = wHash(cell + vec2(vSeed, vSeed * 0.37));
        diffuseColor.rgb = mix(diffuseColor.rgb, uGlass, win * 0.9);
        diffuseColor.rgb *= mix(1.0, 0.86, step(0.5, wn.y));
        float wLit = win * step(1.0 - uLit, h);`)
      .replace('#include <emissivemap_fragment>', `#include <emissivemap_fragment>
        vec3 warm = mix(vec3(1.0, 0.74, 0.42), vec3(0.62, 0.82, 1.0), step(0.78, fract(h * 7.31)));
        totalEmissiveRadiance += warm * wLit * uNight * (0.55 + 0.6 * fract(h * 13.7));`);
  };
  m.customProgramCacheKey = () => 'opera-windows-' + glass + winW + winH + litShare;
  return m;
}

/* ---------------- people (stick figures) ----------------
   One figure = a head (skin colour) + a body (torso, arms and legs, clothes colour).
   Three poses: standing, sitting (legs forward) and lying (for a patient).
   Height of a standing figure: about 0.72 units at scale 1. */
let FIG = null;
export function figureParts() {
  if (FIG) return FIG;
  const cyl = (r, h, x, y, z, rx = 0, rz = 0) => {
    const g = new THREE.CylinderGeometry(r, r, h, 8);
    g.rotateX(rx);
    g.rotateZ(rz);
    g.translate(x, y, z);
    return g;
  };
  const torso = () => cyl(0.056, 0.27, 0, 0.44, 0);
  const arms = y => [cyl(0.022, 0.24, 0, y, 0.085, 0.22), cyl(0.022, 0.24, 0, y, -0.085, -0.22)];
  const standing = mergeGeometries([torso(), ...arms(0.44),
    cyl(0.026, 0.31, 0, 0.155, 0.04), cyl(0.026, 0.31, 0, 0.155, -0.04)]);
  // sitting: hips at y 0.30, thighs forward (+x), shins down
  const seatTorso = cyl(0.056, 0.27, 0, 0.44, 0);
  const sitting = mergeGeometries([seatTorso, ...arms(0.44),
    cyl(0.026, 0.22, 0.11, 0.31, 0.04, 0, Math.PI / 2), cyl(0.026, 0.22, 0.11, 0.31, -0.04, 0, Math.PI / 2),
    cyl(0.026, 0.3, 0.22, 0.16, 0.04), cyl(0.026, 0.3, 0.22, 0.16, -0.04)]);
  const head = new THREE.SphereGeometry(0.085, 14, 10);
  head.translate(0, 0.645, 0);
  FIG = {
    standing, sitting, head,
    skins: ['#8D5524', '#A0673C', '#C68642', '#D9A066', '#E0AC69', '#F1C27D'],
    clothes: ['#2E86DE', '#D64541', '#27AE60', '#F39C12', '#8E44AD', '#34495E', '#ECF0F1', '#16A085', '#E17055', '#6C5CE7']
  };
  return FIG;
}

/* A single figure: pose 'standing' | 'sitting' | 'lying'. */
export function makeFigure(pose, clothes, skin = '#C68642', scale = 1) {
  const F = figureParts(), g = new THREE.Group();
  const body = new THREE.Mesh(pose === 'sitting' ? F.sitting : F.standing, std(clothes, { roughness: 0.8 }));
  const head = new THREE.Mesh(F.head, std(skin, { roughness: 0.7 }));
  body.castShadow = head.castShadow = true;
  body.userData.ownMaterial = head.userData.ownMaterial = true;
  g.add(body, head);
  if (pose === 'lying') {          // on the back, along +x
    g.rotation.z = -Math.PI / 2;
  }
  g.scale.setScalar(scale);
  return g;
}

/* ---------------- ambulance ----------------
   Lower body solid, upper body glass, so the people inside can be seen:
   driver and co-driver in the cab, a paramedic in the back and, once picked
   up, the patient on the stretcher. A warm light inside glows at night. */
let AMB = null;
function ambulanceParts() {
  if (AMB) return AMB;
  AMB = {
    lower: new RoundedBoxGeometry(1.96, 0.4, 0.94, 3, 0.1),
    roof: new RoundedBoxGeometry(1.32, 0.11, 0.96, 3, 0.05),
    cabRoof: new RoundedBoxGeometry(0.6, 0.07, 0.92, 2, 0.03),
    glassBack: new THREE.BoxGeometry(1.28, 0.4, 0.92),
    glassCab: new THREE.BoxGeometry(0.56, 0.37, 0.9),
    pillar: new THREE.BoxGeometry(0.05, 0.4, 0.95),
    stripe: new THREE.BoxGeometry(1.98, 0.09, 0.96),
    crossA: new THREE.BoxGeometry(0.52, 0.04, 0.15),
    crossB: new THREE.BoxGeometry(0.15, 0.04, 0.52),
    beacon: new THREE.BoxGeometry(0.16, 0.11, 0.24),
    beaconBase: new THREE.BoxGeometry(0.22, 0.05, 0.62),
    head: new THREE.BoxGeometry(0.05, 0.13, 0.22),
    tail: new THREE.BoxGeometry(0.04, 0.12, 0.12),
    wheel: new THREE.CylinderGeometry(0.21, 0.21, 0.17, 20),
    hub: new THREE.CylinderGeometry(0.1, 0.1, 0.18, 12),
    stretcher: new THREE.BoxGeometry(0.74, 0.05, 0.26),
    lamp: new THREE.BoxGeometry(0.5, 0.02, 0.2),
    white: std('#F7F9F9', { roughness: 0.5 }),
    glass: new THREE.MeshStandardMaterial({ color: '#A9CBE0', roughness: 0.05, metalness: 0.2, transparent: true,
      opacity: 0.26, depthWrite: false }),
    tyre: std('#1A2226', { roughness: 0.95 }),
    hub2: std('#C9D1D3', { roughness: 0.4, metalness: 0.6 }),
    dark: std('#2B363B'),
    head2: new THREE.MeshStandardMaterial({ color: '#FFFFFF', emissive: '#FFF6DD', emissiveIntensity: 1.5 }),
    tail2: new THREE.MeshStandardMaterial({ color: '#B0201A', emissive: '#FF2A1A', emissiveIntensity: 1.2 })
  };
  return AMB;
}

export function makeAmbulance(id) {
  const P = ambulanceParts(), F = figureParts(), g = new THREE.Group(), car = new THREE.Group();
  const bodyMat = std('#F4F6F6', { roughness: 0.45, metalness: 0.05 });
  const stripeMat = std('#3E5257', { roughness: 0.5 });
  const beaconA = new THREE.MeshStandardMaterial({ color: '#9AA8AB', emissive: '#000000', roughness: 0.3 });
  const beaconB = beaconA.clone();
  const lampMat = new THREE.MeshStandardMaterial({ color: '#FFF4DA', emissive: '#FFD9A0', emissiveIntensity: 0 });
  const pick = { kind: 'ambulance', id };
  const add = (geo, mat, x, y, z, rx = 0, shadow = true) => {
    const m = new THREE.Mesh(geo, mat);
    m.position.set(x, y, z);
    m.rotation.x = rx;
    m.castShadow = shadow;
    m.userData.pick = pick;
    car.add(m);
    return m;
  };
  add(P.lower, bodyMat, 0.08, 0.41, 0);
  add(P.stripe, stripeMat, 0.08, 0.5, 0);
  add(P.roof, bodyMat, -0.24, 1.05, 0);
  add(P.cabRoof, bodyMat, 0.72, 0.99, 0);
  add(P.pillar, bodyMat, 0.42, 0.8, 0);
  add(P.pillar, bodyMat, -0.88, 0.8, 0);
  add(P.crossA, P.white, -0.27, 1.12, 0);
  add(P.crossB, P.white, -0.27, 1.12, 0);
  add(P.beaconBase, P.dark, 0.72, 1.04, 0);
  add(P.beacon, beaconA, 0.72, 1.1, 0.18);
  add(P.beacon, beaconB, 0.72, 1.1, -0.18);
  // headlights take the emergency's severity colour (set every frame in city3d.js)
  const headMat = P.head2.clone();
  add(P.head, headMat, 1.07, 0.42, 0.3).userData.ownMaterial = true;
  add(P.head, headMat, 1.07, 0.42, -0.3);
  add(P.tail, P.tail2, -0.9, 0.46, 0.36);
  add(P.tail, P.tail2, -0.9, 0.46, -0.36);
  add(P.lamp, lampMat, -0.24, 0.98, 0, 0, false);
  [[0.66, 0.43], [0.66, -0.43], [-0.62, 0.43], [-0.62, -0.43]].forEach(([x, z]) => {
    add(P.wheel, P.tyre, x, 0.21, z, Math.PI / 2);
    add(P.hub, P.hub2, x, 0.21, z, Math.PI / 2);
  });

  // people inside (seen through the glass)
  const skin = () => F.skins[(id * 7 + 3) % F.skins.length];
  const driver = makeFigure('sitting', '#1E5AA8', skin(), 0.8);
  driver.position.set(0.6, 0.36, 0.2);
  const codriver = makeFigure('sitting', '#1E5AA8', F.skins[(id * 5 + 1) % F.skins.length], 0.8);
  codriver.position.set(0.6, 0.36, -0.2);
  const medic = makeFigure('sitting', '#F39C12', F.skins[(id * 3 + 2) % F.skins.length], 0.8);
  medic.position.set(-0.68, 0.36, 0.28);
  medic.rotation.y = -Math.PI / 2;           // facing the patient
  const stretcher = new THREE.Mesh(P.stretcher, P.white);
  stretcher.position.set(-0.28, 0.63, -0.12);
  const patient = makeFigure('lying', '#EAF2F8', F.skins[(id * 11) % F.skins.length], 0.78);
  patient.position.set(-0.6, 0.72, -0.12);
  car.add(driver, codriver, medic, stretcher, patient);
  // the glass goes last so the people inside show through it
  add(P.glassBack, P.glass, -0.24, 0.8, 0, 0, false).renderOrder = 4;
  add(P.glassCab, P.glass, 0.72, 0.79, 0, 0, false).renderOrder = 4;

  car.position.y = 0.08;
  car.scale.setScalar(0.95);   // real proportions next to the road and buildings

  // light on the road: a glow under the van and headlight pools in front
  const glow = glowDecal('#FFFFFF', 3.0, 0);
  glow.position.y = 0.1;
  const beams = glowDecal('#FFF2D0', 2.4, 0);
  beams.scale.set(1.2, 0.7, 1);
  beams.position.set(2.1, 0.11, 0);
  car.add(beams);

  g.add(glow, car);
  return { group: g, car, bodyMat, stripeMat, beaconA, beaconB, glow, beams, lampMat, headMat,
           people: { driver, codriver, medic, patient, stretcher } };
}

/* ---------------- hospital ---------------- */
let HOSP = null;
function hospitalParts() {
  if (HOSP) return HOSP;
  const unit = new THREE.BoxGeometry(1, 1, 1);
  unit.translate(0, 0.5, 0);
  HOSP = {
    unit,
    roof: new THREE.BoxGeometry(1, 0.12, 1),
    pad: new THREE.CircleGeometry(0.62, 40),
    crossA: new THREE.BoxGeometry(0.62, 0.62, 0.08),
    crossB: new THREE.BoxGeometry(0.2, 0.2, 0.08),
    canopy: new THREE.BoxGeometry(0.9, 0.06, 0.7),
    post: new THREE.CylinderGeometry(0.03, 0.03, 0.42, 8),
    ring: new THREE.RingGeometry(2.55, 2.8, 64),
    walls: windowMaterial('#F4F6F5', { glass: '#5B7C8F', winW: 0.42, winH: 0.5, litShare: 0.7 }),
    wing: windowMaterial('#E7ECEA', { glass: '#5B7C8F', winW: 0.42, winH: 0.5, litShare: 0.7 }),
    roofMat: std('#D3D9D6', { roughness: 0.9 }),
    padMat: new THREE.MeshStandardMaterial({ map: textures().helipad, roughness: 0.8 }),
    postMat: std('#9AA6A9', { metalness: 0.5, roughness: 0.4 })
  };
  return HOSP;
}

export function makeHospital(id) {
  const P = hospitalParts(), g = new THREE.Group();
  const pick = { kind: 'hospital', id };
  const box = (mat, sx, sy, sz, x, z, y = 0) => {
    const m = new THREE.Mesh(P.unit, mat);
    m.scale.set(sx, sy, sz);
    m.position.set(x, y, z);
    m.castShadow = m.receiveShadow = true;
    m.userData.pick = pick;
    g.add(m);
    return m;
  };
  box(P.walls, 2.5, 2.3, 1.9, 0, 0);
  box(P.wing, 1.5, 1.35, 1.6, 0.35, 1.45);
  const roof = new THREE.Mesh(P.roof, P.roofMat);
  roof.scale.set(2.56, 1, 1.96);
  roof.position.y = 2.36;
  roof.receiveShadow = true;
  g.add(roof);
  const pad = new THREE.Mesh(P.pad, P.padMat);
  pad.rotation.x = -Math.PI / 2;
  pad.position.set(-0.45, 2.43, 0);
  g.add(pad);

  // illuminated cross sign on the front wall
  const signMat = new THREE.MeshStandardMaterial({ color: '#1FA971', emissive: '#22D68A', emissiveIntensity: 0.3 });
  const sA = new THREE.Mesh(P.crossA, signMat);
  sA.scale.set(1, 0.32, 1);
  const sB = new THREE.Mesh(P.crossA, signMat);
  sB.scale.set(0.32, 1, 1);
  const sign = new THREE.Group();
  sign.add(sA, sB);
  sign.position.set(0.75, 1.72, -0.99);
  sign.scale.setScalar(1.35);
  g.add(sign);

  // entrance canopy
  const canopy = new THREE.Mesh(P.canopy, P.roofMat);
  canopy.position.set(-0.6, 0.5, -1.25);
  canopy.castShadow = true;
  g.add(canopy);
  [[-0.95, -1.5], [-0.25, -1.5]].forEach(([x, z]) => {
    const p = new THREE.Mesh(P.post, P.postMat);
    p.position.set(x, 0.24, z);
    g.add(p);
  });

  const ringMat = new THREE.MeshBasicMaterial({ color: '#2EE59D', transparent: true, opacity: 0.9, toneMapped: false });
  const ring = new THREE.Mesh(P.ring, ringMat);
  ring.rotation.x = -Math.PI / 2;
  ring.position.set(0, 0.06, 0.4);
  ring.visible = false;
  g.add(ring);

  const glow = glowDecal('#2EE59D', 7, 0);
  glow.position.set(0, 0.09, 0.4);
  glow.visible = false;
  g.add(glow);

  return { group: g, signMat, ring, glow };
}

/* ---------------- incident beacon (patient waiting) ---------------- */
let PIN = null;
function pinParts() {
  if (PIN) return PIN;
  PIN = {
    head: new THREE.SphereGeometry(0.44, 28, 18),
    tip: new THREE.ConeGeometry(0.31, 0.95, 28),
    beam: new THREE.CylinderGeometry(0.42, 0.42, 1, 32, 1, true),
    ring: new THREE.RingGeometry(0.62, 0.78, 56)
  };
  PIN.beam.translate(0, 0.5, 0);
  return PIN;
}

export function makeBeacon(type) {
  const P = pinParts(), g = new THREE.Group(), c = new THREE.Color(severityOf(type).color);
  const solid = new THREE.MeshStandardMaterial({ color: c, emissive: c, emissiveIntensity: 0.6, roughness: 0.35 });
  const pin = new THREE.Group();
  const head = new THREE.Mesh(P.head, solid);
  head.position.y = 2.2;
  const tip = new THREE.Mesh(P.tip, solid);
  tip.rotation.x = Math.PI;
  tip.position.y = 1.62;
  head.castShadow = tip.castShadow = true;
  pin.add(head, tip);

  // vertical light beam, strongest at the ground
  const beamMat = new THREE.ShaderMaterial({
    uniforms: { uColor: { value: c }, uNight: U.uNight, uTime: U.uTime },
    vertexShader: 'varying vec2 vUv; void main(){ vUv = uv; gl_Position = projectionMatrix * modelViewMatrix * vec4(position,1.0); }',
    fragmentShader: `uniform vec3 uColor; uniform float uNight; uniform float uTime; varying vec2 vUv;
      void main(){
        float a = pow(1.0 - vUv.y, 2.6) * (0.18 + 0.32 * uNight);
        a *= 0.85 + 0.15 * sin(uTime * 3.0 + vUv.y * 12.0);
        gl_FragColor = vec4(uColor * (1.4 + 1.6 * uNight), a);
      }`,
    transparent: true, depthWrite: false, blending: THREE.AdditiveBlending, side: THREE.DoubleSide, toneMapped: false
  });
  const beam = new THREE.Mesh(P.beam, beamMat);
  beam.scale.set(0.5, 9, 0.5);   // a slim marker light, not a pillar
  beam.renderOrder = 3;

  const rings = [0, 1].map(() => {
    const m = new THREE.Mesh(P.ring, new THREE.MeshBasicMaterial({
      color: c, transparent: true, opacity: 0.8, depthWrite: false, toneMapped: false, blending: THREE.AdditiveBlending
    }));
    m.rotation.x = -Math.PI / 2;
    m.position.y = 0.12;
    g.add(m);
    return m;
  });
  const glow = glowDecal(c, 5, 0.4);
  glow.position.y = 0.1;
  g.add(pin, beam, glow);

  // the patient on the ground, with two people waiting beside them
  const F = figureParts(), scene = new THREE.Group();
  const patient = makeFigure('lying', '#EAF2F8', F.skins[2], 0.75);
  patient.position.set(0.55, 0.13, 0.55);
  patient.rotation.y = 0.6;
  const helper = makeFigure('standing', F.clothes[0], F.skins[4], 0.75);
  helper.position.set(1.15, 0.1, 0.25);
  helper.rotation.y = 2.4;
  const caller = makeFigure('standing', F.clothes[3], F.skins[1], 0.75);
  caller.position.set(0.2, 0.1, 1.15);
  caller.rotation.y = -0.6;
  scene.add(patient, helper, caller);
  g.add(scene);
  return { group: g, pin, rings, glow, solid, beamMat, people: { helper, caller } };
}

/* ---------------- route (flowing light along the engine's path) ---------------- */
export function routeMaterial(color, lengthWorld) {
  const c = new THREE.Color(color);
  const m = new THREE.MeshStandardMaterial({ color: c, emissive: c, emissiveIntensity: 0.9, roughness: 0.4 });
  m.defines = { ...m.defines, USE_UV: '' };   // keep STANDARD, add the uv varying
  m.userData.uStrong = { value: 1 };
  m.userData.uLen = { value: lengthWorld };
  m.onBeforeCompile = shader => {
    shader.uniforms.uTime = U.uTime;
    shader.uniforms.uNight = U.uNight;
    shader.uniforms.uStrong = m.userData.uStrong;
    shader.uniforms.uLen = m.userData.uLen;
    shader.fragmentShader = 'uniform float uTime; uniform float uNight; uniform float uStrong; uniform float uLen;\n' +
      shader.fragmentShader.replace('#include <emissivemap_fragment>', `#include <emissivemap_fragment>
        float pos = vUv.x * uLen / 1.6 - uTime * 1.4;
        float p = fract(pos);
        float pulse = smoothstep(0.0, 0.12, p) * smoothstep(0.5, 0.12, p);
        totalEmissiveRadiance *= mix(0.3, 1.0, uStrong) * (0.6 + 0.9 * uNight);
        totalEmissiveRadiance += emissive * pulse * uStrong * (0.8 + 2.4 * uNight);`);
  };
  m.customProgramCacheKey = () => 'opera-route';
  return m;
}
