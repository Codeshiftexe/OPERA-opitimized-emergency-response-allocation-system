/* ==========================================================================
   OPERA - 3D version of the live tracking map (track.html)

   Loaded by track.html after track.js. It does not change track.js: it reads
   the same state (S, emId, tripOf ... declared in track.js), so the 3D
   ambulance is always exactly where the progress bar and ETA say it is.

   The ambulance, its route and the patient pin are coloured by the
   emergency's severity (see SEVERITY in city3d.js).
   The "3D view" switch above the map returns to the 2D SVG map.
   ========================================================================== */

import { City3D, SEVERITY, severityOf } from './city3d.js';

const VIEW_KEY = 'opera.view3d', THEME_KEY = 'opera.theme3d';
const svg = document.getElementById('tmap');
const card = svg ? svg.closest('.card') : null;

function readPref() { try { return sessionStorage.getItem(VIEW_KEY) !== '0'; } catch (e) { return true; } }
function savePref(on) { try { sessionStorage.setItem(VIEW_KEY, on ? '1' : '0'); } catch (e) { /* blocked */ } }
function readTheme() { try { return sessionStorage.getItem(THEME_KEY) === 'day' ? 'day' : 'night'; } catch (e) { return 'night'; } }
function saveTheme(t) { try { sessionStorage.setItem(THEME_KEY, t); } catch (e) { /* blocked */ } }

let following = true;

/* Build the view for the renderer from track.js's current state. */
function view() {
  if (typeof S === 'undefined' || !S) return null;
  const e = emOf(emId);
  if (!e || e.status !== 'assigned') return null;
  const t = tripOf(e);
  return {
    nodes: S.nodes, roads: S.roads, hospitals: S.hospitals,
    ambulances: [{
      id: e.ambulance, state: 'call', type: e.type, pos: t.pos, moving: !t.done, phase: t.phase, priority: e.priority,
      seg: t.done ? null : [t.chain[t.idx], t.chain[t.idx + 1]]
    }],
    pins: t.phase === 0 ? [{ id: e.id, node: e.node, type: e.type, label: `#${e.id} ${e.type} · patient` }] : [],
    routes: [{ id: e.id, chain: t.chain, idx: t.idx, pos: t.pos, done: t.done, type: e.type, strong: true }],
    showWeights: true,
    highlightHospital: e.hospital,
    dimNodes: new Set(t.chain),
    startNode: t.chain[0],
    follow: t.pos,
    followId: e.ambulance
  };
}

function legendHtml() {
  return `<span><i class="sw" style="background:${SEVERITY.Cardiac.color}"></i>Cardiac · ${SEVERITY.Cardiac.label}</span>
    <span><i class="sw" style="background:${SEVERITY.Trauma.color}"></i>Trauma · ${SEVERITY.Trauma.label}</span>
    <span><i class="sw" style="background:${SEVERITY.Transfer.color}"></i>Transfer · ${SEVERITY.Transfer.label}</span>
    <span><i class="ln" style="background:#8D9A9E"></i>Route already driven</span>
    <span><i class="sw sq" style="background:#2EE59D"></i>Destination hospital (glowing ring)</span>
    <span class="hint">The ambulance, its glowing route ahead and the patient's light beam use the emergency's severity colour.
      Drag to rotate, scroll to zoom, right-drag to pan. ☾ switches day / night.</span>`;
}

function button(icon, text, title) {
  const b = document.createElement('button');
  b.className = 'b';
  b.title = title;
  b.setAttribute('aria-label', title);
  b.innerHTML = `<i class="ti ti-${icon}" aria-hidden="true"></i>${text}`;
  return b;
}

function setup() {
  if (!svg || !card) return;

  // header row above the map: title, severity chip and the 3D switch
  const bar = document.createElement('div');
  bar.className = 'o3d-bar';
  bar.innerHTML = '<span class="o3d-title">City map</span><span class="chip o3d-sev" id="o3dSev"></span>';
  const toggle = button('box', '3D view', 'Switch between the 3D and 2D map');
  toggle.classList.add('sm', 'tw');
  bar.appendChild(toggle);
  card.prepend(bar);

  const host = document.createElement('div');
  host.className = 'o3d-host';
  svg.before(host);

  let city;
  try {
    city = new City3D(host, { getView: view, theme: readTheme() });
  } catch (err) {
    host.remove();
    bar.remove();
    console.warn('OPERA 3D map not available:', err);
    return;
  }
  const CLOSE = 15;                 // follow view: high enough to see the streets around it
  city.setFollow(following, CLOSE);

  // camera tools over the map
  const tools = document.createElement('div');
  tools.className = 'zoom o3d-tools';
  const zin = button('plus', '', 'Zoom in'), zout = button('minus', '', 'Zoom out');
  const reset = button('arrows-maximize', '', 'Show the whole city');
  const follow = button('focus-2', '', 'Follow the ambulance');
  follow.classList.add('tw', 'on');
  zin.onclick = () => city.zoom(0.75);
  zout.onclick = () => city.zoom(1.33);
  reset.onclick = () => { following = false; city.setFollow(false); city.setChase(false); follow.classList.remove('on'); if (window.__chaseBtn) window.__chaseBtn.classList.remove('on'); city.resetView(); };
  follow.onclick = () => { following = !following; city.setFollow(following, CLOSE); follow.classList.toggle('on', following); };
  const themeBtn = button('moon', '', 'Switch day / night');
  const showTheme = () => { themeBtn.innerHTML = `<i class="ti ti-${city.theme === 'night' ? 'sun' : 'moon'}" aria-hidden="true"></i>`; };
  themeBtn.onclick = () => { city.setTheme(city.theme === 'night' ? 'day' : 'night'); saveTheme(city.theme); showTheme(); };
  showTheme();
  const chase = button('video', '', 'Chase view: ride behind the ambulance');
  chase.classList.add('tw');
  window.__chaseBtn = chase;
  chase.onclick = () => {
    const on = !chase.classList.contains('on');
    chase.classList.toggle('on', on);
    follow.classList.toggle('on', !on);
    following = !on;
    if (on) city.setChase(true); else city.setFollow(true, CLOSE);
  };
  city.onChaseStop = () => { chase.classList.remove('on'); follow.classList.add('on'); following = true; };
  follow.addEventListener('click', () => { chase.classList.remove('on'); city.setChase(false); });
  tools.append(zin, zout, reset, follow, chase, themeBtn);
  host.appendChild(tools);

  // our legend replaces the 2D one while 3D is on
  const oldLegend = card.querySelector('.leg');
  if (oldLegend) oldLegend.classList.add('o3d-old');
  const legend = document.createElement('div');
  legend.className = 'leg o3d-legend';
  legend.innerHTML = legendHtml();
  (oldLegend || host).after(legend);

  // the 2D map is not drawn while 3D is on (saves work every frame)
  const draw2d = window.drawMap;
  let on = readPref();
  if (typeof draw2d === 'function') window.drawMap = (e, t) => { if (!on) draw2d(e, t); };

  const apply = () => {
    card.classList.toggle('o3d-mode', on);
    toggle.classList.toggle('on', on);
    toggle.setAttribute('aria-pressed', on);
  };
  toggle.onclick = () => { on = !on; savePref(on); apply(); };
  apply();

  // severity chip in the header follows the tracked emergency
  const chip = document.getElementById('o3dSev');
  setInterval(() => {
    if (typeof S === 'undefined' || !S) return;
    const e = emOf(emId);
    if (!e) { chip.hidden = true; return; }
    const s = severityOf(e.type);
    chip.hidden = false;
    chip.textContent = `#${e.id} ${e.type} · ${s.label}`;
    chip.style.background = s.color;
    chip.style.color = '#fff';
  }, 500);
}

setup();
