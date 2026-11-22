/* ==========================================================================
   OPERA - real-time tracking in the browser (dashboard and tracking page)

   Loaded after opera.js (dashboard) or track.js (tracking page). It changes
   neither file; it replaces three of their global functions at run time:

     request()   every server reply carries "tracking": the live position of
                 every ambulance on a call, kept by the C++ LiveTracker.
                 We store it here (window.OPERA_LIVE).
     tripOf(e)   used by the 2D map, the 3D map, the progress bar and the ETA.
                 It places the ambulance at the SERVER's live position instead
                 of a timer in this browser.
     setSpeed()  (tracking page) demo speed, a server setting (simulated mode only).

   Two server modes (POST /api/tracking/mode, admin only):
     gps        REAL tracking (default). The position comes only from the crew
                device's GPS fixes (crew.html) or junction reports. Between fixes
                nothing is invented: the ambulance stays where it was last seen,
                and the badge shows how old that fix is. A new fix glides the
                marker to its new place over 0.8 s, purely for the eye.
     simulated  demo without a device: the server moves trips along the route.
                The badge says SIMULATED so it is never mistaken for real data.

   The dashboard re-reads the server every 2 seconds (the tracking page already
   does), so it shows new fixes, other operators and auto-dispatch without a click.
   ========================================================================== */
(function () {
  'use strict';
  if (typeof request !== 'function' || typeof tripOf !== 'function') return;

  const LIVE = window.OPERA_LIVE = {ok: false, at: 0, spm: 1, mode: 'gps', geo: false, trips: new Map()};
  const onTrackPage = typeof emId !== 'undefined';
  const GLIDE_MS = 800;       // marker glide to a new fix (display only)
  const STALE_S = 15;         // a GPS fix older than this is shown as "signal lost?"
  const glide = new Map();    // emergency id -> {from, to, at}

  /* ---- 1. remember the live positions from every reply ---- */
  const plainRequest = request;
  window.request = async function (method, path, body) {
    const r = await plainRequest(method, path, body);
    const tr = r && r.data && r.data.tracking;
    if (tr && Array.isArray(tr.trips)) {
      const now = Date.now();
      LIVE.ok = true;
      LIVE.at = now;
      LIVE.spm = tr.secondsPerMinute || 1;
      LIVE.mode = tr.mode || 'gps';
      LIVE.geo = !!tr.geo;
      const next = new Map(tr.trips.map(t => [t.emergency, t]));
      for (const [id, t] of next) {          // start a glide when the server position moved
        const g = glide.get(id);
        if (!g) glide.set(id, {from: t.progress, to: t.progress, at: now});
        else if (Math.abs(g.to - t.progress) > 1e-6) glide.set(id, {from: shown(g, now), to: t.progress, at: now});
      }
      for (const id of glide.keys()) if (!next.has(id)) glide.delete(id);
      LIVE.trips = next;
      try { msPerMin = Math.round(LIVE.spm * 1000); } catch (e) { /* page without msPerMin */ }
    }
    return r;
  };
  function shown(g, now) {
    const f = Math.min(1, (now - g.at) / GLIDE_MS);
    return g.from + (g.to - g.from) * f;
  }

  // Road minutes covered right now, as the server says (no guessing in gps mode).
  //
  // Drawn smoothly: the target is the server's progress plus the time since that
  // reply (real-time / demo modes), and the drawn value follows the target
  // continuously. It never restarts from an older value when a new reply comes
  // in (that made the ambulance twitch back every 2 s), and it only moves
  // backwards when the server really moved it back (a GPS correction).
  const smooth = new Map();   // emergency id -> {v: drawn progress, t: when}
  function liveProgress(id, live) {
    const now = Date.now();
    let target = live.progress;
    if ((LIVE.mode === 'simulated' || LIVE.mode === 'realtime') && live.estimated !== false && live.phase < 2) {
      target += (now - LIVE.at) / 1000 / LIVE.spm;
    }
    const s = smooth.get(id);
    if (!s) { smooth.set(id, {v: target, t: now}); return target; }
    const dt = Math.min(1, Math.max(0, (now - s.t) / 1000));
    s.t = now;
    const gap = target - s.v;
    if (Math.abs(gap) > 0.5) s.v += gap * (1 - Math.exp(-dt * 3));   // a real correction: glide there
    else s.v = target;                                               // normal motion: exactly on time
    for (const k of smooth.keys()) if (!LIVE.trips.has(k)) smooth.delete(k);
    return s.v;
  }

  /* ---- 2. trips follow the server's live position ---- */
  const plainTripOf = tripOf;
  window.tripOf = function (e) {
    const live = LIVE.trips.get(e.id);
    if (!live) return plainTripOf(e);          // server without live tracking: old behaviour
    const legs = [];
    const add = (ids, phase) => {
      for (let i = 0; i + 1 < ids.length; i++) legs.push({a: ids[i], b: ids[i + 1], min: roadMinutes(ids[i], ids[i + 1]), phase});
    };
    add(e.routeToScene, 0);
    add(e.routeToHospital, 1);
    const toScene = legs.filter(l => l.phase === 0).reduce((s, l) => s + l.min, 0);
    const total = legs.reduce((s, l) => s + l.min, 0);
    const elapsed = Math.max(0, Math.min(total, liveProgress(e.id, live)));
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
    const done = elapsed >= total - 1e-9;
    return {legs, total, toScene, elapsed, remaining: Math.max(0, total - elapsed), pos, near, idx, done,
            phase: done ? 2 : elapsed < toScene ? 0 : 1,
            chain: e.routeToScene.concat(e.routeToHospital.slice(1)), live};
  };

  /* ---- 3. demo speed is a server setting (tracking page, simulated mode) ---- */
  if (typeof setSpeed === 'function') {
    window.setSpeed = function (v) {
      window.request('POST', '/api/tracking/speed', {msPerMinute: v}).catch(() => {});
    };
  }

  /* ---- 4. the dashboard keeps itself up to date ---- */
  if (!onTrackPage && typeof renderAll === 'function') {
    setInterval(async () => {
      try {
        if (!API || !TOKEN || !S || inFlight || document.hidden) return;
        const r = await window.request('GET', '/api/session');
        if (r && r.res.status === 200 && r.data && r.data.state) { S = r.data.state; renderAll(); }
      } catch (e) { /* server busy or gone: the next tick tries again */ }
    }, 2000);
  }

  /* ---- 5. "LIVE" badge + mode switch ---- */
  const chip = document.createElement('span');
  chip.className = 'chip live-chip';
  chip.innerHTML = '<i class="live-dot"></i><span></span>';
  const modeSel = document.createElement('select');
  modeSel.className = 'live-mode';
  modeSel.setAttribute('aria-label', 'Position source');
  modeSel.innerHTML = '<option value="realtime">Real time</option><option value="gps">GPS only</option><option value="simulated">Simulated (demo speed)</option>';
  modeSel.title = 'Admin only. Real time: ambulances move at real road speed from dispatch, crew GPS corrects them. GPS only: they move only on crew GPS. Simulated: fast demo.';
  modeSel.onchange = async () => {
    const r = await window.request('POST', '/api/tracking/mode', {mode: modeSel.value}).catch(() => null);
    if (r && r.data && r.data.message && typeof say === 'function') say(r.data.message, !r.data.ok);
  };
  const where = onTrackPage ? document.querySelector('#view .ph') : document.querySelector('#mapview .mh');
  if (where) { where.appendChild(chip); where.appendChild(modeSel); }
  const text = chip.querySelector('span');

  function describe(t) {
    const age = Math.round(t.secondsSinceFix + (Date.now() - LIVE.at) / 1000);
    if (t.source === 'simulated') return {s: 'SIMULATED · not a real position', cls: 'sim'};
    if (t.source === 'real-time estimate') {
      const fixes = (t.gpsFixes || 0) + (t.reports || 0);
      return {s: fixes ? `REAL TIME · moving from the last crew fix (${age} s ago)`
                       : 'REAL TIME · moving at road speed since dispatch', cls: 'rt'};
    }
    if (t.source === 'waiting for gps') return {s: `Waiting for GPS from the crew · ${age} s since dispatch`, cls: 'wait'};
    if (t.source === 'junction report') return {s: `Crew reported a junction ${age} s ago`, cls: age > STALE_S * 4 ? 'stale' : ''};
    // gps
    const g = t.gps || {};
    let s = `GPS ${age} s ago`;
    if (g.accuracy) s += ` · ±${Math.round(g.accuracy)} m`;
    if (g.speed != null) s += ` · ${Math.round(g.speed * 3.6)} km/h`;
    if (g.offRoute) return {s: s + ` · ${Math.round(g.offRouteMetres)} m OFF the planned route`, cls: 'stale'};
    if (age > STALE_S) return {s: s + ' · signal lost?', cls: 'stale'};
    return {s, cls: ''};
  }

  setInterval(() => {
    chip.hidden = !LIVE.ok;
    const admin = typeof me !== 'undefined' && me && me.role === 'admin';
    modeSel.hidden = !LIVE.ok || !admin;
    if (document.activeElement !== modeSel) modeSel.value = LIVE.mode;
    document.body.classList.toggle('live-sim', LIVE.mode === 'simulated');
    // demo speed only means something in simulated mode
    const speed = document.getElementById('speed');
    if (speed && speed.closest('label')) speed.closest('label').hidden = LIVE.ok && LIVE.mode !== 'simulated';
    if (!LIVE.ok) return;
    let d;
    if (onTrackPage) {
      const t = LIVE.trips.get(emId);
      d = t ? describe(t) : {s: LIVE.mode === 'simulated' ? 'SIMULATED' : 'LIVE', cls: LIVE.mode === 'simulated' ? 'sim' : ''};
      chip.title = 'Position from the server. Real time: it moves at real road speed from dispatch, and a crew GPS fix (crew.html) corrects it.';
    } else {
      const ts = Array.from(LIVE.trips.values());
      const n = ts.length;
      const waiting = ts.filter(t => t.source === 'waiting for gps').length;
      d = LIVE.mode === 'simulated'
        ? {s: `SIMULATED · ${n} ambulance${n === 1 ? '' : 's'} on a call`, cls: 'sim'}
        : LIVE.mode === 'realtime'
        ? {s: `REAL TIME · ${n} ambulance${n === 1 ? '' : 's'} on a call`, cls: 'rt'}
        : {s: `LIVE GPS · ${n} on a call` + (waiting ? ` · ${waiting} waiting for GPS` : ''), cls: waiting ? 'wait' : ''};
      chip.title = 'Positions come from the server: crew GPS fixes in Real GPS mode.';
    }
    text.textContent = d.s;
    chip.className = 'chip live-chip' + (d.cls ? ' ' + d.cls : '');
    chip.hidden = false;
  }, 500);
})();
