/* ==========================================================================
   OPERA - 3D city map (Three.js), the renderer

   Used by two pages:
     map3d.js    the dashboard's "See map" view
     track3d.js  the live tracking page (track.html)

   Like the rest of the frontend, this file decides nothing. Every frame it asks
   its page for a "view" (nodes, roads, hospitals, ambulances, routes) built from
   the engine's state, and draws it. Routes and road times are the C++ engine's
   Dijkstra results; the 3D scene only shows them.

   Split into three files:
     city3d.js         renderer, camera, day/night, keeping the scene in sync
     city3d-world.js   the city around the graph: platform, streets, buildings,
                       trees, lamps, park, lake (generated from the map)
     city3d-models.js  ambulance, hospital, incident beacon, route, shaders

   Coordinates: each engine node has (x, y) for drawing. The 3D world uses
   x -> X and y -> Z (the ground), with Y up.
   ========================================================================== */

import * as THREE from 'three';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';
import { CSS2DRenderer, CSS2DObject } from 'three/addons/renderers/CSS2DRenderer.js';
import { EffectComposer } from 'three/addons/postprocessing/EffectComposer.js';
import { RenderPass } from 'three/addons/postprocessing/RenderPass.js';
import { UnrealBloomPass } from 'three/addons/postprocessing/UnrealBloomPass.js';
import { OutputPass } from 'three/addons/postprocessing/OutputPass.js';
import { GTAOPass } from 'three/addons/postprocessing/GTAOPass.js';
import { RoomEnvironment } from 'three/addons/environments/RoomEnvironment.js';
import { U, SEVERITY, severityOf, makeAmbulance, makeHospital, makeBeacon, routeMaterial, std } from './city3d-models.js';
import { buildWorld, HOSP_OFFSET, PIN_OFFSET, PARK_OFFSET } from './city3d-world.js';

export { SEVERITY, severityOf };

const K = 0.1;                 // world units per map unit
const ROUTE_R = 0.17;
const ROUTE_Y = 0.44;
const RADIAL = 10;

/* Day and night settings; every value is blended by the night factor (0..1). */
const THEME = {
  day: {
    skyTop: '#2F7BD8', skyHorizon: '#A8CCEE', fog: '#B9D3EC',
    sun: '#FFF0D8', sunI: 2.1, sunPos: [-24, 40, 28],
    hemiSky: '#CFE0FA', hemiGround: '#8C7F68', hemiI: 0.85,
    env: 0.35, exposure: 0.9, bloom: 0.05, threshold: 1.0, fogNear: 170, fogFar: 460
  },
  night: {
    skyTop: '#010207', skyHorizon: '#0A1330', fog: '#060C1C',
    sun: '#7F9CFF', sunI: 0.55, sunPos: [28, 42, -18],
    hemiSky: '#23355F', hemiGround: '#05070D', hemiI: 0.42,
    env: 0.08, exposure: 1.05, bloom: 0.45, threshold: 0.86, fogNear: 110, fogFar: 320
  }
};

const esc = s => String(s == null ? '' : s).replace(/[&<>"']/g,
  c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
const lerp = (a, b, t) => a + (b - a) * t;
const ease = t => (t < 0.5 ? 4 * t * t * t : 1 - Math.pow(-2 * t + 2, 3) / 2);

function label(className, html, center = [0.5, 0.5]) {
  const el = document.createElement('div');
  el.className = 'o3d-lbl ' + className;
  el.innerHTML = html;
  const obj = new CSS2DObject(el);
  obj.center.set(center[0], center[1]);
  return obj;
}

function disposeTree(obj) {
  obj.traverse(o => {
    // CSS2D labels only leave the page when they themselves are removed: do it here
    if (o.isCSS2DObject && o.element.parentNode) o.element.parentNode.removeChild(o.element);
    if (o.isMesh || o.isPoints) {
      if (o.userData.ownGeometry) o.geometry.dispose();
      if (o.userData.ownMaterial) o.material.dispose();
    }
  });
}

function angleLerp(a, b, t) {
  let d = ((b - a + Math.PI) % (2 * Math.PI)) - Math.PI;
  if (d < -Math.PI) d += 2 * Math.PI;
  return a + d * t;
}

export class City3D {
  /**
   * @param host     element the canvas goes in (position: relative)
   * @param options  getView(): view object or null (null = nothing to draw)
   *                 onPick({kind, id}): a node / ambulance / hospital was clicked
   *                 theme: 'night' | 'day'
   */
  constructor(host, { getView, onPick = null, theme = 'night' } = {}) {
    this.host = host;
    this.getView = getView;
    this.onPick = onPick;

    const r = this.renderer = new THREE.WebGLRenderer({ antialias: false, powerPreference: 'high-performance' });
    r.setPixelRatio(Math.min(window.devicePixelRatio || 1, 1.75));
    r.shadowMap.enabled = true;
    r.shadowMap.type = THREE.PCFShadowMap;          // filtered shadows, softened by shadow.radius
    r.toneMapping = THREE.ACESFilmicToneMapping;     // filmic, photographic contrast
    r.domElement.className = 'o3d-canvas';
    host.appendChild(r.domElement);

    this.labels = new CSS2DRenderer();
    this.labels.domElement.className = 'o3d-labels';
    host.appendChild(this.labels.domElement);

    this.scene = new THREE.Scene();
    this.camera = new THREE.PerspectiveCamera(34, 16 / 9, 0.5, 900);
    this.controls = new OrbitControls(this.camera, r.domElement);
    Object.assign(this.controls, {
      enableDamping: true, dampingFactor: 0.07, maxPolarAngle: 1.36, minDistance: 2.2, maxDistance: 190,
      screenSpacePanning: false, autoRotateSpeed: 0.55
    });
    this.controls.addEventListener('start', () => {
      this.userMoved = true;
      this.followDist = null;
      if (this.chaseOn) { this.chaseOn = false; this.followOn = true; if (this.onChaseStop) this.onChaseStop(); }
      this._flight = null;
      if (this.controls.autoRotate) { this.controls.autoRotate = false; if (this.onOrbitStop) this.onOrbitStop(); }
    });

    // reflections for glass, water and paint
    const pmrem = new THREE.PMREMGenerator(r);
    this.scene.environment = pmrem.fromScene(new RoomEnvironment(), 0.04).texture;

    this._sky();
    this._lights();
    this._post();

    this.worldGroup = new THREE.Group();
    this.dynGroup = new THREE.Group();
    this.scene.add(this.worldGroup, this.dynGroup);

    this.citySig = null;
    this.city = null;              // the generated world (city3d-world.js)
    this.hospitals = new Map();
    this.ambulances = new Map();
    this.pins = new Map();
    this.routes = new Map();
    this.extent = { w: 60, d: 36 };
    this.needsFit = true;
    this.introDone = false;
    this.timer = new THREE.Timer();
    this.night = theme === 'day' ? 0 : 1;
    this.nightTarget = this.night;

    this.selectRing = new THREE.Mesh(new THREE.TorusGeometry(1.75, 0.07, 10, 64),
      new THREE.MeshBasicMaterial({ color: '#4DA3FF', toneMapped: false }));
    this.selectRing.rotation.x = Math.PI / 2;
    this.selectRing.visible = false;
    this.scene.add(this.selectRing);

    this.startLabel = label('o3d-start', 'Start', [0.5, 1.6]);
    this.startLabel.visible = false;
    this.scene.add(this.startLabel);

    this._resize = this._resize.bind(this);
    new ResizeObserver(this._resize).observe(host);
    this._resize();
    this._pointer();

    this._loop = this._loop.bind(this);
    requestAnimationFrame(this._loop);
  }

  /* ---------------- set-up ---------------- */
  _sky() {
    this.skyUniforms = { top: { value: new THREE.Color() }, horizon: { value: new THREE.Color() }, night: U.uNight };
    const sky = new THREE.Mesh(new THREE.SphereGeometry(420, 32, 16), new THREE.ShaderMaterial({
      uniforms: this.skyUniforms,
      vertexShader: 'varying vec3 vP; void main(){ vP = normalize(position); gl_Position = projectionMatrix * modelViewMatrix * vec4(position,1.0); }',
      fragmentShader: `uniform vec3 top; uniform vec3 horizon; uniform float night; varying vec3 vP;
        void main(){
          float h = clamp(vP.y, 0.0, 1.0);
          vec3 c = mix(horizon, top, pow(h, 0.55));
          c += vec3(0.015, 0.02, 0.05) * night * pow(1.0 - h, 8.0);   // faint city glow on the horizon
          gl_FragColor = vec4(c, 1.0);
        }`,
      side: THREE.BackSide, depthWrite: false, fog: false
    }));
    sky.renderOrder = -10;
    this.scene.add(sky);

    // stars, only at night
    const n = 1400, pos = new Float32Array(n * 3);
    for (let i = 0; i < n; i++) {
      const u = Math.random() * Math.PI * 2, v = Math.random() * 0.9 + 0.08, rr = 400, s = Math.sqrt(1 - v * v);
      pos[i * 3] = Math.cos(u) * s * rr;
      pos[i * 3 + 1] = v * rr;
      pos[i * 3 + 2] = Math.sin(u) * s * rr;
    }
    const g = new THREE.BufferGeometry();
    g.setAttribute('position', new THREE.BufferAttribute(pos, 3));
    this.starMat = new THREE.PointsMaterial({ color: '#DCE6FF', size: 1.6, sizeAttenuation: false, transparent: true, opacity: 1, fog: false, depthWrite: false });
    this.stars = new THREE.Points(g, this.starMat);
    this.scene.add(this.stars);

    this.scene.fog = new THREE.Fog('#081022', 110, 320);
  }

  _lights() {
    this.hemi = new THREE.HemisphereLight('#FFFFFF', '#888888', 1);
    const sun = this.sun = new THREE.DirectionalLight('#FFFFFF', 2);
    sun.castShadow = true;
    sun.shadow.mapSize.set(3072, 3072);
    sun.shadow.radius = 3;
    sun.shadow.bias = -0.0003;
    sun.shadow.normalBias = 0.03;
    this.scene.add(this.hemi, sun, sun.target);
  }

  _post() {
    const rt = new THREE.WebGLRenderTarget(1, 1, { type: THREE.HalfFloatType, samples: 4 });
    this.composer = new EffectComposer(this.renderer, rt);
    this.composer.addPass(new RenderPass(this.scene, this.camera));
    // ambient occlusion: soft contact shadows where buildings, trees and cars meet the ground
    try {
      this.ao = new GTAOPass(this.scene, this.camera, 256, 256);
      this.ao.blendIntensity = 0.9;
      this.ao.updateGtaoMaterial({ radius: 0.9, distanceExponent: 1, thickness: 1, scale: 1 });
      this.composer.addPass(this.ao);
    } catch (e) { this.ao = null; }               // older GPUs: carry on without it
    this.bloom = new UnrealBloomPass(new THREE.Vector2(256, 256), 0.7, 0.42, 0.82);
    this.composer.addPass(this.bloom);
    this.composer.addPass(new OutputPass());
  }

  _resize() {
    const w = this.host.clientWidth, h = this.host.clientHeight;
    if (!w || !h) return;
    this.renderer.setSize(w, h);
    this.composer.setSize(w, h);
    this.labels.setSize(w, h);
    this.camera.aspect = w / h;
    this.camera.updateProjectionMatrix();
    // keep the whole city framed when the view opens or changes size (until the user moves it)
    if (this.city && !this.userMoved && !this._flight && !this.followOn && !this.chaseOn) this.resetView(false);
  }

  /* map (x, y) -> world position */
  toWorld(x, y, h = 0) { return new THREE.Vector3((x - this.cx) * K, h, (y - this.cy) * K); }

  /* ---------------- public controls ---------------- */
  setTheme(theme) { this.nightTarget = theme === 'day' ? 0 : 1; }
  get theme() { return this.nightTarget > 0.5 ? 'night' : 'day'; }

  setOrbit(on) { this.controls.autoRotate = !!on; }
  get orbiting() { return this.controls.autoRotate; }

  /* Follow an ambulance; optionally move in to `distance` (cancelled if the user drags). */
  setFollow(on, distance = null) {
    this.followOn = !!on;
    this.followDist = on ? distance : null;
    if (on) this.chaseOn = false;
  }

  /* Chase view: the camera rides behind and above the tracked ambulance, looking ahead. */
  setChase(on) {
    this.chaseOn = !!on;
    if (on) { this.followOn = false; this._flight = null; }
  }

  zoom(factor) {
    const dir = this.camera.position.clone().sub(this.controls.target);
    const len = THREE.MathUtils.clamp(dir.length() * factor, this.controls.minDistance, this.controls.maxDistance);
    this._flight = { from: this.camera.position.clone(), fromT: this.controls.target.clone(),
      to: this.controls.target.clone().add(dir.setLength(len)), toT: this.controls.target.clone(), t: 0, speed: 3 };
  }

  resetView(animate = true) {
    const { pos, target } = this._defaultCamera();
    if (!animate) {
      this.camera.position.copy(pos);
      this.controls.target.copy(target);
      this.controls.update();
      return;
    }
    this._flight = { from: this.camera.position.clone(), fromT: this.controls.target.clone(), to: pos, toT: target, t: 0, speed: 1.4 };
  }

  _defaultCamera() {
    const { w, d } = this.extent;
    const vfov = THREE.MathUtils.degToRad(this.camera.fov);
    const hfov = 2 * Math.atan(Math.tan(vfov / 2) * this.camera.aspect);
    const polar = 0.9, azimuth = -0.38;
    const dist = Math.max((w / 2) / Math.tan(hfov / 2), (d / 2) / Math.tan(vfov / 2) * 1.15) * 1.02 + 4;
    const pos = new THREE.Vector3(Math.sin(polar) * Math.sin(azimuth), Math.cos(polar), Math.sin(polar) * Math.cos(azimuth))
      .multiplyScalar(dist);
    this.controls.maxDistance = Math.max(30, dist * 1.6);   // never so far that the city fades out
    this.homeDist = dist;
    return { pos, target: new THREE.Vector3(0, 0, 1.2) };
  }

  /* ---------------- building the city ---------------- */
  _buildCity(v) {
    disposeTree(this.worldGroup);
    this.worldGroup.clear();
    for (const m of [this.hospitals, this.ambulances, this.pins, this.routes]) {
      for (const e of m.values()) this._remove(e.group);
      m.clear();
    }
    const xs = v.nodes.map(n => n.x), ys = v.nodes.map(n => n.y);
    const minX = Math.min(...xs), maxX = Math.max(...xs), minY = Math.min(...ys), maxY = Math.max(...ys);
    this.cx = (minX + maxX) / 2;
    this.cy = (minY + maxY) / 2;
    this.extent = { w: (maxX - minX) * K + 7, d: (maxY - minY) * K + 7 };

    this.city = buildWorld({
      nodes: v.nodes, roads: v.roads, hospitals: v.hospitals, seed: this.citySig,
      world: (x, y, h) => this.toWorld(x, y, h)
    });
    this.worldGroup.add(this.city.group);

    const s = this.sun.shadow.camera, half = Math.max(this.city.size.w, this.city.size.d) / 2 + 3;
    Object.assign(s, { left: -half, right: half, top: half, bottom: -half, near: 1, far: 160 });
    s.updateProjectionMatrix();
    this.needsFit = true;
  }

  _remove(group) {
    if (!group) return;
    this.dynGroup.remove(group);
    disposeTree(group);
  }

  /* ---------------- hospitals ---------------- */
  _hospital(h, nodes) {
    let e = this.hospitals.get(h.id);
    if (!e) {
      e = makeHospital(h.id);
      e.lbl = label('o3d-hosp', '', [0.5, 1.1]);
      e.lbl.position.set(0, 3.0, 0);
      e.group.add(e.lbl);
      e.sig = '';
      this.dynGroup.add(e.group);
      this.hospitals.set(h.id, e);
    }
    const n = nodes[h.node];
    if (n) e.group.position.copy(this.toWorld(n.x, n.y)).add(HOSP_OFFSET);
    const sig = h.name + h.availableBeds + '/' + h.totalBeds;
    if (sig !== e.sig) {
      e.sig = sig;
      e.full = h.availableBeds <= 0;
      e.signMat.color.set(e.full ? '#C7372B' : '#1FA971');
      e.signMat.emissive.set(e.full ? '#FF3B2F' : '#22D68A');
      e.lbl.element.innerHTML = `${esc(h.name)}<span class="${e.full ? 'full' : 'ok'}">${e.full ? 'Full' : h.availableBeds + '/' + h.totalBeds + ' beds free'}</span>`;
    }
    return e;
  }

  /* ---------------- ambulances ---------------- */
  _ambulance(id) {
    let e = this.ambulances.get(id);
    if (e) return e;
    e = makeAmbulance(id);
    e.lbl = label('o3d-amb', 'A' + id, [0.5, 1.2]);
    e.lbl.position.y = 2.05;
    e.group.add(e.lbl);
    e.yaw = 0;
    e.stateKey = '';
    e.placed = false;
    this.dynGroup.add(e.group);
    this.ambulances.set(id, e);
    return e;
  }

  _styleAmbulance(e, a) {
    const key = a.state + (a.type || '');
    if (key === e.stateKey) return;
    e.stateKey = key;
    const el = e.lbl.element;
    e.color = new THREE.Color(a.state === 'call' ? severityOf(a.type).color : '#FFFFFF');
    if (a.state === 'call') {
      e.bodyMat.color.copy(e.color);
      e.stripeMat.color.set('#FFFFFF');
      e.glow.material.color.copy(e.color);
      Object.assign(el.style, { background: severityOf(a.type).color, color: '#fff', borderColor: 'transparent' });
    } else {
      const off = a.state === 'offline';
      e.bodyMat.color.set(off ? '#9AA6A9' : '#F4F6F6');
      e.bodyMat.emissive.set('#000000');
      e.stripeMat.color.set(off ? '#6B7B80' : '#3E5257');
      [e.beaconA, e.beaconB].forEach(b => { b.color.set('#8E9A9D'); b.emissive.set('#000000'); });
      e.glow.material.opacity = 0;
      e.beams.material.opacity = 0;
      e.headMat.color.set('#FFFFFF');
      e.headMat.emissive.set('#FFF6DD');
      e.headMat.emissiveIntensity = 0.6;
      Object.assign(el.style, off ? { background: '#E3E9EA', color: '#4B5F63', borderColor: '' } : { background: '', color: '', borderColor: '' });
    }
    el.textContent = 'A' + a.id + (a.state === 'offline' ? ' · offline' : '');
  }

  /* ---------------- incident beacons ---------------- */
  _pin(p, nodes) {
    let e = this.pins.get(p.id);
    if (!e) {
      e = makeBeacon(p.type);
      e.lbl = label('o3d-pin', '', [0.5, 1.2]);
      e.lbl.position.y = 3.2;
      e.lbl.element.style.setProperty('--sev', severityOf(p.type).color);
      e.group.add(e.lbl);
      e.phase = Math.random() * 6;
      e.text = '';
      this.dynGroup.add(e.group);
      this.pins.set(p.id, e);
    }
    const n = nodes[p.node];
    if (n) e.group.position.copy(this.toWorld(n.x, n.y)).add(PIN_OFFSET);
    if (e.text !== p.label) { e.text = p.label; e.lbl.element.textContent = p.label; }
    return e;
  }

  /* ---------------- routes ---------------- */
  _route(r, nodes) {
    const key = r.id + ':' + r.chain.join(',') + ':' + r.type;
    let e = this.routes.get(r.id);
    if (e && e.key !== key) { this._remove(e.group); this.routes.delete(r.id); e = null; }
    if (e) return e;
    const pts = r.chain.map(id => this.toWorld(nodes[id].x, nodes[id].y, ROUTE_Y));
    const group = new THREE.Group();
    e = { key, group, pts, cum: [0], total: 0 };
    for (let i = 1; i < pts.length; i++) e.cum.push(e.cum[i - 1] + pts[i - 1].distanceTo(pts[i]));
    e.total = e.cum[e.cum.length - 1];
    if (pts.length > 1 && e.total > 0) {
      const path = new THREE.CurvePath();
      for (let i = 1; i < pts.length; i++) path.add(new THREE.LineCurve3(pts[i - 1], pts[i]));
      e.segs = Math.max(24, Math.ceil(e.total * 12));
      const ahead = new THREE.TubeGeometry(path, e.segs, ROUTE_R, RADIAL, false);
      e.ahead = new THREE.Mesh(ahead, routeMaterial(severityOf(r.type).color, e.total));
      e.driven = new THREE.Mesh(ahead.clone(), std('#8D9A9E', { roughness: 0.6 }));
      e.ahead.userData.ownGeometry = e.ahead.userData.ownMaterial = true;
      e.driven.userData.ownGeometry = e.driven.userData.ownMaterial = true;
      const cap = new THREE.Mesh(new THREE.SphereGeometry(ROUTE_R * 1.9, 18, 12), e.ahead.material);
      cap.position.copy(pts[pts.length - 1]);
      cap.userData.ownGeometry = true;
      group.add(e.driven, e.ahead, cap);
    }
    this.dynGroup.add(group);
    this.routes.set(r.id, e);
    return e;
  }

  _fraction(e, r, nodes) {
    if (!e.total || r.done) return 1;
    const i = Math.min(r.idx, e.pts.length - 1), a = nodes[r.chain[i]];
    const along = Math.hypot(r.pos.x - a.x, r.pos.y - a.y) * K;
    return THREE.MathUtils.clamp((e.cum[i] + along) / e.total, 0, 1);
  }

  /* ---------------- theme ---------------- */
  _applyTheme(n) {
    const d = THEME.day, ni = THEME.night, c = (a, b) => new THREE.Color(a).lerp(new THREE.Color(b), n);
    this.skyUniforms.top.value.copy(c(d.skyTop, ni.skyTop));
    this.skyUniforms.horizon.value.copy(c(d.skyHorizon, ni.skyHorizon));
    this.scene.fog.color.copy(c(d.fog, ni.fog));
    this.scene.fog.near = lerp(d.fogNear, ni.fogNear, n);
    this.scene.fog.far = lerp(d.fogFar, ni.fogFar, n);
    this.sun.color.copy(c(d.sun, ni.sun));
    this.sun.intensity = lerp(d.sunI, ni.sunI, n);
    this.sun.position.set(...d.sunPos.map((v, i) => lerp(v, ni.sunPos[i], n)));
    this.hemi.color.copy(c(d.hemiSky, ni.hemiSky));
    this.hemi.groundColor.copy(c(d.hemiGround, ni.hemiGround));
    this.hemi.intensity = lerp(d.hemiI, ni.hemiI, n);
    this.scene.environmentIntensity = lerp(d.env, ni.env, n);
    this.renderer.toneMappingExposure = lerp(d.exposure, ni.exposure, n);
    this.bloom.strength = lerp(d.bloom, ni.bloom, n);
    this.bloom.threshold = lerp(d.threshold, ni.threshold, n);
    this.starMat.opacity = Math.max(0, n * 1.2 - 0.2);
    this.stars.visible = n > 0.02;
    this.host.classList.toggle('o3d-night', n > 0.5);
  }

  /* ---------------- per-frame sync with the page's view ---------------- */
  _sync(v, dt, time) {
    const nodes = v.nodes;
    const sig = nodes.map(n => n.id + ',' + n.x + ',' + n.y + n.name).join(';') + '|' +
      v.roads.map(r => r.a + '-' + r.b + ':' + r.minutes).join(';') + '|' + v.hospitals.map(h => h.id + '@' + h.node).join(',');
    if (sig !== this.citySig) { this.citySig = sig; this._buildCity(v); }
    if (this.needsFit && this.host.clientWidth) {
      this.needsFit = false;
      this.resetView(false);
      if (!this.introDone) {   // fly in from high above, once per page
        this.introDone = true;
        const end = this.camera.position.clone(), endT = this.controls.target.clone();
        const start = end.clone().applyAxisAngle(new THREE.Vector3(0, 1, 0), 1.25).multiplyScalar(1.75).setY(end.y * 2.1);
        this.camera.position.copy(start);
        this._flight = { from: start, fromT: endT.clone(), to: end, toT: endT, t: 0, speed: 0.42 };
      }
    }
    const n = this.night;
    this.city.update(time, n);

    // labels
    this.city.weightLabels.forEach(l => { l.visible = !!v.showWeights; });
    for (const [id, l] of this.city.nodeLabels) l.element.classList.toggle('dim', !!v.dimNodes && !v.dimNodes.has(id));

    // hospitals
    const seenH = new Set();
    v.hospitals.forEach(h => {
      const e = this._hospital(h, nodes);
      const target = v.highlightHospital === h.id;
      e.ring.visible = e.glow.visible = target;
      if (target) {
        e.glow.material.opacity = 0.35 + 0.25 * Math.sin(time * 2.5);
        e.ring.material.opacity = 0.7 + 0.3 * Math.sin(time * 2.5);
      }
      e.signMat.emissiveIntensity = 0.35 + n * (e.full ? 2.2 + Math.sin(time * 4) : 2.6);
      e.lbl.element.classList.toggle('dim', v.highlightHospital != null && !target);
      seenH.add(h.id);
    });
    for (const [id, e] of this.hospitals) if (!seenH.has(id)) { this._remove(e.group); this.hospitals.delete(id); }

    // routes
    const seenR = new Set();
    v.routes.forEach(r => {
      if (!r.chain || r.chain.some(id => !nodes[id])) return;
      const e = this._route(r, nodes);
      seenR.add(r.id);
      if (!e.ahead) return;
      const f = this._fraction(e, r, nodes), per = RADIAL * 6, j = Math.round(f * e.segs);
      e.ahead.geometry.setDrawRange(j * per, Infinity);
      e.driven.geometry.setDrawRange(0, j * per);
      e.driven.visible = !!r.strong;
      e.ahead.material.userData.uStrong.value = r.strong ? 1 : 0.55;
    });
    for (const [id, e] of this.routes) if (!seenR.has(id)) { this._remove(e.group); this.routes.delete(id); }

    // ambulances
    const parked = new Map();
    v.ambulances.forEach(a => { if (!a.pos) { if (!parked.has(a.node)) parked.set(a.node, []); parked.get(a.node).push(a.id); } });
    const seenA = new Set(), white = new THREE.Color('#FFFFFF');
    v.ambulances.forEach(a => {
      const e = this._ambulance(a.id);
      this._styleAmbulance(e, a);
      seenA.add(a.id);
      let target, yaw = e.yaw;
      if (a.pos) {
        target = this.toWorld(a.pos.x, a.pos.y);
        if (a.seg && nodes[a.seg[0]] && nodes[a.seg[1]]) {
          const p0 = nodes[a.seg[0]], p1 = nodes[a.seg[1]];
          yaw = -Math.atan2(p1.y - p0.y, p1.x - p0.x);
        }
      } else {
        const nd = nodes[a.node];
        if (!nd) return;
        const list = parked.get(a.node), k = list.indexOf(a.id);
        target = this.toWorld(nd.x, nd.y).add(PARK_OFFSET).add(new THREE.Vector3(0, 0, (k - (list.length - 1) / 2) * 1.3));
        yaw = 0;
      }
      if (!e.placed) { e.group.position.copy(target); e.yaw = yaw; e.placed = true; }
      else {
        e.group.position.lerp(target, a.pos ? 1 : Math.min(1, dt * 6));
        e.yaw = angleLerp(e.yaw, yaw, Math.min(1, dt * 10));
      }
      e.car.rotation.y = e.yaw;
      // who is inside: crew on a call, the patient once picked up, only a driver when parked
      const pp = e.people, onCall = a.state === 'call';
      pp.driver.visible = a.state !== 'offline';
      pp.codriver.visible = pp.medic.visible = pp.stretcher.visible = onCall;
      pp.patient.visible = onCall && (a.phase || 0) >= 1;
      e.lampMat.emissiveIntensity = onCall ? 0.4 + n * 2.6 : 0;
      if (a.state === 'call') {
        // alternating beacons, a glow on the road, headlights at night
        const flash = a.moving ? Math.sin(time * 15) : -1;
        [[e.beaconA, flash > 0], [e.beaconB, flash < 0 && a.moving]].forEach(([b, on]) => {
          b.color.copy(on ? white : e.color);
          b.emissive.copy(on ? white : e.color);
          b.emissiveIntensity = on ? 6 : 0.6 + n;
        });
        e.glow.material.opacity = (0.25 + 0.75 * n) * (a.moving ? 0.85 + 0.15 * Math.sin(time * 15) : 0.55);
        // headlights and their light on the road show severity; brighter with higher priority
        const pf = THREE.MathUtils.clamp((a.priority || 50) / 100, 0.35, 1.6);
        const sevPulse = a.type === 'Cardiac' ? 0.8 + 0.2 * Math.sin(time * 9) : 1;
        e.headMat.color.copy(e.color);
        e.headMat.emissive.copy(e.color);
        e.headMat.emissiveIntensity = (1.6 + 3.4 * n) * pf * sevPulse;
        e.beams.material.color.copy(e.color);
        e.beams.material.opacity = (a.moving ? 0.45 + 0.5 * n : 0.25) * Math.min(1.2, pf) * sevPulse;
        const ptxt = 'A' + a.id + ' \u00B7 P' + Math.round(a.priority || 0);
        if (e.lbl.element.textContent !== ptxt) e.lbl.element.textContent = ptxt;
        e.bodyMat.emissive.copy(e.color).multiplyScalar(0.12 * n);
      }
    });
    for (const [id, e] of this.ambulances) if (!seenA.has(id)) { this._remove(e.group); this.ambulances.delete(id); }

    // incident beacons
    const seenP = new Set();
    v.pins.forEach(p => {
      const e = this._pin(p, nodes);
      seenP.add(p.id);
      e.pin.position.y = Math.sin(time * 2.2 + e.phase) * 0.12;
      e.pin.rotation.y = time * 1.2;
      e.rings.forEach((ring, k) => {
        const s = (time * 0.7 + e.phase + k * 0.5) % 1;
        ring.scale.setScalar(1 + s * 3.4);
        ring.material.opacity = 0.85 * (1 - s);
      });
      e.glow.material.opacity = 0.35 + 0.35 * n + 0.15 * Math.sin(time * 4 + e.phase);
      e.solid.emissiveIntensity = 0.5 + 1.8 * n;
      e.people.caller.rotation.z = 0.08 * Math.sin(time * 6 + e.phase);     // waving for help
      e.people.helper.position.y = 0.1 + 0.02 * Math.abs(Math.sin(time * 3));
    });
    for (const [id, e] of this.pins) if (!seenP.has(id)) { this._remove(e.group); this.pins.delete(id); }

    // selected location ring and route start label
    const sel = v.selectedNode != null ? nodes[v.selectedNode] : null;
    this.selectRing.visible = !!sel;
    if (sel) {
      this.selectRing.position.copy(this.toWorld(sel.x, sel.y, 0.3));
      this.selectRing.scale.setScalar(1 + 0.06 * Math.sin(time * 3));
    }
    const st = v.startNode != null ? nodes[v.startNode] : null;
    this.startLabel.visible = !!st;
    if (st) this.startLabel.position.copy(this.toWorld(st.x, st.y, 0.4));

    // x-ray cutaway: keep the tracked ambulance visible through buildings
    const amb = v.followId != null ? this.ambulances.get(v.followId) : null;
    U.uCut.value = amb && (this.followOn || this.chaseOn) ? 1 : 0;
    if (amb) U.uFocus.value.copy(amb.group.position).setY(0.7);
    U.uCam.value.copy(this.camera.position);

    // chase view: behind the ambulance, slightly above, looking where it drives
    if (this.chaseOn && amb) {
      const yaw = amb.yaw, fwd = new THREE.Vector3(Math.cos(yaw), 0, -Math.sin(yaw));
      const p = amb.group.position;
      const want = p.clone().addScaledVector(fwd, -6.5).add(new THREE.Vector3(0, 3.6, 0));
      const look = p.clone().addScaledVector(fwd, 3).setY(0.6);
      const k = Math.min(1, dt * 2.2);
      this.camera.position.lerp(want, k);
      this.controls.target.lerp(look, k);
    }

    // follow the tracked ambulance from above, keeping the viewing angle
    if (this.followOn && v.follow && !this._flight) {
      const p = this.toWorld(v.follow.x, v.follow.y);
      const delta = p.sub(this.controls.target).multiplyScalar(Math.min(1, dt * 3));
      this.controls.target.add(delta);
      this.camera.position.add(delta);
      if (this.followDist) {
        const dir = this.camera.position.clone().sub(this.controls.target), len = dir.length();
        if (Math.abs(len - this.followDist) < 0.3) this.followDist = null;
        else this.camera.position.copy(this.controls.target).add(dir.setLength(len + (this.followDist - len) * Math.min(1, dt * 1.6)));
      }
    }
  }

  /* ---------------- clicking and hovering ---------------- */
  _pickAt(ev) {
    const el = this.renderer.domElement, rc = el.getBoundingClientRect();
    const ndc = new THREE.Vector2(((ev.clientX - rc.left) / rc.width) * 2 - 1, -((ev.clientY - rc.top) / rc.height) * 2 + 1);
    this.ray = this.ray || new THREE.Raycaster();
    this.ray.setFromCamera(ndc, this.camera);
    const targets = (this.city ? this.city.nodePicks : []).concat([this.dynGroup]);
    for (const h of this.ray.intersectObjects(targets, true)) {
      for (let o = h.object; o; o = o.parent) if (o.userData && o.userData.pick) return o.userData.pick;
    }
    return null;
  }

  _pointer() {
    const el = this.renderer.domElement;
    let down = null, lastHover = 0;
    el.addEventListener('pointerdown', ev => { down = { x: ev.clientX, y: ev.clientY }; });
    el.addEventListener('pointerup', ev => {
      if (!down) return;
      const moved = Math.abs(ev.clientX - down.x) + Math.abs(ev.clientY - down.y) > 5;
      down = null;
      if (moved || !this.onPick) return;
      const p = this._pickAt(ev);
      if (p) this.onPick(p);
    });
    el.addEventListener('pointermove', ev => {
      if (down || !this.onPick) return;
      const now = performance.now();
      if (now - lastHover < 70) return;
      lastHover = now;
      el.style.cursor = this._pickAt(ev) ? 'pointer' : '';
    });
  }

  /* ---------------- render loop ---------------- */
  _loop(timestamp) {
    requestAnimationFrame(this._loop);
    this.timer.update(timestamp);
    const dt = Math.min(this.timer.getDelta(), 0.1);
    if (!this.host.isConnected || this.host.offsetParent === null) { this._hidden = true; return; }   // hidden: no work
    if (this._hidden) { this._hidden = false; this._resize(); }
    let v = null;
    try { v = this.getView(); } catch (err) { v = null; }
    if (!v || !v.nodes || !v.nodes.length) return;
    const time = this.timer.getElapsed();
    U.uTime.value = time;

    // smooth day <-> night change
    const gap = this.nightTarget - this.night;
    this.night = Math.abs(gap) < 0.001 ? this.nightTarget : this.night + Math.sign(gap) * Math.min(Math.abs(gap), dt * 1.2);
    U.uNight.value = this.night;
    this._applyTheme(this.night);

    this._sync(v, dt, time);
    if (this._flight) {
      const f = this._flight;
      f.t = Math.min(1, f.t + dt * f.speed);
      const k = ease(f.t);
      this.camera.position.lerpVectors(f.from, f.to, k);
      this.controls.target.lerpVectors(f.fromT, f.toT, k);
      if (f.t >= 1) this._flight = null;
    }
    // fog adapts to the zoom, and panning cannot drag the city out of view
    const camDist = this.camera.position.distanceTo(this.controls.target);
    this.scene.fog.near = Math.max(this.scene.fog.near, camDist * 1.15);
    this.scene.fog.far = Math.max(this.scene.fog.far, camDist * 2.8);
    if (this.city) {
      const hx = this.city.size.w / 2, hz = this.city.size.d / 2, t = this.controls.target;
      const cx = THREE.MathUtils.clamp(t.x, -hx, hx) - t.x, cz = THREE.MathUtils.clamp(t.z, -hz, hz) - t.z;
      if (cx || cz) { t.x += cx; t.z += cz; this.camera.position.x += cx; this.camera.position.z += cz; }
    }
    this.controls.update();
    this.composer.render();
    this.labels.render(this.scene, this.camera);
  }
}
