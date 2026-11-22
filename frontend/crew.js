/* ==========================================================================
   OPERA - ambulance crew page (crew.html)

   The crew's side of REAL tracking. The crew signs in on a phone, picks its
   call and presses "Start sharing GPS". The phone's GPS (Geolocation API,
   watchPosition) is then sent to the server every ~2 s:

       POST /api/tracking/gps {"emergency": id, "lat", "lon", "accuracy", "speed"}

   The server's LiveTracker projects each fix onto the engine's route and every
   dashboard / tracking page shows the ambulance there. Nothing moves between
   fixes. The screen is kept awake (Wake Lock) while sharing, because phones stop
   GPS in background tabs.

   Fallback without GPS: "Reached <junction>" (POST /api/tracking/report).
   On arrival the crew confirms delivery (POST /api/emergencies/complete).

   Browsers only give GPS to secure pages: https://..., or http://localhost.
   See LIVE_TRACKING.md for how to open this page on a phone that way.
   ========================================================================== */
(function () {
  'use strict';
  const $ = id => document.getElementById(id);
  const esc = s => String(s == null ? '' : s).replace(/[&<>"']/g,
    c => ({'&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;'}[c]));
  let API = null, TOKEN = null, S = null, LIVE = null, liveAt = 0, busy = false, lastKey = null;

  const SEND_EVERY_MS = 2000;      // at most one fix every 2 s
  const MAX_FIX_AGE_MS = 10000;    // never send a fix older than this
  const REFRESH_AFTER_MS = 5000;   // a phone standing still may stop reporting: ask again after this
  const gps = {watch: null, last: null, lastSentAt: 0, sending: false, sent: 0, reply: '', bad: false, wake: null,
               tripId: null, asking: false};

  async function request(method, path, body) {
    const headers = {};
    if (TOKEN) headers['Authorization'] = 'Bearer ' + TOKEN;
    if (method !== 'GET') headers['Content-Type'] = 'application/json';
    const res = await fetch(API + path, {method, headers, cache: 'no-store',
      body: method === 'GET' ? undefined : JSON.stringify(body || {})});
    let data = null;
    try { data = await res.json(); } catch (e) { data = null; }
    if (data && data.state) S = data.state;
    if (data && data.tracking) { LIVE = data.tracking; liveAt = Date.now(); }
    return {res, data};
  }

  async function findServer() {
    const bases = [location.protocol.startsWith('http') ? location.origin : null, 'http://localhost:8080', 'http://127.0.0.1:8080'];
    for (const b of bases.filter(Boolean)) {
      try {
        const r = await fetch(b + '/api/ping', {cache: 'no-store'});
        const d = await r.json();
        if (d && d.opera) return b;
      } catch (e) { /* try the next one */ }
    }
    return null;
  }

  const say = (t, bad) => { $('cMsg').textContent = t || ''; $('cMsg').className = 'msg' + (bad ? ' bad' : ''); };

  // road minutes of a -> b, from the engine's roads
  function minutes(a, b) {
    let best = Infinity;
    for (const r of S.roads) if ((r.a === a && r.b === b) || (!r.oneWay && r.a === b && r.b === a)) best = Math.min(best, r.minutes);
    return best === Infinity ? 0 : best;
  }

  function myTrips() {
    return S ? S.emergencies.filter(e => e.status === 'assigned') : [];
  }
  function selectedTrip() {
    const trips = myTrips();
    return trips.find(x => String(x.id) === $('cTrip').value) || trips[0] || null;
  }

  /* ------------------------------------------------------------------
     GPS sharing
     ------------------------------------------------------------------ */
  function gpsWarn(text) { $('gWarn').hidden = !text; $('gWarn').textContent = text || ''; }

  function startGps() {
    if (!window.isSecureContext) {
      gpsWarn('This browser only shares GPS with secure pages. Open this page through https:// (a tunnel) or as ' +
              'http://localhost (USB port forwarding). See LIVE_TRACKING.md.');
      return;
    }
    if (!('geolocation' in navigator)) { gpsWarn('This browser has no GPS (Geolocation) support.'); return; }
    gpsWarn('');
    gps.watch = navigator.geolocation.watchPosition(onFix, onGpsError,
      {enableHighAccuracy: true, maximumAge: 0, timeout: 20000});
    keepAwake();
    renderGps();
  }

  function stopGps(reason) {
    if (gps.watch !== null) navigator.geolocation.clearWatch(gps.watch);
    gps.watch = null;
    gps.last = null;
    if (gps.wake) { gps.wake.release().catch(() => {}); gps.wake = null; }
    if (reason) gps.reply = reason;
    renderGps();
  }

  async function keepAwake() {
    try { if ('wakeLock' in navigator) gps.wake = await navigator.wakeLock.request('screen'); }
    catch (e) { /* not allowed (battery saver): GPS still works while the page is visible */ }
  }
  document.addEventListener('visibilitychange', () => {
    if (document.visibilityState === 'visible' && gps.watch !== null && !gps.wake) keepAwake();
  });

  function onGpsError(err) {
    const why = {1: 'GPS permission was refused. Allow location for this site in the browser settings.',
                 2: 'No GPS position available. Go outdoors or check that location is on.',
                 3: 'GPS is taking long to find a position…'}[err.code] || err.message;
    gpsWarn(why);
    if (err.code === 1) stopGps();
    renderGps();
  }

  function onFix(p) {
    gps.last = {lat: p.coords.latitude, lon: p.coords.longitude, accuracy: p.coords.accuracy,
                speed: p.coords.speed, at: p.timestamp || Date.now()};
    if (gps.bad === false) gpsWarn('');
    sendFix();
    renderGps();
  }

  // watchPosition only fires when the phone moves. Parked at the scene, ask for a
  // fresh fix now and then, so the dashboard sees "still here" and not "signal lost?".
  function refreshFix() {
    if (gps.watch === null || gps.asking) return;
    if (gps.last && Date.now() - gps.last.at < REFRESH_AFTER_MS) return;
    gps.asking = true;
    navigator.geolocation.getCurrentPosition(p => { gps.asking = false; onFix(p); },
      () => { gps.asking = false; },
      {enableHighAccuracy: true, maximumAge: 0, timeout: 10000});
  }

  // also called by a timer
  async function sendFix() {
    const e = selectedTrip();
    if (!gps.last || gps.sending || !e) return;
    const now = Date.now();
    if (now - gps.lastSentAt < SEND_EVERY_MS) return;
    if (now - gps.last.at > MAX_FIX_AGE_MS) return;      // old fix: wait for a fresh one
    gps.sending = true;
    gps.lastSentAt = now;
    gps.tripId = e.id;
    try {
      const body = {emergency: e.id, lat: gps.last.lat, lon: gps.last.lon, accuracy: gps.last.accuracy};
      if (typeof gps.last.speed === 'number' && gps.last.speed >= 0) body.speed = gps.last.speed;
      const r = await request('POST', '/api/tracking/gps', body);
      gps.bad = !(r.data && r.data.ok);
      gps.reply = r.data ? r.data.message : 'No reply (HTTP ' + r.res.status + ').';
      if (r.data && r.data.ok) gps.sent++;
      if (r.res.status === 401) { stopGps('Signed out: sign in again.'); }
    } catch (err) {
      gps.bad = true;
      gps.reply = 'Server not reachable. Fixes resume when it is back.';
    } finally {
      gps.sending = false;
      renderGps();
      render();
    }
  }

  function renderGps() {
    const on = gps.watch !== null;
    $('gToggle').textContent = on ? 'Stop sharing GPS' : 'Start sharing GPS';
    $('gToggle').classList.toggle('on', on);
    const st = $('gState');
    if (!on) { st.textContent = 'Off'; st.className = 'chip'; }
    else if (!gps.last) { st.textContent = 'Finding position…'; st.className = 'chip'; }
    else if (gps.bad) { st.textContent = 'Not used'; st.className = 'chip al'; }
    else { st.textContent = 'Sharing'; st.className = 'chip ok'; }
    const L = gps.last;
    const lines = [];
    if (L) {
      const age = Math.round((Date.now() - L.at) / 1000);
      lines.push(`Position ${L.lat.toFixed(6)}, ${L.lon.toFixed(6)} · ±${Math.round(L.accuracy)} m · ${age} s old` +
                 (typeof L.speed === 'number' && L.speed >= 0 ? ` · ${Math.round(L.speed * 3.6)} km/h` : ''));
    }
    if (on) lines.push(`Fixes accepted by the server: ${gps.sent}`);
    if (gps.reply) lines.push('Server: ' + gps.reply);
    $('gInfo').innerHTML = lines.map(l => `<div${gps.bad && l.startsWith('Server') ? ' class="bad"' : ''}>${esc(l)}</div>`).join('');
  }

  /* ------------------------------------------------------------------
     The call: progress, junction fallback, delivery
     ------------------------------------------------------------------ */
  function render() {
    if (!S) return;
    const trips = myTrips(), sel = $('cTrip'), keep = sel.value;
    const opts = trips.map(e => `<option value="${e.id}">#${e.id} ${esc(e.type)} · Ambulance ${e.ambulance}</option>`).join('');
    if (sel.innerHTML !== opts) { sel.innerHTML = opts; if (trips.some(e => String(e.id) === keep)) sel.value = keep; }
    renderCalibration();
    const e = selectedTrip();
    if (!e) {
      $('cInfo').textContent = '';
      $('cNow').innerHTML = '<h3>No call in progress</h3><p class="sub">When the dispatcher sends this ambulance, the call appears here.</p>';
      lastKey = null;
      if (gps.watch !== null && gps.tripId !== null) stopGps('The call has ended. GPS sharing stopped.');
      return;
    }
    const N = S.nodes, name = id => (N[id] ? N[id].name : 'node ' + id);
    const hosp = S.hospitals.find(h => h.id === e.hospital);
    $('cInfo').innerHTML = `Patient at <b>${esc(name(e.node))}</b>, then <b>${esc(hosp ? hosp.name : 'hospital')}</b>.`;

    const chain = e.routeToScene.concat(e.routeToHospital.slice(1));
    const cum = [0];
    for (let i = 1; i < chain.length; i++) cum.push(cum[i - 1] + minutes(chain[i - 1], chain[i]));
    const total = cum[cum.length - 1];
    const lt = LIVE && LIVE.trips.find(t => t.emergency === e.id);
    const sim = LIVE && LIVE.mode === 'simulated';
    const extra = sim && lt && lt.phase < 2 ? (Date.now() - liveAt) / 1000 / (LIVE.secondsPerMinute || 1) : 0;
    const progress = lt ? Math.min(total, lt.progress + extra) : 0;
    let next = cum.findIndex(c => c > progress + 1e-6);
    const arrived = next < 0;
    const route = chain.map((n, i) => `<span class="${i < next || arrived ? 'pd' : i === next ? 'nx' : ''}">${esc(name(n))}</span>`).join('');
    const pct = total > 0 ? Math.round(progress / total * 100) : 100;
    const source = !lt ? '' : {
      'gps': 'from your GPS',
      'junction report': 'from your last junction report',
      'waiting for gps': 'waiting for your GPS',
      'simulated': 'SIMULATED by the server (demo mode)'}[lt.source] || lt.source;
    const eta = Math.max(0, total - progress);
    const status = `${pct}% of the trip, ${source}. About ${eta.toFixed(1)} road min to the hospital.`;
    // rebuild the buttons only when the step changes (never between press and release)
    const key = e.id + '|' + next + '|' + arrived;
    if (key === lastKey) {
      $('cBar').style.width = pct + '%';
      $('cPct').textContent = status;
      return;
    }
    lastKey = key;
    $('cNow').innerHTML = `<div class="crew-step">
      <div class="crew-bar"><b id="cBar" style="width:${pct}%"></b></div>
      <div class="sub" id="cPct">${esc(status)}</div>
      ${arrived
        ? `<div class="crew-next">At the hospital<b>${esc(hosp ? hosp.name : '')}</b></div>
           <button class="b crew-done" id="cDone">Patient handed over (delivered)</button>`
        : `<div class="crew-next">Next junction<b>${esc(name(chain[next]))}</b></div>
           <button class="b" id="cReach" data-node="${chain[next]}">No GPS? Report: reached ${esc(name(chain[next]))}</button>
           ${next + 1 < chain.length ? `<button class="b" id="cSkip" data-node="${chain[chain.length - 1]}">We are at the hospital</button>` : ''}`}
      <div class="crew-route">${route}</div></div>`;
    const report = async node => {
      if (busy) return;
      busy = true;
      try {
        const r = await request('POST', '/api/tracking/report', {emergency: e.id, node});
        say(r.data ? r.data.message : 'No reply.', !(r.data && r.data.ok));
      } finally { busy = false; lastKey = null; render(); }
    };
    if ($('cReach')) $('cReach').onclick = () => report(+$('cReach').dataset.node);
    if ($('cSkip')) $('cSkip').onclick = () => report(+$('cSkip').dataset.node);
    if ($('cDone')) $('cDone').onclick = async () => {
      if (busy) return;
      busy = true;
      try {
        const r = await request('POST', '/api/emergencies/complete', {id: e.id});
        say(r.data ? r.data.message : 'No reply.', !(r.data && r.data.ok));
      } finally { busy = false; lastKey = null; render(); }
    };
  }

  /* ------------------------------------------------------------------
     Junction coordinates (prints GEO lines for city_map.txt)
     ------------------------------------------------------------------ */
  function renderCalibration() {
    const sel = $('calNode');
    if (!S || sel.options.length === S.nodes.length) return;
    sel.innerHTML = S.nodes.map(n => `<option value="${n.id}">${n.id} · ${esc(n.name)}</option>`).join('');
  }
  function calibrate() {
    if (!window.isSecureContext || !('geolocation' in navigator)) {
      $('calMsg').textContent = 'GPS is not available on this page (needs https:// or http://localhost).';
      return;
    }
    $('calMsg').textContent = 'Reading GPS… stay still for a few seconds.';
    navigator.geolocation.getCurrentPosition(p => {
      const id = +$('calNode').value;
      const line = `GEO ${id} ${p.coords.latitude.toFixed(6)} ${p.coords.longitude.toFixed(6)}`;
      const out = $('calOut');
      const lines = out.value.split('\n').filter(l => l && !l.startsWith(`GEO ${id} `));
      lines.push(line);
      out.value = lines.join('\n');
      $('calMsg').textContent = `Junction ${id}: ±${Math.round(p.coords.accuracy)} m.` +
        (p.coords.accuracy > 30 ? ' That is not precise: wait outdoors and press again.' : '');
    }, err => { $('calMsg').textContent = 'GPS error: ' + err.message; },
    {enableHighAccuracy: true, maximumAge: 0, timeout: 30000});
  }

  /* ------------------------------------------------------------------
     Sign-in and start
     ------------------------------------------------------------------ */
  async function signIn() {
    const name = $('cName').value.trim(), password = $('cPass').value;
    if (!name || !password) { $('cLoginMsg').textContent = 'Enter your name and password.'; return; }
    const r = await request('POST', '/api/login', {name, password});
    if (!r.data || !r.data.ok) { $('cLoginMsg').textContent = r.data ? r.data.message : 'No reply from the server.'; return; }
    TOKEN = r.data.token;
    try { sessionStorage.setItem('opera.crewToken', TOKEN); } catch (e) { /* blocked */ }
    enter();
  }

  function enter() {
    $('cLogin').hidden = true;
    $('cApp').hidden = false;
    if (!window.isSecureContext) {
      gpsWarn('GPS needs a secure page. This one is ' + location.origin + '. Use the https:// tunnel address, or ' +
              'http://localhost with USB port forwarding (LIVE_TRACKING.md). Junction reports still work.');
    }
    render();
    renderGps();
  }

  async function start() {
    API = await findServer();
    $('cConn').textContent = API ? 'Server connected' : 'Server not reachable';
    if (!API) return;
    $('cGo').onclick = signIn;
    $('cPass').onkeydown = ev => { if (ev.key === 'Enter') signIn(); };
    $('cTrip').onchange = () => { lastKey = null; render(); };
    $('gToggle').onclick = () => (gps.watch === null ? startGps() : stopGps('GPS sharing stopped.'));
    $('calGo').onclick = calibrate;
    try { TOKEN = sessionStorage.getItem('opera.crewToken'); } catch (e) { TOKEN = null; }
    if (TOKEN) {
      const r = await request('GET', '/api/session');
      if (r.res.status === 200) enter(); else TOKEN = null;
    }
    setInterval(async () => {
      if (!TOKEN || busy) return;
      try { await request('GET', '/api/session'); $('cConn').textContent = 'Server connected'; render(); }
      catch (e) { $('cConn').textContent = 'Server not reachable'; }
    }, 2000);
    setInterval(() => {
      if (gps.watch !== null) { refreshFix(); sendFix(); renderGps(); }
      if (TOKEN && !busy && !document.activeElement.closest('select')) render();
    }, 1000);
  }
  start();
})();
