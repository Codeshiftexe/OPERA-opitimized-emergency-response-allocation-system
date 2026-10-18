'use strict';
/* ==========================================================================
   OPERA web interface

   The page decides nothing. Every action is a request to the C++ server,
   the engine does the work, and every reply carries the complete new state,
   which the page draws from scratch (API routes: see include/Api.hpp).

   Finding the server: the page tries, in order, the address it was loaded
   from and http://localhost:8080. So it works both when opened from
   opera_server itself and when opened with VS Code Live Server (as long as
   opera_server is running).

   Sign-in: the name and password are checked by the server against
   data/passwords.txt. The server answers with a session token, which
   is sent with every request ("Authorization: Bearer ...") and kept in
   sessionStorage so a page reload stays signed in.
   ========================================================================== */

let API = null;          // server address, found by findServer()
let TOKEN = null;        // session token from /api/login
let me = null;           // {name, role} of the signed-in user
let S = null;            // last state received from the server
let focus = null;        // emergency selected in the queue (a view choice only)
let page = null;         // null = dashboard | 'map' | 'amb' | 'hosp' | 'node'  (tracking is track.html)
let nodeId = null;       // node shown on the node page
let flt = 'all';         // filter on the ambulance / hospital pages
let showW = true;        // road weights on the map
let inFlight = false;    // a request is running: ignore repeated clicks
let flashT = null;
let base = null;         // map bounds from the node positions
let vb = {z: 1, cx: 0, cy: 0};

const $ = id => document.getElementById(id);
const esc = s => String(s == null ? '' : s).replace(/[&<>"']/g,
  c => ({'&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;'}[c]));
const kv = (k, v) => `<div class="kv"><span>${k}</span><b>${v}</b></div>`;
const r1 = x => Math.round(x * 10) / 10;
const TYPE_COLOR = {Cardiac: '#C62F1A', Trauma: '#B5400F', Transfer: '#0F766E'};
const AST = {
  available: ['Available', '#1B7143', '#E1F2E8'],
  busy:      ['On a call', '#0B5CAD', '#E3EEF9'],
  offline:   ['Offline',   '#4B5F63', '#E3E9EA']
};

/* ---- lookups into the current state ---- */
const nodeOf = id => S.nodes[id];                       // ids are 0..n-1, in order
const nm = id => nodeOf(id) ? nodeOf(id).name : 'node ' + id;
const hospOf = id => S.hospitals.find(h => h.id === id);
const ambOf = id => S.ambulances.find(a => a.id === id);
const emOf = id => S.emergencies.find(e => e.id === id);
const hospName = id => (hospOf(id) || {name: 'Hospital ' + id}).name;
const ambName = id => 'Ambulance ' + id;
const clockText = m => 'T+' + m + ' min';
const icon = (name, bg) => `<span class="ico" style="background:${bg}"><i class="ti ti-${name}" aria-hidden="true"></i></span>`;

/* ==========================================================================
   SERVER CONNECTION - the only place the page talks to the engine
   ========================================================================== */
async function request(method, path, body) {
  const headers = {};
  if (TOKEN) headers['Authorization'] = 'Bearer ' + TOKEN;
  if (method !== 'GET') headers['Content-Type'] = 'application/json';
  const res = await fetch(API + path, {method, headers, cache: 'no-store',
    body: method === 'GET' ? undefined : JSON.stringify(body || {})});
  let data = null;
  try { data = await res.json(); } catch (e) { data = null; }
  return {res, data};
}

async function call(method, path, body) {
  if (inFlight) return null;
  if (!API) { setConn(false); say('Cannot reach the OPERA server. Is opera_server still running?', true); return null; }
  inFlight = true;
  document.body.classList.add('waiting');
  try {
    let r;
    try { r = await request(method, path, body); }
    catch (e) {
      setConn(false);
      say('Cannot reach the OPERA server. Is opera_server still running?', true);
      return null;
    }
    if (!r.data) { setConn(false); say('The server sent an unexpected reply (HTTP ' + r.res.status + ').', true); return null; }
    setConn(true);
    if (r.res.status === 401) {
      endSession('Your session has ended (for example, the server was restarted). Please sign in again.');
      return null;
    }
    const data = r.data;
    if (data.state) { S = data.state; renderAll(); }
    if (data.message) say(data.message, !data.ok);
    return data;
  } finally {
    inFlight = false;
    document.body.classList.remove('waiting');
  }
}

function setConn(ok) {
  const c = $('conn');
  c.textContent = ok ? 'Engine connected' : 'Server not reachable';
  c.className = 'chip ' + (ok ? 'ok' : 'al');
}

function say(text, bad) {
  const f = $('flash');
  clearTimeout(flashT);
  if (!text) { f.hidden = true; return; }
  f.textContent = text;
  f.className = 'flash' + (bad ? ' bad' : '');
  f.hidden = false;
  flashT = setTimeout(() => { f.hidden = true; }, 9000);
}

/* ==========================================================================
   ACTIONS - one function per API route
   ========================================================================== */
async function registerEmergency() {
  const d = await call('POST', '/api/emergencies', {type: $('type').value, node: +$('loc').value});
  if (d && d.ok) {
    focus = Math.max(...S.emergencies.map(e => e.id));  // the one just registered
    renderAll();
  }
}
async function dispatchNext() {
  const d = await call('POST', '/api/dispatch');
  if (d && d.result && d.result.emergency !== null) {
    focus = d.result.emergency;
    if (d.ok) {
      trips[focus] = Date.now();   // the trip animation starts now
      saveTrips();
      // Undo is offered for 10 seconds, on the tracking page (track.js).
      ssSet('opera.undo', JSON.stringify({id: focus, until: Date.now() + UNDO_MS}));
      arrivedSaid.delete(focus);
      openTrack(focus);            // load the tracking page (track.html)
    } else {
      renderAll();
    }
  }
}
const complete = id => call('POST', '/api/emergencies/complete', {id});
const discharge = id => call('POST', '/api/hospitals/discharge', {id});
const setOnline = (id, online) => call('POST', '/api/ambulances/status', {id, online});

/* ==========================================================================
   DASHBOARD
   ========================================================================== */
function buildForm() {
  const ty = $('type');
  if (!ty.options.length) {
    ty.innerHTML = S.emergencyTypes.map(t => `<option value="${esc(t.name)}">${esc(t.name)}</option>`).join('');
  }
  const loc = $('loc');
  if (loc.options.length !== S.nodes.length) {
    const keep = loc.value;
    loc.innerHTML = S.nodes.map(n => `<option value="${n.id}">${esc(n.name)} (node ${n.id})</option>`).join('');
    if (keep !== '' && S.nodes[+keep]) loc.value = keep;
  }
  updTypeInfo();
}

function updTypeInfo() {
  if (!S) return;
  const t = S.emergencyTypes.find(x => x.name === $('type').value);
  if (!t) return;
  $('type').style.borderLeftColor = TYPE_COLOR[t.name] || '#9DAEB1';
  $('typeInfo').innerHTML = `<b style="font-weight:500">${esc(t.name)}: base priority ${t.basePriority}, response target ${t.targetMinutes} min</b>`
    + `<span style="color:#4B5F63">+${S.agingPerMinute} priority per minute waiting</span>`;
}

function renderQueue() {
  const q = $('find').value.toLowerCase().trim();
  const pending = S.queue.map(emOf).filter(Boolean);
  const assigned = S.emergencies.filter(e => e.status === 'assigned');
  const items = pending.concat(assigned).filter(e => !q ||
    ('#' + e.id + ' ' + e.type + ' ' + e.status + ' ' + nm(e.node)).toLowerCase().includes(q));
  if (!items.length) {
    $('queue').innerHTML = `<div class="msg">${S.emergencies.length && !q ? 'Nothing waiting or in progress.' : 'No emergencies match. Register one above.'}</div>`;
    return;
  }
  $('queue').innerHTML = items.map(e => {
    const pos = S.queue.indexOf(e.id);
    const line = e.status === 'pending'
      ? `${pos === 0 ? 'Next to dispatch · ' : 'Position ' + (pos + 1) + ' · '}waiting ${e.waitingMinutes} min · target ${e.targetMinutes} min`
      : `${ambName(e.ambulance)} → ${esc(hospName(e.hospital))} · ${r1(e.minutesToScene)} min to scene`;
    const cls = e.status === 'assigned' ? '' : e.priority >= 100 ? 'c' : e.priority >= 50 ? 'w' : '';
    return `<div class="q ${cls} ${e.id === focus ? 'f' : ''}" data-f="${e.id}" tabindex="0">
      <div><b style="font-weight:500">#${e.id} ${esc(e.type)} · ${esc(nm(e.node))}</b><em>${line}</em></div>
      <span class="tag" style="color:${e.status === 'assigned' ? '#0B5CAD' : TYPE_COLOR[e.type] || '#4B5F63'}">${e.status === 'assigned' ? 'Assigned' : 'Priority ' + r1(e.priority)}</span></div>`;
  }).join('');
}

function renderResponse() {
  const e = focus !== null ? emOf(focus) : null;
  $('path').innerHTML = '';
  $('respAct').innerHTML = '';
  if (!e) {
    $('rh').textContent = 'Response';
    $('facts').innerHTML = '<div class="msg">Select an emergency in the queue, or dispatch one, to see its plan.</div>';
    return;
  }
  $('rh').textContent = 'Response for #' + e.id + ' · ' + e.type;
  if (e.status === 'pending') {
    const pos = S.queue.indexOf(e.id);
    $('facts').innerHTML = `<div><small>Status</small><strong>Waiting</strong></div>
      <div><small>Queue position</small><strong>${pos + 1} of ${S.queue.length}</strong></div>
      <div><small>Priority now</small><strong>${r1(e.priority)}</strong></div>
      <div><small>Waiting</small><strong>${e.waitingMinutes} min</strong></div>
      <div><small>Target</small><strong>${e.targetMinutes} min</strong></div>`;
    return;
  }
  if (e.status === 'resolved') {
    $('facts').innerHTML = `<div class="msg">#${e.id} has been delivered to hospital. Its ambulance is available again.</div>`;
    return;
  }
  const h = hospOf(e.hospital);
  $('facts').innerHTML = `<div><small>Ambulance</small><strong>${ambName(e.ambulance)}</strong></div>
    <div><small>Hospital</small><strong>${esc(h ? h.name : e.hospital)}</strong></div>
    <div><small>To scene</small><strong>${r1(e.minutesToScene)} min</strong></div>
    <div><small>To hospital</small><strong>${r1(e.minutesToHospital)} min</strong></div>
    <div><small>Beds left there</small><strong>${h ? h.availableBeds + ' of ' + h.totalBeds : '?'}</strong></div>`;
  const chain = (ids, cls) => ids.map(n => `<span class="${cls}" title="node ${n}">${esc(nm(n))}</span>`).join('→');
  $('path').innerHTML = chain(e.routeToScene, '') + '→' + chain(e.routeToHospital.slice(1), 'pd');
  $('respAct').innerHTML = `<button class="b" data-track="${e.id}" style="background:#0B5CAD;border-color:#0B5CAD;color:#fff"><i class="ti ti-route" aria-hidden="true"></i>Track live</button>
    <button class="b" data-done="${e.id}"><i class="ti ti-check" aria-hidden="true"></i>Patient delivered</button>
    <button class="b" data-showmap><i class="ti ti-map-2" aria-hidden="true"></i>Show on map</button>`;
}

// Activity log: a list that keeps scrolling upwards, newest entry first.
// It is rebuilt only when the log changes, so the scrolling does not jump on
// every redraw. Hover or keyboard focus pauses it; with "reduce motion" turned
// on in the operating system it is a normal scrollable list instead.
let logKey = null;
function renderLog() {
  const box = $('log');
  const key = JSON.stringify(S.log);
  if (key === logKey) return;
  logKey = key;
  if (!S.log.length) { box.innerHTML = '<div class="msg">Nothing has happened yet.</div>'; return; }
  const rows = S.log.map(l => `<div class="lrow"><time class="mono">${clockText(l.minute)}</time><span>${esc(l.text)}${l.user ? ` <em style="color:#4B5F63;font-style:normal">· ${esc(l.user)}</em>` : ''}</span></div>`).join('');
  const reduce = window.matchMedia && window.matchMedia('(prefers-reduced-motion: reduce)').matches;
  if (reduce || !box.offsetParent) {          // no motion wanted, or dashboard hidden (cannot measure)
    box.innerHTML = '<div>' + rows + '</div>';
    if (!box.offsetParent) logKey = null;      // measure again when the dashboard is shown
    return;
  }
  // One copy must be at least as tall as the box, so the loop never shows a gap;
  // two copies are scrolled by exactly one copy's height (-50%), then it repeats.
  // A separator marks where the log starts over, so repeated rows are not
  // mistaken for new events.
  const loop = rows + '<div class="lsep" aria-hidden="true">↻ start of log</div>';
  box.innerHTML = `<div class="ticker-track"><div>${loop}</div></div>`;
  const oneH = box.firstChild.firstChild.offsetHeight || 1;
  const copy = loop.repeat(Math.max(1, Math.ceil(box.clientHeight / oneH)));
  box.innerHTML = `<div class="ticker-track"><div>${copy}</div><div aria-hidden="true">${copy}</div></div>`;
  const track = box.firstChild;
  track.style.setProperty('--dur', Math.max(8, track.firstChild.offsetHeight / 18) + 's');   // about 18 px per second
}

function renderAll() {
  if (!S) return;
  syncTrips();
  buildForm();
  const waiting = S.queue.length;
  const avail = S.ambulances.filter(a => a.status === 'available').length;
  const onCall = S.ambulances.filter(a => a.status === 'busy').length;
  const beds = S.hospitals.reduce((s, h) => s + h.availableBeds, 0);
  const full = S.hospitals.filter(h => h.availableBeds === 0).length;
  $('qCount').textContent = waiting + ' waiting';
  applyRole();
  $('heroNow').textContent = `${waiting} emergenc${waiting === 1 ? 'y' : 'ies'} waiting, ${avail} of ${S.ambulances.length} ambulances available, ${beds} beds free.`;
  $('mapSub').textContent = `${S.nodes.length} junctions, ${S.roads.length} roads, ${S.hospitals.length} hospitals`;
  $('bAmbS').textContent = `${avail} of ${S.ambulances.length} available · ${onCall} on a call`;
  $('bHosS').textContent = `${beds} beds free · ${full} full`;
  $('gname').textContent = `${S.nodes.length} nodes and ${S.roads.length} roads, loaded by the server from city_map.txt`;
  renderQueue();
  renderResponse();
  renderLog();
  renderActive();
  renderBeds();
  renderMiniMap();
  if (page === 'map') renderMap();
  else if (page) renderPage();
}

/* ==========================================================================
   MAP
   ========================================================================== */
function computeBase() {
  const xs = S.nodes.map(n => n.x), ys = S.nodes.map(n => n.y), pad = 60;
  const x0 = Math.min(...xs) - pad, y0 = Math.min(...ys) - pad;
  base = {x: x0, y: y0, w: Math.max(...xs) + pad - x0, h: Math.max(...ys) + pad - y0};
  vb = {z: 1, cx: base.x + base.w / 2, cy: base.y + base.h / 2};
}

function updVB() {
  if (!base) return;
  const w = base.w / vb.z, h = base.h / vb.z;
  vb.cx = Math.max(base.x + w / 2, Math.min(base.x + base.w - w / 2, vb.cx));
  vb.cy = Math.max(base.y + h / 2, Math.min(base.y + base.h - h / 2, vb.cy));
  $('map').setAttribute('viewBox', `${vb.cx - w / 2} ${vb.cy - h / 2} ${w} ${h}`);
}

function renderMap() {
  if (!base) computeBase();
  updVB();
  const N = S.nodes, pts = ids => ids.map(n => N[n].x + ',' + N[n].y).join(' ');
  const f = focus !== null ? emOf(focus) : null;
  let h = '';

  S.roads.forEach(r => {
    const a = N[r.a], b = N[r.b];
    h += `<line x1="${a.x}" y1="${a.y}" x2="${b.x}" y2="${b.y}" stroke="#9DAEB1" stroke-width="2"><title>${esc(a.name)} – ${esc(b.name)}: ${r1(r.minutes)} min${r.oneWay ? ' (one-way)' : ''}</title></line>`;
  });
  // other active routes faint, the selected one strong
  S.emergencies.filter(e => e.status === 'assigned' && e !== f).forEach(e => {
    h += `<polyline points="${pts(e.routeToHospital)}" fill="none" stroke="#B8CFE8" stroke-width="4" stroke-linejoin="round"/>`;
  });
  if (f && f.status === 'assigned') {
    if (f.routeToScene.length > 1) h += `<polyline points="${pts(f.routeToScene)}" fill="none" stroke="#0B5CAD" stroke-width="4" stroke-dasharray="8 6" stroke-linejoin="round" stroke-linecap="round"/>`;
    if (f.routeToHospital.length > 1) h += `<polyline points="${pts(f.routeToHospital)}" fill="none" stroke="#0B5CAD" stroke-width="6" stroke-linejoin="round" stroke-linecap="round"/>`;
  }
  if (showW) S.roads.forEach(r => {
    const a = N[r.a], b = N[r.b];
    h += `<text x="${(a.x + b.x) / 2}" y="${(a.y + b.y) / 2 - 5}" text-anchor="middle" font-size="11" font-family="monospace" fill="#4B5F63" stroke="#F6F8F8" stroke-width="3" paint-order="stroke" style="pointer-events:none">${r1(r.minutes)}</text>`;
  });

  const sel = +$('loc').value;
  const col = {available: '#1B7143', busy: '#0B5CAD', offline: '#8A9A9D'};
  N.forEach(n => {
    const hs = S.hospitals.filter(x => x.node === n.id), parked = S.ambulances.filter(a => a.node === n.id && !onTrip(a));
    h += `<g data-n="${n.id}" style="cursor:pointer">${n.id === sel ? `<circle cx="${n.x}" cy="${n.y}" r="16" fill="none" stroke="#0B5CAD" stroke-width="2.5" stroke-dasharray="4 3"/>` : ''}<circle cx="${n.x}" cy="${n.y}" r="10" fill="#fff" stroke="#102A2E" stroke-width="2"><title>Node ${n.id}: ${esc(n.name)}. Click for the node page.</title></circle><text x="${n.x}" y="${n.y + 4}" text-anchor="middle" font-size="11" fill="#102A2E" font-family="monospace" style="pointer-events:none">${n.id}</text></g>`;
    let y = n.y + 24;
    h += `<text x="${n.x}" y="${y}" text-anchor="middle" font-size="11" fill="#102A2E" stroke="#F6F8F8" stroke-width="3" paint-order="stroke" style="pointer-events:none">${esc(n.name)}</text>`;
    hs.forEach(x => {
      y += 11;
      h += `<text x="${n.x}" y="${y}" text-anchor="middle" font-size="10" fill="${x.availableBeds ? '#1B7143' : '#C62F1A'}" stroke="#F6F8F8" stroke-width="3" paint-order="stroke" style="pointer-events:none">${esc(x.name)} · ${x.availableBeds}/${x.totalBeds} beds</text>`;
    });
    const cw = 30, x0 = n.x - (parked.length * cw - 2) / 2;
    parked.forEach((a, i) => {
      h += `<g data-a="${a.id}" style="cursor:pointer"><title>${ambName(a.id)} · ${AST[a.status][0]}${a.emergency !== null ? ' (#' + a.emergency + ')' : ''}</title><rect x="${x0 + i * cw}" y="${y + 4}" width="28" height="15" rx="3" fill="${col[a.status]}"/><text x="${x0 + i * cw + 14}" y="${y + 15.5}" text-anchor="middle" font-size="11" fill="#fff" style="pointer-events:none">A${a.id}</text></g>`;
    });
  });
  S.hospitals.forEach(x => {
    const n = N[x.node];
    h += `<rect x="${n.x + 9}" y="${n.y - 24}" width="22" height="22" rx="4" fill="${x.availableBeds ? '#1B7143' : '#8A9A9D'}" style="pointer-events:none"/><text x="${n.x + 20}" y="${n.y - 9}" text-anchor="middle" font-size="13" fill="#fff" style="pointer-events:none">H</text>`;
  });
  // ambulances on a trip, at their animated position
  S.emergencies.filter(e => e.status === 'assigned').forEach(e => {
    const t = tripOf(e), p = t.pos;
    h += `<g data-a="${e.ambulance}" style="cursor:pointer"><title>${ambName(e.ambulance)} · #${e.id} · ${PHASE[t.phase]}</title><rect x="${p.x - 17}" y="${p.y - 9}" width="34" height="18" rx="3" fill="#0B5CAD" stroke="#fff" stroke-width="1.5"/><text x="${p.x}" y="${p.y + 4}" text-anchor="middle" font-size="11" fill="#fff" style="pointer-events:none">A${e.ambulance}</text></g>`;
  });
  S.emergencies.filter(e => e.status === 'pending' || (e.status === 'assigned' && tripOf(e).phase === 0)).forEach(e => {
    const n = N[e.node], as = e.status === 'assigned';
    h += `<g style="pointer-events:none"><circle cx="${n.x - 12}" cy="${n.y - 14}" r="11" fill="${as ? '#fff' : '#C62F1A'}" stroke="#C62F1A" stroke-width="2"/><text x="${n.x - 12}" y="${n.y - 9}" text-anchor="middle" font-size="13" fill="${as ? '#C62F1A' : '#fff'}">!</text><text x="${n.x - 12}" y="${n.y - 28}" text-anchor="middle" font-size="11" fill="#C62F1A">#${e.id}</text></g>`;
  });
  $('map').innerHTML = h;
  $('focusChip').textContent = f
    ? `#${f.id} ${f.type} · ${f.status === 'assigned' ? ambName(f.ambulance) + ' → ' + hospName(f.hospital) : f.status}`
    : 'No emergency selected';
}

/* ==========================================================================
   LIVE TRACKING - shared with track.html / track.js
   (page-side animation; the engine is not changed)

   What is real: the route (ambulance -> scene -> hospital), the minutes on
   each road, the hospital and the ambulance all come from the engine
   (DispatchCoordinator + dijkstra), exactly as it computed them.

   What the page adds: it animates the ambulance along that route in real
   time, msPerMin milliseconds per road minute (default 1 s). The engine itself
   has no movement over time: it keeps the ambulance at its start node until
   "Patient delivered" calls completeDispatch, which moves it to the hospital.
   So the animation is a playback of the engine's plan, and delivery is always
   confirmed by the user, never by a timer.

   The tracking itself is a separate page, track.html?id=<emergency>. This
   file only starts trips, opens that page, and shows moving ambulances on the
   dashboard's full map. The start time of each trip is kept in sessionStorage,
   so both pages put the ambulance at the same place.
   ========================================================================== */
const TRIP_KEY = 'opera.trips', SPEED_KEY = 'opera.msPerMin';
const UNDO_MS = 10000;   // how long Undo is offered after a dispatch
const PHASE = ['En route to patient', 'Transporting to hospital', 'Arrived at hospital'];
let trips = {};          // emergency id -> real time (ms) when its trip animation started
let msPerMin = 1000;     // real milliseconds per road minute
const arrivedSaid = new Set();

function loadTrips() {
  try { trips = JSON.parse(ssGet(TRIP_KEY) || '{}') || {}; } catch (e) { trips = {}; }
  const v = +ssGet(SPEED_KEY);
  if ([250, 500, 1000, 2000].includes(v)) msPerMin = v;
}
function saveTrips() { ssSet(TRIP_KEY, JSON.stringify(trips)); }

// Forget trips of emergencies that are no longer assigned (undone or delivered).
function syncTrips() {
  const live = new Set(S.emergencies.filter(e => e.status === 'assigned').map(e => String(e.id)));
  let changed = false;
  for (const id of Object.keys(trips)) {
    if (!live.has(id)) { delete trips[id]; arrivedSaid.delete(+id); changed = true; }
  }
  if (changed) saveTrips();
}

const onTrip = a => a.status === 'busy' && a.emergency !== null && !!emOf(a.emergency) && emOf(a.emergency).status === 'assigned';

// Travel time of the road a -> b as the engine stores it.
function roadMinutes(a, b) {
  let best = Infinity;
  for (const r of S.roads) {
    if ((r.a === a && r.b === b) || (!r.oneWay && r.a === b && r.b === a)) best = Math.min(best, r.minutes);
  }
  return best === Infinity ? 0 : best;
}

// Where the ambulance of an assigned emergency is in its animated trip.
function tripOf(e) {
  const legs = [];
  const add = (ids, phase) => { for (let i = 0; i + 1 < ids.length; i++) legs.push({a: ids[i], b: ids[i + 1], min: roadMinutes(ids[i], ids[i + 1]), phase}); };
  add(e.routeToScene, 0);
  add(e.routeToHospital, 1);
  const toScene = legs.filter(l => l.phase === 0).reduce((s, l) => s + l.min, 0);
  const total = legs.reduce((s, l) => s + l.min, 0);
  if (!(e.id in trips)) { trips[e.id] = Date.now(); saveTrips(); }   // first seen (e.g. other tab)
  const elapsed = Math.max(0, Math.min(total, (Date.now() - trips[e.id]) / msPerMin));
  const N = S.nodes;
  const end = e.routeToHospital.length ? e.routeToHospital[e.routeToHospital.length - 1] : e.node;
  let pos = {x: N[end].x, y: N[end].y}, near = end, idx = legs.length, t = elapsed;
  for (let i = 0; i < legs.length; i++) {
    const l = legs[i];
    if (t < l.min) {
      const f = l.min > 0 ? t / l.min : 1;
      pos = {x: N[l.a].x + (N[l.b].x - N[l.a].x) * f, y: N[l.a].y + (N[l.b].y - N[l.a].y) * f};
      near = f < 0.5 ? l.a : l.b;
      idx = i;
      break;
    }
    t -= l.min;
  }
  const done = elapsed >= total;
  return {legs, total, toScene, elapsed, remaining: total - elapsed, pos, near, idx, done,
          phase: done ? 2 : elapsed < toScene ? 0 : 1,
          chain: e.routeToScene.concat(e.routeToHospital.slice(1))};
}

// Load the tracking webpage for one assigned emergency.
function openTrack(id) {
  location.href = 'track.html?id=' + encodeURIComponent(id);
}

// Animation clock: 10 frames per second while any ambulance is moving.
setInterval(() => {
  if (!S || !me) return;
  let redraw = false;
  S.emergencies.filter(e => e.status === 'assigned').forEach(e => {
    const t = tripOf(e);
    if (!t.done) { redraw = true; return; }
    if (!arrivedSaid.has(e.id)) {
      arrivedSaid.add(e.id);
      redraw = true;
      say(ambName(e.ambulance) + ' has arrived at ' + hospName(e.hospital) + ' with #' + e.id + '. Confirm delivery with "Track live" or "Patient delivered".');
    }
  });
  if (!redraw) return;
  if (page === 'map') renderMap();
  else if (page === null) { renderMiniMap(); renderActive(); }
}, 100);

loadTrips();


/* ---- right column: active dispatches, hospital beds, live mini-map ---- */

// Rebuilt only when the list or a phase changes, so its Track buttons are
// never replaced between mouse-down and mouse-up.
let activeKey = null;
function renderActive() {
  const items = S.emergencies.filter(e => e.status === 'assigned').map(e => ({e, t: tripOf(e)}));
  $('activeN').textContent = items.length;
  const key = items.map(x => x.e.id + ':' + x.t.phase).join(',');
  if (key === activeKey) return;
  activeKey = key;
  $('active').innerHTML = items.length
    ? items.map(({e, t}) => `<div class="arow"><span class="adot ph${t.phase}" aria-hidden="true"></span><div class="at"><b>#${e.id} ${esc(e.type)} · ${ambName(e.ambulance)}</b><em>${PHASE[t.phase]} · to ${esc(hospName(e.hospital))}</em></div><button class="b sm" data-track="${e.id}"><i class="ti ti-route" aria-hidden="true"></i>Track</button></div>`).join('')
    : '<div class="msg">No ambulance is on a call. Dispatched emergencies appear here.</div>';
}

function renderBeds() {
  $('beds').innerHTML = S.hospitals.map(h => {
    const f = h.totalBeds ? h.availableBeds / h.totalBeds : 0;
    const c = h.availableBeds === 0 ? '#C62F1A' : h.availableBeds <= 2 ? '#B5400F' : '#1B7143';
    return `<div class="brow"><span class="bn" title="${esc(nm(h.node))}">${esc(h.name)}</span><span class="bb"><b style="width:${Math.round(f * 100)}%;background:${c}"></b></span><span class="bc" style="color:${c}">${h.availableBeds}/${h.totalBeds}</span></div>`;
  }).join('');
}

// Small live map: roads, hospitals, emergencies and ambulances (moving ones
// at their animated position). No labels; the full map has them.
function renderMiniMap() {
  const N = S.nodes, xs = N.map(n => n.x), ys = N.map(n => n.y), pad = 24;
  const x0 = Math.min(...xs) - pad, y0 = Math.min(...ys) - pad;
  const svg = $('miniMap');
  svg.setAttribute('viewBox', `${x0} ${y0} ${Math.max(...xs) + pad - x0} ${Math.max(...ys) + pad - y0}`);
  let m = '';
  S.roads.forEach(r => { const a = N[r.a], b = N[r.b]; m += `<line x1="${a.x}" y1="${a.y}" x2="${b.x}" y2="${b.y}" stroke="#C4D0D2" stroke-width="3"/>`; });
  S.emergencies.filter(e => e.status === 'assigned').forEach(e => {
    const t = tripOf(e), ahead = [t.pos].concat(t.chain.slice(t.idx + 1).map(n => N[n]));
    if (!t.done && ahead.length > 1) m += `<polyline points="${ahead.map(p => p.x + ',' + p.y).join(' ')}" fill="none" stroke="#0B5CAD" stroke-width="5" stroke-linejoin="round" stroke-linecap="round"/>`;
  });
  N.forEach(n => { m += `<circle cx="${n.x}" cy="${n.y}" r="6" fill="#fff" stroke="#8A9A9D" stroke-width="2.5"/>`; });
  S.hospitals.forEach(h => { const n = N[h.node]; m += `<rect x="${n.x + 7}" y="${n.y - 19}" width="14" height="14" rx="3" fill="${h.availableBeds ? '#1B7143' : '#8A9A9D'}"/>`; });
  S.emergencies.filter(e => e.status === 'pending' || (e.status === 'assigned' && tripOf(e).phase === 0)).forEach(e => {
    const n = N[e.node]; m += `<circle cx="${n.x - 9}" cy="${n.y - 11}" r="7" fill="#C62F1A" stroke="#fff" stroke-width="2"/>`;
  });
  const col = {available: '#1B7143', busy: '#0B5CAD', offline: '#8A9A9D'};
  S.ambulances.filter(a => !onTrip(a)).forEach(a => { const n = N[a.node]; m += `<rect x="${n.x - 9}" y="${n.y + 8}" width="18" height="10" rx="2" fill="${col[a.status]}"/>`; });
  S.emergencies.filter(e => e.status === 'assigned').forEach(e => { const p = tripOf(e).pos; m += `<rect x="${p.x - 11}" y="${p.y - 7}" width="22" height="14" rx="3" fill="#0B5CAD" stroke="#fff" stroke-width="2"/>`; });
  svg.innerHTML = m;
}

/* ==========================================================================
   OTHER PAGES
   ========================================================================== */
function go(p) {
  if (!S) return;
  page = p;
  flt = 'all';
  $('home').hidden = p !== null;
  $('mapview').hidden = p !== 'map';
  $('page').hidden = p === null || p === 'map';
  if (p === null) { logKey = null; renderLog(); activeKey = null; renderActive(); }
  if (p === 'map') renderMap();
  else if (p) renderPage();
  window.scrollTo(0, 0);
}

const header = (title, sub, ico, bg, extra) => `<div class="ph">
  <button class="b" data-back><i class="ti ti-arrow-left" aria-hidden="true"></i>Back to dashboard</button>${extra || ''}
  <div class="pt"><span class="ic" style="background:${bg}"><i class="ti ti-${ico}" aria-hidden="true"></i></span><div><h3>${title}</h3><small>${sub}</small></div></div></div>`;

const filters = (opts, on) => '<div class="fl">' + opts.map(o =>
  `<button class="b" data-flt="${o[0]}" style="${flt === o[0] ? `background:${on[1]};border-color:${on[0]};color:${on[0]}` : ''}">${o[1]} (${o[2]})</button>`).join('') + '</div>';

function ambulancesPage() {
  const c = s => S.ambulances.filter(a => a.status === s).length;
  let h = header('Ambulances', `${S.ambulances.length} units · ${c('available')} available · ${c('busy')} on a call`, 'ambulance', '#0B5CAD');
  h += filters([['all', 'All', S.ambulances.length], ['available', 'Available', c('available')], ['busy', 'On a call', c('busy')], ['offline', 'Offline', c('offline')]], ['#0B5CAD', '#E3EEF9']);
  const list = S.ambulances.filter(a => flt === 'all' || a.status === flt);
  return h + '<div class="plist">' + (list.length ? list.map(a => {
    const st = AST[a.status], e = a.emergency !== null ? emOf(a.emergency) : null;
    // Only admins may change availability; the server checks this too (403).
    const action = !isAdmin() ? '' : a.status === 'offline'
      ? `<button class="b" data-online="${a.id}"><i class="ti ti-power" aria-hidden="true"></i>Bring online</button>`
      : `<button class="b dng" data-offline="${a.id}"${a.status === 'busy' ? ' disabled' : ''}><i class="ti ti-power" aria-hidden="true"></i>Take offline</button>`;
    return `<div class="pc" style="border-top-color:${st[1]}"><div class="row"><div style="display:flex;gap:10px;align-items:center">${icon('ambulance', '#0B5CAD')}<div><div class="nm">${ambName(a.id)}</div><div class="sub">At node ${a.node} · ${esc(nm(a.node))}</div></div></div><span class="bdg" style="background:${st[2]};color:${st[1]}">${st[0]}</span></div>
      <div class="note">${e ? `On call #${e.id} ${esc(e.type)} at ${esc(nm(e.node))}, patient to ${esc(hospName(e.hospital))}. Can go offline after the patient is delivered.` : a.status === 'available' ? 'Ready for dispatch.' : 'Not in service; the engine will not dispatch it.'}</div>
      <div class="tb">${e ? `<button class="b" data-focus="${e.id}"><i class="ti ti-route" aria-hidden="true"></i>Show #${e.id}</button>` : ''}${action}</div></div>`;
  }).join('') : '<div class="msg">No ambulances match this filter.</div>') + '</div>';
}

function hospitalsPage() {
  const open = S.hospitals.filter(x => x.availableBeds > 0).length, beds = S.hospitals.reduce((s, x) => s + x.availableBeds, 0);
  let h = header('Hospitals', `${S.hospitals.length} hospitals · ${beds} beds free · ${S.hospitals.length - open} full`, 'building-hospital', '#1B7143');
  h += filters([['all', 'All', S.hospitals.length], ['open', 'Beds available', open], ['full', 'Full', S.hospitals.length - open]], ['#1B7143', '#E1F2E8']);
  const list = S.hospitals.filter(x => flt === 'all' || (flt === 'open' ? x.availableBeds > 0 : x.availableBeds === 0));
  return h + '<div class="plist">' + (list.length ? list.map(x => {
    const full = x.availableBeds === 0, low = !full && x.availableBeds <= 2;
    const colr = full ? '#C62F1A' : low ? '#8A5300' : '#1B7143', bg = full ? '#FBE6E1' : low ? '#FFF0D6' : '#E1F2E8';
    const inbound = S.emergencies.filter(e => e.status === 'assigned' && e.hospital === x.id);
    const occupied = x.totalBeds - x.availableBeds;
    return `<div class="pc" style="border-top-color:${colr}"><div class="row"><div style="display:flex;gap:10px;align-items:center">${icon('building-hospital', '#1B7143')}<div><div class="nm">${esc(x.name)}</div><div class="sub">Node ${x.node} · ${esc(nm(x.node))}</div></div></div><span class="bdg" style="background:${bg};color:${colr}">${full ? 'Full' : low ? 'Limited' : 'Available'}</span></div>
      <div><div class="kv" style="border:0;padding:0 0 4px"><span>Beds free</span><b style="color:${colr}">${x.availableBeds} of ${x.totalBeds}</b></div><div class="bigbar"><b style="width:${x.totalBeds ? x.availableBeds / x.totalBeds * 100 : 0}%;background:${colr}"></b></div></div>
      <div class="note">${inbound.length ? 'Inbound: ' + inbound.map(e => `#${e.id} (${ambName(e.ambulance)})`).join(', ') : 'No ambulances inbound.'}</div>
      <div class="tb"><button class="b" data-discharge="${x.id}"${occupied ? '' : ' disabled'}><i class="ti ti-door-exit" aria-hidden="true"></i>Discharge a patient</button></div></div>`;
  }).join('') : '<div class="msg">No hospitals match this filter.</div>') + '</div>';
}

function nodePage() {
  const n = nodeOf(nodeId);
  if (!n) return header('Node', 'not found', 'map-pin', '#0C2226');
  const roads = S.roads.filter(r => r.a === n.id || r.b === n.id);
  const hs = S.hospitals.filter(x => x.node === n.id), ambs = S.ambulances.filter(a => a.node === n.id);
  const ems = S.emergencies.filter(e => e.node === n.id && e.status !== 'resolved');
  const pc = (t, c, inner) => `<div class="pc" style="border-top-color:${c}"><h4>${t}</h4>${inner}</div>`;
  let h = header(`Node ${n.id} · ${esc(n.name)}`, `${roads.length} roads`, 'map-pin', '#0C2226',
    '<button class="b" data-backmap><i class="ti ti-map-2" aria-hidden="true"></i>Map</button>');
  h += '<div class="plist">';
  h += pc('Roads from here', '#85B7EB', roads.length ? roads.map(r => {
    const o = r.a === n.id ? r.b : r.a;
    return `<div class="kv"><span><button class="lk" data-node="${o}">${o} · ${esc(nm(o))}</button></span><b>${r1(r.minutes)} min</b></div>`;
  }).join('') : '<div class="sub">No road touches this node, so no ambulance can reach it.</div>');
  h += pc('Hospitals here', '#1B7143', hs.length ? hs.map(x => kv(esc(x.name), x.availableBeds + ' of ' + x.totalBeds + ' beds free')).join('') : '<div class="sub">None.</div>');
  h += pc('Ambulances here', '#0B5CAD', ambs.length ? ambs.map(a => kv(ambName(a.id), AST[a.status][0])).join('') : '<div class="sub">None parked here.</div>');
  h += pc('Open emergencies here', '#C62F1A', ems.length ? ems.map(e => kv(`#${e.id} ${esc(e.type)}`, e.status === 'pending' ? 'Waiting ' + e.waitingMinutes + ' min' : ambName(e.ambulance))).join('') : '<div class="sub">None.</div>');
  h += pc('Actions', '#C62F1A', `<button class="b pr" data-regat="${n.id}" style="width:auto"><i class="ti ti-plus" aria-hidden="true"></i>Register emergency here</button>`);
  return h + '</div>';
}

function renderPage() {
  const html = page === 'amb' ? ambulancesPage() : page === 'hosp' ? hospitalsPage() : nodePage();
  $('pg').innerHTML = html;
}

/* ==========================================================================
   EVENTS
   ========================================================================== */
function selectEmergency(id) { focus = id; renderAll(); }

$('reg').onclick = registerEmergency;
$('disp').onclick = dispatchNext;
$('type').onchange = updTypeInfo;
$('loc').onchange = () => { if (page === 'map') renderMap(); };
$('find').oninput = () => renderQueue();
$('seeMap').onclick = () => go('map');
$('heroTg').onclick = () => {
  const open = $('heroTg').getAttribute('aria-expanded') !== 'true';
  $('heroTg').setAttribute('aria-expanded', String(open));
  $('heroMore').hidden = !open;
};
$('mapBack').onclick = () => go(null);
$('bAmb').onclick = () => go('amb');
$('bHos').onclick = () => go('hosp');
$('tW').onclick = () => { showW = !showW; $('tW').classList.toggle('on', showW); $('tW').setAttribute('aria-pressed', showW); renderMap(); };
$('zin').onclick = () => { vb.z = Math.min(3, vb.z * 1.4); updVB(); };
$('zout').onclick = () => { vb.z = Math.max(1, vb.z / 1.4); updVB(); };
$('zres').onclick = () => { computeBase(); updVB(); };

$('queue').onclick = e => { const t = e.target.closest('[data-f]'); if (t) selectEmergency(+t.dataset.f); };
$('queue').onkeydown = e => { if (e.key === 'Enter') { const t = e.target.closest('[data-f]'); if (t) selectEmergency(+t.dataset.f); } };
$('active').onclick = e => { const t = e.target.closest('[data-track]'); if (t) openTrack(+t.dataset.track); };
$('respAct').onclick = e => {
  const d = e.target.closest('[data-done]'), tr = e.target.closest('[data-track]');
  if (tr) openTrack(+tr.dataset.track);
  else if (d) complete(+d.dataset.done);
  else if (e.target.closest('[data-showmap]')) go('map');
};
$('pg').onclick = e => {
  const c = s => e.target.closest(s);
  let x;
  if (c('[data-back]')) go(null);
  else if (c('[data-backmap]')) go('map');
  else if ((x = c('[data-flt]'))) { flt = x.dataset.flt; renderPage(); }
  else if ((x = c('[data-node]'))) { nodeId = +x.dataset.node; go('node'); }
  else if ((x = c('[data-regat]'))) { $('loc').value = x.dataset.regat; go(null); say('Location set to ' + nm(+x.dataset.regat) + '. Choose the type and press Register.'); }
  else if ((x = c('[data-offline]'))) setOnline(+x.dataset.offline, false);
  else if ((x = c('[data-online]'))) setOnline(+x.dataset.online, true);
  else if ((x = c('[data-discharge]'))) discharge(+x.dataset.discharge);
  else if ((x = c('[data-focus]'))) openTrack(+x.dataset.focus);
};

/* map: click a node or an ambulance; drag to pan when zoomed */
let pd = null;
$('map').addEventListener('pointerdown', e => {
  const t = e.target.closest('[data-n],[data-a]');
  pd = {x: e.clientX, y: e.clientY, n: t ? t.dataset.n : undefined, a: t ? t.dataset.a : undefined, cx: vb.cx, cy: vb.cy, moved: false};
});
$('map').addEventListener('pointermove', e => {
  if (!pd) return;
  const dx = e.clientX - pd.x, dy = e.clientY - pd.y;
  if (Math.abs(dx) + Math.abs(dy) > 6) pd.moved = true;
  if (pd.moved && vb.z > 1) { const k = (base.w / vb.z) / $('map').clientWidth; vb.cx = pd.cx - dx * k; vb.cy = pd.cy - dy * k; updVB(); }
});
window.addEventListener('pointerup', () => {
  if (!pd) return;
  const d = pd;
  pd = null;
  if (d.moved) return;
  if (d.a !== undefined) go('amb');
  else if (d.n !== undefined) { nodeId = +d.n; go('node'); }
});

/* icon font blocked or offline: fall back to plain symbols */
window.addEventListener('load', () => {
  const on = () => document.body.classList.add('noicons');
  try { document.fonts.load('1em "tabler-icons"').then(f => { if (!f.length) on(); }).catch(on); } catch (e) { on(); }
});

/* ==========================================================================
   SIGN-IN, SESSION, ROLES
   ========================================================================== */
const TOKEN_KEY = 'opera.token';
const isAdmin = () => !!me && me.role === 'admin';
function ssGet(k) { try { return sessionStorage.getItem(k); } catch (e) { return null; } }
function ssSet(k, v) { try { if (v === null) sessionStorage.removeItem(k); else sessionStorage.setItem(k, v); } catch (e) { /* storage blocked */ } }

function applyRole() {
  $('who').textContent = me ? me.name + ' · ' + me.role : '';
  $('heroRole').textContent = isAdmin()
    ? 'As admin you can also take ambulances offline and bring them back online.'
    : 'As operator you can register, dispatch, undo, deliver and discharge.';
}

function enterApp() {
  $('login').hidden = true;
  $('op').hidden = false;
  document.title = 'OPERA · Dispatch desk';
  page = null;
  go(null);
  renderAll();
  window.scrollTo(0, 0);
}

function showLogin(message) {
  $('op').hidden = true;
  $('login').hidden = false;
  document.title = 'OPERA · Sign in';
  $('l_pass').value = '';
  $('l_cap').value = '';
  $('lerr').textContent = message || '';
  newCaptcha();
}

// Forget the session locally and go back to the sign-in page.
function endSession(message) {
  TOKEN = null; me = null; S = null; focus = null; page = null; base = null;
  trips = {}; saveTrips(); arrivedSaid.clear();
  ssSet(TOKEN_KEY, null);
  showLogin(message);
}

async function logout() {
  try { if (API && TOKEN) await request('POST', '/api/logout'); } catch (e) { /* server gone: sign out locally anyway */ }
  endSession('');
}

/* ---- captcha (codes from captcha.txt; a check in the page, not a security measure) ---- */
const CAP_FALLBACK = ["L8MV2N", "ZFTY5G", "RU7G43", "XXSAYU", "36C8Y9", "YRW293", "QGM42R", "3RNSG6", "J6FMU5", "E5RVLX", "TYF4X7", "XKWZQ4", "9A5NTL", "SFXQQG", "UHU77M", "VKFX93", "U8GX6F", "D65LGN", "WAWTFY", "NZYPM7"];
let CAPS = CAP_FALLBACK.slice(), captcha = '';
const rnd = (a, b) => a + Math.random() * (b - a);
function rndInt(n) { try { const a = new Uint32Array(1); crypto.getRandomValues(a); return a[0] % n; } catch (e) { return Math.floor(Math.random() * n); } }
function parseCaptchas(t) {
  const seen = new Set();
  t.split('\n').map(l => l.trim()).filter(l => l && l[0] !== '#').forEach(l => { l = l.toUpperCase(); if (/^[A-Z0-9]{4,10}$/.test(l)) seen.add(l); });
  return Array.from(seen);
}
async function loadCaptchas() {
  try {
    const r = await fetch('captcha.txt?t=' + Date.now(), {cache: 'no-store'});
    if (!r.ok) throw new Error(r.status);
    const c = parseCaptchas(await r.text());
    if (c.length >= 2) CAPS = c;
  } catch (e) { /* keep the built-in list */ }
  newCaptcha();
}
function newCaptcha() {
  let c;
  do { c = CAPS[rndInt(CAPS.length)]; } while (c === captcha && CAPS.length > 1);
  captcha = c;
  $('capBox').innerHTML = c.split('').map(ch => '<span style="transform:rotate(' + Math.round(rnd(-14, 14)) + 'deg) translateY(' + Math.round(rnd(-4, 4)) + 'px)">' + ch + '</span>').join('');
}

async function signIn() {
  const err = $('lerr');
  err.textContent = '';
  const name = $('l_name').value.trim(), pw = $('l_pass').value, cap = $('l_cap').value.trim().toUpperCase();
  if (!name || !pw || !cap) { err.textContent = 'Enter your name, your password and the characters shown in the image.'; return; }
  if (cap !== captcha) { err.textContent = 'The characters did not match. A new image is shown.'; newCaptcha(); $('l_cap').value = ''; return; }
  if (!API && !(await findServer())) { err.textContent = 'The OPERA server is not running. Start opera_server, then try again.'; return; }
  $('lgo').disabled = true;
  try {
    const r = await request('POST', '/api/login', {name, password: pw});
    if (r.res.ok && r.data && r.data.token) {
      TOKEN = r.data.token;
      me = r.data.user;
      S = r.data.state;
      ssSet(TOKEN_KEY, TOKEN);
      $('l_pass').value = ''; $('l_cap').value = '';
      setConn(true);
      enterApp();
      say(r.data.message);
    } else {
      err.textContent = (r.data && r.data.message) || ('Sign-in failed (HTTP ' + r.res.status + ').');
      newCaptcha(); $('l_pass').value = ''; $('l_cap').value = '';
    }
  } catch (e) {
    API = null;
    err.textContent = 'Lost contact with the OPERA server. Start opera_server, then try again.';
    waitForServer();
  } finally {
    $('lgo').disabled = false;
  }
}

/* ---- "Make a passwords.txt line" (same format the server checks) ---- */
async function sha256hex(s) {
  if (!(window.crypto && crypto.subtle)) throw new Error('nocrypto');
  const b = await crypto.subtle.digest('SHA-256', new TextEncoder().encode(s));
  return Array.from(new Uint8Array(b)).map(x => x.toString(16).padStart(2, '0')).join('');
}
async function makeLine() {
  const n = $('mk_name').value.trim(), r = $('mk_role').value, p = $('mk_pass').value, m = $('mk_msg');
  if (!n || /[|\n]/.test(n)) { m.textContent = 'Enter a name without the | character.'; return; }
  if (p.length < 8) { m.textContent = 'Use a password of at least 8 characters.'; return; }
  try {
    const salt = Array.from(crypto.getRandomValues(new Uint8Array(8))).map(b => b.toString(16).padStart(2, '0')).join('');
    $('mk_out').value = 'USER ' + n + ' | ' + r + ' | ' + salt + ' | ' + await sha256hex(salt + p);
    $('mk_pass').value = '';
    m.textContent = 'Paste this line into data/passwords.txt, delete any older line for ' + n + ', and restart opera_server.';
  } catch (e) { m.textContent = 'This browser cannot hash here. Open the page through http://localhost.'; }
}

/* ==========================================================================
   FINDING THE SERVER, START
   ========================================================================== */
async function findServer() {
  const list = [];
  if (location.protocol === 'http:' || location.protocol === 'https:') list.push(location.origin);
  list.push('http://localhost:8080', 'http://127.0.0.1:8080');
  for (const base of Array.from(new Set(list))) {
    try {
      const r = await fetch(base + '/api/ping', {cache: 'no-store'});
      if (!r.ok) continue;                     // e.g. Live Server answers 404 here
      const d = await r.json();
      if (d && d.opera) { API = base; showServer(true); return true; }
    } catch (e) { /* not there: try the next address */ }
  }
  API = null;
  showServer(false);
  return false;
}

function showServer(ok) {
  $('lsrv').innerHTML = ok
    ? '<span class="lg-ok">Connected to the OPERA server at ' + esc(API) + '.</span>'
    : '<span class="lg-warn">The OPERA server is not running. Start opera_server (see HOW_TO_RUN.md); this page connects as soon as it is up.</span>'
      + (location.protocol === 'file:' ? '<span class="lg-warn">This page was opened as a file. Open http://localhost:8080 instead.</span>' : '');
}

let serverTimer = null;
function waitForServer() {
  if (serverTimer) return;
  serverTimer = setInterval(async () => {
    if (await findServer()) { clearInterval(serverTimer); serverTimer = null; }
  }, 3000);
}

async function start() {
  $('lform').onsubmit = ev => { ev.preventDefault(); signIn(); };
  $('capNew').onclick = () => { newCaptcha(); $('l_cap').value = ''; $('l_cap').focus(); };
  $('mkline').onclick = makeLine;
  $('logout').onclick = logout;
  loadCaptchas();
  if (!(await findServer())) { waitForServer(); return; }
  // Still signed in from before a page reload?
  const saved = ssGet(TOKEN_KEY);
  if (!saved) return;
  TOKEN = saved;
  try {
    const r = await request('GET', '/api/session');
    if (r.res.ok && r.data && r.data.user) {
      me = r.data.user;
      S = r.data.state;
      setConn(true);
      enterApp();
      return;
    }
    if (r.res.status === 401) { endSession(''); return; }   // the server says the token is no longer valid
  } catch (e) {
    // Network error, or the request was cancelled because the page is being
    // reloaded. That says nothing about the session, so keep the stored token.
  }
  TOKEN = null;
  showLogin('');
}

start();
