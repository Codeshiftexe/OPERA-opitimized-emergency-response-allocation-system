'use strict';
/* ==========================================================================
   OPERA live tracking page   (track.html?id=<emergency id>)

   opera.js opens this page after a successful dispatch.

   Where every piece comes from:
     route        emergency.routeToScene + emergency.routeToHospital, the node
                  lists the C++ engine computed with Dijkstra
     road times   state.roads[].minutes, the engine's graph weights
     ambulance, hospital, beds, status   the engine's state

   What this page adds: it moves the ambulance along that route in real time,
   msPerMin milliseconds per road minute. It never takes its own decisions:
   when the ambulance arrives, the user confirms delivery, and only that click
   calls the engine (POST /api/emergencies/complete).

   Undo: for 10 seconds after the dispatch an "Undo dispatch" button with a
   countdown is shown here (opera.js stores {id, until} in opera.undo). It calls
   POST /api/undo, which reverses the most recent dispatch still in progress -
   within those seconds that is this one, unless another user dispatched in the
   meantime; the server's reply always names the emergency it undid.

   Shared with opera.js through sessionStorage (same browser tab):
     opera.token     session token from sign-in
     opera.trips     {emergency id: time the trip started}, so the dashboard map
                     and this page show the ambulance at the same place
     opera.msPerMin  playback speed
   ========================================================================== */

const TOKEN_KEY = 'opera.token', TRIP_KEY = 'opera.trips', SPEED_KEY = 'opera.msPerMin';
const UNDO_KEY = 'opera.undo', UNDO_MS = 10000;   // undo window written by opera.js at dispatch
const PHASE = ['En route to patient', 'Transporting to hospital', 'Arrived at hospital'];
const SPEEDS = [[2000, 'Slow (1 road min = 2 s)'], [1000, 'Normal (1 road min = 1 s)'],
                [500, 'Fast (1 road min = 0.5 s)'], [250, 'Very fast (1 road min = 0.25 s)']];
const POLL_MS = 2000;    // re-read the engine state this often (undo / delivery elsewhere)

let API = null;          // server address
let TOKEN = null;        // session token
let S = null;            // last engine state
let me = null;           // signed-in user
let emId = null;         // emergency being tracked
let trips = {};
let msPerMin = 1000;
let layoutKey = null;    // the side panel is rebuilt only when this changes
let busy = false;

const $ = id => document.getElementById(id);
const esc = s => String(s == null ? '' : s).replace(/[&<>"']/g,
  c => ({'&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;'}[c]));
const r1 = x => Math.round(x * 10) / 10;
const nm = id => (S.nodes[id] ? S.nodes[id].name : 'node ' + id);
const hospOf = id => S.hospitals.find(h => h.id === id);
const emOf = id => S.emergencies.find(e => e.id === id);
const hospName = id => (hospOf(id) || {name: 'Hospital ' + id}).name;
const ambName = id => 'Ambulance ' + id;
const TYPE_COLOR = {Cardiac: '#C62F1A', Trauma: '#B5400F', Transfer: '#0F766E'};

function ssGet(k) { try { return sessionStorage.getItem(k); } catch (e) { return null; } }
function ssSet(k, v) { try { sessionStorage.setItem(k, v); } catch (e) { /* storage blocked */ } }

function loadTrips() {
  try { trips = JSON.parse(ssGet(TRIP_KEY) || '{}') || {}; } catch (e) { trips = {}; }
  const v = +ssGet(SPEED_KEY);
  if (SPEEDS.some(s => s[0] === v)) msPerMin = v;
}
function saveTrips() { ssSet(TRIP_KEY, JSON.stringify(trips)); }

function say(text, bad) {
  const f = $('flash');
  f.textContent = text;
  f.className = 'flash' + (bad ? ' bad' : '');
  f.hidden = !text;
}

function showMessage(title, body) {
  $('view').hidden = true;
  $('msg').hidden = false;
  $('msgT').textContent = title;
  $('msgB').textContent = body;
}

/* ==========================================================================
   SERVER
   ========================================================================== */
async function findServer() {
  const list = [];
  if (location.protocol === 'http:' || location.protocol === 'https:') list.push(location.origin);
  list.push('http://localhost:8080', 'http://127.0.0.1:8080');
  for (const base of Array.from(new Set(list))) {
    try {
      const r = await fetch(base + '/api/ping', {cache: 'no-store'});
      if (!r.ok) continue;
      const d = await r.json();
      if (d && d.opera) { API = base; return true; }
    } catch (e) { /* try the next address */ }
  }
  return false;
}

async function request(method, path, body) {
  const headers = {Authorization: 'Bearer ' + TOKEN};
  if (method !== 'GET') headers['Content-Type'] = 'application/json';
  const res = await fetch(API + path, {method, headers, cache: 'no-store',
    body: method === 'GET' ? undefined : JSON.stringify(body || {})});
  let data = null;
  try { data = await res.json(); } catch (e) { data = null; }
  return {res, data};
}

function setConn(ok) {
  $('conn').textContent = ok ? 'Engine connected' : 'Server not reachable';
  $('conn').className = 'chip ' + (ok ? 'ok' : 'al');
}

// Reads the engine state. Returns false if the page cannot continue.
async function refresh() {
  let r;
  try { r = await request('GET', '/api/session'); }
  catch (e) { setConn(false); say('Lost contact with the OPERA server. Is opera_server still running?', true); return true; }
  setConn(true);
  if (r.res.status === 401) {
    showMessage('Please sign in again', 'Your session has ended (for example, the server was restarted). Go back to the dashboard and sign in.');
    return false;
  }
  if (!r.data || !r.data.state) { say('The server sent an unexpected reply (HTTP ' + r.res.status + ').', true); return true; }
  if (!busy) { S = r.data.state; me = r.data.user; }
  $('who').textContent = me ? me.name + ' · ' + me.role : '';
  return true;
}

/* ==========================================================================
   THE TRIP - computed only from the engine's route and road times
   ========================================================================== */

// Travel time of the road a -> b, as stored in the engine's graph.
function roadMinutes(a, b) {
  let best = Infinity;
  for (const r of S.roads) {
    if ((r.a === a && r.b === b) || (!r.oneWay && r.a === b && r.b === a)) best = Math.min(best, r.minutes);
  }
  return best === Infinity ? 0 : best;
}

// Where the ambulance is now on the engine's route.
function tripOf(e) {
  const legs = [];
  const add = (ids, phase) => { for (let i = 0; i + 1 < ids.length; i++) legs.push({a: ids[i], b: ids[i + 1], min: roadMinutes(ids[i], ids[i + 1]), phase}); };
  add(e.routeToScene, 0);       // ambulance -> patient   (engine's Dijkstra path)
  add(e.routeToHospital, 1);    // patient   -> hospital  (engine's Dijkstra path)
  const toScene = legs.filter(l => l.phase === 0).reduce((s, l) => s + l.min, 0);
  const total = legs.reduce((s, l) => s + l.min, 0);
  if (!(e.id in trips)) { trips[e.id] = Date.now(); saveTrips(); }
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
  return {total, toScene, elapsed, remaining: total - elapsed, pos, near, idx, done,
          phase: done ? 2 : elapsed < toScene ? 0 : 1,
          chain: e.routeToScene.concat(e.routeToHospital.slice(1))};
}

function setSpeed(v) {
  const now = Date.now();
  for (const id of Object.keys(trips)) trips[id] = now - (now - trips[id]) / msPerMin * v;  // keep progress
  msPerMin = v;
  ssSet(SPEED_KEY, String(v));
  saveTrips();
}

/* ==========================================================================
   DRAWING
   ========================================================================== */
function drawMap(e, t) {
  const N = S.nodes, svg = $('tmap');
  // Whole city in view, so the trip is seen in context.
  const xs = N.map(n => n.x), ys = N.map(n => n.y), pad = 70;
  const x0 = Math.min(...xs) - pad, y0 = Math.min(...ys) - pad;
  const w = Math.max(...xs) + pad - x0, h = Math.max(...ys) + pad - y0;
  svg.setAttribute('viewBox', `${x0} ${y0} ${w} ${h}`);
  const pts = arr => arr.map(p => p.x + ',' + p.y).join(' ');
  const onRoute = new Set(t.chain);
  let m = '';
  S.roads.forEach(r => { const a = N[r.a], b = N[r.b]; m += `<line x1="${a.x}" y1="${a.y}" x2="${b.x}" y2="${b.y}" stroke="#C4D0D2" stroke-width="2"/>`; });
  S.roads.forEach(r => { const a = N[r.a], b = N[r.b]; m += `<text x="${(a.x + b.x) / 2}" y="${(a.y + b.y) / 2 - 5}" text-anchor="middle" font-size="10" font-family="monospace" fill="#6B7C80" stroke="#F6F8F8" stroke-width="3" paint-order="stroke">${r1(r.minutes)}</text>`; });
  // driven part (dashed) and part still ahead (solid), split at the ambulance
  const driven = t.chain.slice(0, t.idx + 1).map(n => N[n]).concat(t.done ? [] : [t.pos]);
  if (driven.length > 1) m += `<polyline points="${pts(driven)}" fill="none" stroke="#A9C8E8" stroke-width="5" stroke-dasharray="8 6" stroke-linejoin="round"/>`;
  if (!t.done) m += `<polyline points="${pts([t.pos].concat(t.chain.slice(t.idx + 1).map(n => N[n])))}" fill="none" stroke="#0B5CAD" stroke-width="6" stroke-linejoin="round" stroke-linecap="round"/>`;
  N.forEach(n => {
    const key = onRoute.has(n.id);
    m += `<circle cx="${n.x}" cy="${n.y}" r="${key ? 11 : 8}" fill="#fff" stroke="${key ? '#0B5CAD' : '#8A9A9D'}" stroke-width="2"/><text x="${n.x}" y="${n.y + 4}" text-anchor="middle" font-size="10" fill="#102A2E" font-family="monospace">${n.id}</text>`;
    m += `<text x="${n.x}" y="${n.y + 24}" text-anchor="middle" font-size="11" fill="${key ? '#102A2E' : '#6B7C80'}" stroke="#F6F8F8" stroke-width="3" paint-order="stroke">${esc(n.name)}</text>`;
  });
  const start = N[t.chain[0]];
  // left of the node, so it never covers the hospital "H" drawn to the right
  m += `<text x="${start.x - 15}" y="${start.y - 10}" text-anchor="end" font-size="10" fill="#0B5CAD" stroke="#F6F8F8" stroke-width="3" paint-order="stroke">START</text>`;
  const hosp = hospOf(e.hospital);
  if (hosp) {
    const hn = N[hosp.node];
    m += `<rect x="${hn.x + 9}" y="${hn.y - 28}" width="24" height="24" rx="4" fill="#1B7143"/><text x="${hn.x + 21}" y="${hn.y - 11}" text-anchor="middle" font-size="14" fill="#fff">H</text>`;
  }
  if (t.phase === 0) {
    const n = N[e.node];
    m += `<circle cx="${n.x - 14}" cy="${n.y - 16}" r="11" fill="#C62F1A"/><text x="${n.x - 14}" y="${n.y - 11}" text-anchor="middle" font-size="13" fill="#fff">!</text>`;
  }
  const p = t.pos;
  m += `<g id="tAmb"><rect x="${p.x - 19}" y="${p.y - 10}" width="38" height="20" rx="4" fill="#0B5CAD" stroke="#fff" stroke-width="2"/><text x="${p.x}" y="${p.y + 4}" text-anchor="middle" font-size="11" fill="#fff">A${e.ambulance}</text></g>`;
  svg.innerHTML = m;
}

function chainHtml(t) {
  return t.chain.map((n, k) => {
    const cls = t.done || k <= t.idx ? 'pd' : k === t.idx + 1 ? 'cu' : '';
    return `<span class="${cls}" title="node ${n}">${esc(nm(n))}</span>`;
  }).join('→');
}

// The side panel holds buttons, so it is rebuilt only when the phase changes.
function sideHtml(e, t) {
  const h = hospOf(e.hospital), ty = TYPE_COLOR[e.type] || '#4B5F63';
  const stc = t.done ? '#1B7143' : '#0B5CAD', bg = t.done ? '#E1F2E8' : '#E3EEF9';
  const action = t.done
    ? `<div class="note" style="background:#E1F2E8">${ambName(e.ambulance)} has arrived at ${esc(hospName(e.hospital))}.</div>
       <button class="b pr" id="done"><i class="ti ti-check" aria-hidden="true"></i>Confirm patient delivered</button>`
    : `<button class="b" id="done"><i class="ti ti-check" aria-hidden="true"></i>Mark delivered now</button>`;
  return `
    <div class="pc" style="border-top-color:${stc}"><div class="row"><span class="bdg" style="background:${bg};color:${stc}">${PHASE[t.phase]}</span>
      <div style="text-align:right"><div style="font-size:24px;font-weight:500" id="tEta"></div><div class="sub">${t.done ? 'patient at hospital' : 'road time remaining'}</div></div></div>
      <div class="bigbar"><b id="tBar" style="width:0%;background:${stc}"></b></div><div class="sub" id="tPct"></div></div>
    <div class="pc" style="border-top-color:#0B5CAD"><div class="nm">${ambName(e.ambulance)}</div><div class="sub">Near <span id="tNear"></span></div>
      <div class="sub">Engine route: ${r1(e.minutesToScene)} min to the patient, then ${r1(e.minutesToHospital)} min to the hospital.</div></div>
    <div class="pc" style="border-top-color:#1B7143"><div class="nm">${esc(hospName(e.hospital))}</div>
      <div class="sub">Node ${h ? h.node + ' · ' + esc(nm(h.node)) : '?'}</div>
      <div class="sub">${h ? h.availableBeds + ' of ' + h.totalBeds + ' beds free (one reserved for #' + e.id + ')' : ''}</div></div>
    <div class="pc" style="border-top-color:${ty}"><div class="row"><div><div class="nm">#${e.id} ${esc(e.type)}</div><div class="sub">Patient at node ${e.node} · ${esc(nm(e.node))}</div></div><span class="tag" style="color:${ty}">Target ${e.targetMinutes} min</span></div></div>
    <div class="pc" style="border-top-color:#85B7EB"><div class="nm" style="font-size:14px">Route (from the engine)</div><div class="path" id="tChain"></div></div>
    <div class="pc" style="border-top-color:#C62F1A">${action}
      <label>Playback speed<select id="speed">${SPEEDS.map(s => `<option value="${s[0]}"${s[0] === msPerMin ? ' selected' : ''}>${s[1]}</option>`).join('')}</select></label></div>`;
}

function render() {
  if (!S) return;
  renderUndo();
  const e = emOf(emId);
  if (!e) { showMessage('Emergency not found', 'There is no emergency #' + emId + '. It may belong to an earlier server run.'); return; }
  if (e.status === 'pending') {
    showMessage('Dispatch undone', '#' + e.id + ' is back in the queue, waiting for an ambulance.');
    return;
  }
  if (e.status === 'resolved') {
    showMessage('Patient delivered', '#' + e.id + ' ' + e.type + ' was delivered to ' + hospName(e.hospital) + ' by ' + ambName(e.ambulance) +
      '. The ambulance is available again at the hospital; the bed stays occupied until the patient is discharged.');
    return;
  }
  $('msg').hidden = true;
  $('view').hidden = false;
  const t = tripOf(e);
  const h = hospOf(e.hospital);
  // Rebuild only when something shown in the panel changed (phase, speed, beds).
  const key = e.id + ':' + t.phase + ':' + msPerMin + ':' + (h ? h.availableBeds : '');
  if (key !== layoutKey) {
    $('title').textContent = 'Tracking #' + e.id + ' · ' + ambName(e.ambulance);
    $('sub').textContent = nm(t.chain[0]) + ' → ' + nm(e.node) + ' (patient) → ' + hospName(e.hospital);
    document.title = 'OPERA · Tracking #' + e.id;
    $('side').innerHTML = sideHtml(e, t);
    $('done').onclick = deliver;
    $('speed').onchange = ev => { setSpeed(+ev.target.value); layoutKey = null; render(); };
    layoutKey = key;
  }
  drawMap(e, t);
  const pct = t.total > 0 ? Math.round(100 * t.elapsed / t.total) : 100;
  $('tEta').textContent = t.done ? 'Arrived' : Math.max(1, Math.ceil(t.remaining)) + ' min';
  $('tBar').style.width = pct + '%';
  $('tPct').textContent = pct + '% of the trip · ' + r1(t.elapsed) + ' of ' + r1(t.total) + ' road minutes';
  $('tNear').textContent = 'node ' + t.near + ' · ' + nm(t.near);
  $('tChain').innerHTML = chainHtml(t);
}

async function deliver() {
  if (busy) return;
  busy = true;
  try {
    const r = await request('POST', '/api/emergencies/complete', {id: emId});
    if (r.data && r.data.state) S = r.data.state;
    if (r.data && r.data.message) say(r.data.message, !r.data.ok);
    if (r.res.status === 401) showMessage('Please sign in again', 'Your session has ended. Go back to the dashboard and sign in.');
  } catch (e) {
    say('Lost contact with the OPERA server.', true);
  } finally {
    busy = false;
    layoutKey = null;
    render();
  }
}

/* ==========================================================================
   UNDO WINDOW (10 seconds after the dispatch)
   ========================================================================== */
function undoWindow() {
  try {
    const u = JSON.parse(ssGet(UNDO_KEY) || 'null');
    if (u && u.id === emId && Date.now() < u.until) return u;
  } catch (e) { /* no window */ }
  return null;
}

function renderUndo() {
  const u = undoWindow(), e = S ? emOf(emId) : null, bar = $('undoBar');
  if (!u || !e || e.status !== 'assigned') { bar.hidden = true; return; }
  const left = Math.max(0, u.until - Date.now());
  bar.hidden = false;
  $('undoTxt').textContent = ambName(e.ambulance) + ' dispatched to #' + e.id + '. Wrong call?';
  $('undoSec').textContent = Math.ceil(left / 1000);
  $('undoFill').style.width = (left / UNDO_MS * 100) + '%';
}

async function undoDispatch() {
  if (busy || !undoWindow()) return;
  busy = true;
  try {
    const r = await request('POST', '/api/undo');
    if (r.data && r.data.state) S = r.data.state;
    if (r.data && r.data.message) say(r.data.message, !r.data.ok);
    ssSet(UNDO_KEY, 'null');   // one undo per dispatch
  } catch (e) {
    say('Lost contact with the OPERA server.', true);
  } finally {
    busy = false;
    layoutKey = null;
    render();
  }
}

/* ==========================================================================
   START
   ========================================================================== */
async function start() {
  emId = parseInt(new URLSearchParams(location.search).get('id'), 10);
  if (!Number.isInteger(emId)) { showMessage('Nothing to track', 'Open this page from the dashboard by dispatching an emergency.'); return; }
  TOKEN = ssGet(TOKEN_KEY);
  if (!TOKEN) { showMessage('Please sign in', 'Sign in on the dashboard first, then dispatch an emergency.'); return; }
  loadTrips();
  if (!(await findServer())) {
    setConn(false);
    showMessage('Server not running', 'Start opera_server (see HOW_TO_RUN.md), then reload this page.');
    return;
  }
  if (!(await refresh())) return;
  $('undoBtn').onclick = undoDispatch;
  render();
  // Smooth movement: redraw every animation frame while the trip is running.
  const frame = () => { render(); requestAnimationFrame(frame); };
  requestAnimationFrame(frame);
  // Re-read the engine state regularly, so an undo or a delivery made elsewhere shows up.
  setInterval(async () => { if (!busy) await refresh(); }, POLL_MS);
}

/* icon font blocked or offline: fall back to plain symbols (same as opera.js) */
window.addEventListener('load', () => {
  const on = () => document.body.classList.add('noicons');
  try { document.fonts.load('1em "tabler-icons"').then(f => { if (!f.length) on(); }).catch(on); } catch (e) { on(); }
});

start();
