/* ==========================================================================
   OPERA - 3D version of the dashboard's "See map" view

   Loaded by opera.html after opera.js. It does not change opera.js: it reads
   the same state the 2D map uses (S, focus, showW, tripOf ... declared in
   opera.js) and gives it to the 3D renderer in city3d.js.

   The 2D SVG map is still there. The "3D view" switch in the map toolbar turns
   the 3D map on and off; the choice is remembered for this browser tab.
   If WebGL is not available, the page simply keeps the 2D map.
   ========================================================================== */

import { City3D, SEVERITY } from './city3d.js';

const VIEW_KEY = 'opera.view3d', THEME_KEY = 'opera.theme3d';
const el = id => document.getElementById(id);

const stage = document.querySelector('#mapview .stage');
const card = stage ? stage.closest('.card') : null;

function readPref() { try { return sessionStorage.getItem(VIEW_KEY) !== '0'; } catch (e) { return true; } }
function savePref(on) { try { sessionStorage.setItem(VIEW_KEY, on ? '1' : '0'); } catch (e) { /* blocked */ } }
function readTheme() { try { return sessionStorage.getItem(THEME_KEY) === 'day' ? 'day' : 'night'; } catch (e) { return 'night'; } }
function saveTheme(t) { try { sessionStorage.setItem(THEME_KEY, t); } catch (e) { /* blocked */ } }

/* Build the view for the renderer from opera.js's current state. */
function view() {
  if (typeof S === 'undefined' || !S || page !== 'map') return null;
  const trips = new Map();
  S.emergencies.filter(e => e.status === 'assigned').forEach(e => trips.set(e.id, tripOf(e)));

  const ambulances = S.ambulances.map(a => {
    const e = a.emergency !== null ? emOf(a.emergency) : null;
    const t = e && trips.get(e.id);
    if (t && a.status === 'busy') {
      return { id: a.id, state: 'call', type: e.type, pos: t.pos, moving: !t.done, phase: t.phase, priority: e.priority,
               seg: t.done ? null : [t.chain[t.idx], t.chain[t.idx + 1]] };
    }
    return { id: a.id, node: a.node, state: a.status === 'offline' ? 'offline' : 'idle' };
  });

  const pins = S.emergencies
    .filter(e => e.status === 'pending' || (trips.has(e.id) && trips.get(e.id).phase === 0))
    .map(e => ({ id: e.id, node: e.node, type: e.type,
                 label: `#${e.id} ${e.type}${e.status === 'pending' ? ' · waiting' : ''}` }));

  const routes = [...trips.entries()].map(([id, t]) => {
    const e = emOf(id);
    return { id, chain: t.chain, idx: t.idx, pos: t.pos, done: t.done, type: e.type, strong: focus === id };
  });

  const loc = el('loc');
  return {
    nodes: S.nodes, roads: S.roads, hospitals: S.hospitals,
    ambulances, pins, routes,
    showWeights: showW,
    selectedNode: loc && loc.value !== '' ? +loc.value : null
  };
}

function legendHtml() {
  const sev = Object.entries(SEVERITY).map(([type, s]) =>
    `<span><i class="sw" style="background:${s.color}"></i>${type} · ${s.label}</span>`).join('');
  return `${sev}
    <span><i class="sw" style="background:#F4F6F6;border:1px solid #9DAEB1"></i>Ambulance available</span>
    <span><i class="sw" style="background:#9AA6A9"></i>Offline</span>
    <span><i class="sw sq" style="background:#1FA971"></i>Hospital sign (red = full)</span>
    <span><i class="ln" style="background:#8D9A9E"></i>Route already driven</span>
    <span class="hint">An ambulance on a call, its glowing route and the patient's light beam take the emergency's colour.
      Select an emergency in the queue to highlight its route. Drag to rotate, scroll to zoom, right-drag to pan.
      Click a junction, hospital or ambulance for its page. ☾ switches day / night, ⟳ starts a slow cinematic orbit.</span>`;
}

function setup() {
  if (!stage || !card) return;

  const host = document.createElement('div');
  host.className = 'o3d-host';
  stage.prepend(host);

  let city;
  try {
    city = new City3D(host, {
      getView: view,
      theme: readTheme(),
      onPick: p => {
        if (p.kind === 'node') { nodeId = p.id; go('node'); }
        else if (p.kind === 'ambulance') go('amb');
        else if (p.kind === 'hospital') go('hosp');
      }
    });
  } catch (err) {
    host.remove();                    // no WebGL: keep the 2D map
    console.warn('OPERA 3D map not available:', err);
    return;
  }

  // our legend replaces the 2D one while 3D is on
  const oldLegend = card.querySelector('.leg');
  if (oldLegend) oldLegend.classList.add('o3d-old');
  const legend = document.createElement('div');
  legend.className = 'leg o3d-legend';
  legend.innerHTML = legendHtml();
  (oldLegend || stage).after(legend);

  // "3D view" switch next to the Weights button
  const toolbar = card.querySelector('.mh .tb');
  const toggle = document.createElement('button');
  toggle.className = 'b sm tw';
  toggle.innerHTML = '<i class="ti ti-box" aria-hidden="true"></i>3D view';
  if (toolbar) toolbar.prepend(toggle);

  // zoom buttons drive the 3D camera while 3D is on, the SVG otherwise
  const orig = { zin: el('zin').onclick, zout: el('zout').onclick, zres: el('zres').onclick };
  let on = readPref();
  const apply = () => {
    card.classList.toggle('o3d-mode', on);
    toggle.classList.toggle('on', on);
    toggle.setAttribute('aria-pressed', on);
    if (!on && typeof renderMap === 'function' && page === 'map') renderMap();
  };
  toggle.onclick = () => { on = !on; savePref(on); apply(); };
  el('zin').onclick = () => (on ? city.zoom(0.75) : orig.zin());
  el('zout').onclick = () => (on ? city.zoom(1.33) : orig.zout());
  el('zres').onclick = () => (on ? city.resetView() : orig.zres());

  // 3D-only buttons under the zoom buttons: day / night and cinematic orbit
  const zoomBox = el('zin').parentElement;
  const mk = (cls, title) => {
    const b = document.createElement('button');
    b.className = 'b o3d-only';
    b.title = title;
    b.setAttribute('aria-label', title);
    b.innerHTML = `<i class="ti ti-${cls}" aria-hidden="true"></i>`;
    zoomBox.appendChild(b);
    return b;
  };
  const themeBtn = mk('moon', 'Switch day / night');
  const orbitBtn = mk('rotate-360', 'Cinematic orbit');
  const showTheme = () => { themeBtn.innerHTML = `<i class="ti ti-${city.theme === 'night' ? 'sun' : 'moon'}" aria-hidden="true"></i>`; };
  themeBtn.onclick = () => { city.setTheme(city.theme === 'night' ? 'day' : 'night'); saveTheme(city.theme); showTheme(); };
  orbitBtn.onclick = () => { city.setOrbit(!city.orbiting); orbitBtn.classList.toggle('on', city.orbiting); };
  city.onOrbitStop = () => orbitBtn.classList.remove('on');
  showTheme();
  apply();
}

setup();
