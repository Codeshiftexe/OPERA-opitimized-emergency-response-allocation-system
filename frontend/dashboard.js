/* ==========================================================================
   OPERA - live counters at the top of the dashboard (dashboard.js)

   Display only: every second it reads the engine state that opera.js already
   keeps (S) and shows five numbers: emergencies waiting, critical (cardiac)
   ones waiting, ambulances on a call, ambulances free and hospital beds free.
   A number that changes gives a short "bump" so the eye catches it.
   ========================================================================== */
(function () {
  'use strict';
  const box = document.getElementById('kpis');
  if (!box) return;

  const TILES = [
    {k: 'wait', icon: 'urgent',         label: 'Waiting'},
    {k: 'crit', icon: 'heartbeat',      label: 'Critical waiting'},
    {k: 'call', icon: 'ambulance',      label: 'On a call'},
    {k: 'free', icon: 'steering-wheel', label: 'Ambulances free'},
    {k: 'beds', icon: 'bed',            label: 'Beds free'}
  ];
  box.innerHTML = TILES.map(t => `
    <div class="kpi kpi-${t.k}">
      <span class="kpi-ic"><i class="ti ti-${t.icon}" aria-hidden="true"></i></span>
      <span class="kpi-tx"><b data-v="${t.k}">–</b><span class="kpi-l">${t.label}</span><small data-s="${t.k}"></small></span>
    </div>`).join('');

  const last = {};
  function set(k, value, sub) {
    const v = box.querySelector(`[data-v="${k}"]`);
    if (last[k] !== value) {
      v.textContent = value;
      if (last[k] !== undefined) {           // replay the bump animation
        const tile = v.closest('.kpi');
        tile.classList.remove('bump');
        void tile.offsetWidth;
        tile.classList.add('bump');
      }
      last[k] = value;
    }
    box.querySelector(`[data-s="${k}"]`).textContent = sub;
  }

  function tick() {
    if (typeof S === 'undefined' || !S) return;
    const em = S.emergencies || [], amb = S.ambulances || [], hs = S.hospitals || [];
    const waiting = em.filter(e => e.status === 'pending');
    const critical = waiting.filter(e => e.type === 'Cardiac');
    const onCall = em.filter(e => e.status === 'assigned').length;
    const free = amb.filter(a => a.status === 'available').length;
    const inService = amb.filter(a => a.status !== 'offline').length;
    const bedsFree = hs.reduce((s, h) => s + h.availableBeds, 0);
    const bedsAll = hs.reduce((s, h) => s + h.totalBeds, 0);
    set('wait', waiting.length, waiting.length ? 'in triage order' : 'queue is clear');
    set('crit', critical.length, critical.length ? 'cardiac, served first' : 'none right now');
    set('call', onCall, onCall ? 'moving live on the map' : 'no active trips');
    set('free', free, `of ${inService} in service`);
    set('beds', bedsFree, `of ${bedsAll} in ${hs.length} hospitals`);
    box.classList.toggle('alert', critical.length > 0);
  }
  setInterval(tick, 800);
  tick();
})();

/* ==========================================================================
   Scrolling bars and full-page views

   The dashboard shows the emergency queue and the active dispatches as two
   slim bars whose items scroll past continuously (pause on hover). Clicking
   a bar opens a full-page view with the complete list. The complete lists are
   the original cards that opera.js keeps up to date (#queue, #active); they
   live in a hidden store and are moved into the full-page view while it is
   open, so every button in them keeps working exactly as before.
   ========================================================================== */
(function () {
  'use strict';
  const bars = document.getElementById('bars'), store = document.getElementById('fvStore');
  const op = document.getElementById('op');
  if (!bars || !store || !op) return;
  const qCard = store.querySelector('.q-card'), aCard = store.querySelector('.act-card');
  const esc = s => String(s == null ? '' : s).replace(/[&<>"']/g,
    c => ({'&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;'}[c]));

  /* ---------- the two bars ---------- */
  bars.innerHTML = ['queue', 'active'].map(k => `
    <button type="button" class="bar bar-${k}" data-open="${k}"
            aria-label="${k === 'queue' ? 'Emergency queue' : 'Active dispatches'}: open the full view">
      <span class="bar-h"><i class="ti ti-${k === 'queue' ? 'urgent' : 'ambulance'}" aria-hidden="true"></i>
        <b>${k === 'queue' ? 'Emergency queue' : 'Active dispatches'}</b><span class="bar-n" data-n="${k}">0</span></span>
      <span class="bar-v"><span class="bar-track" data-track="${k}"></span></span>
      <span class="bar-go">Full view <i class="ti ti-arrows-maximize" aria-hidden="true"></i></span>
    </button>`).join('');

  const nodeName = id => (S && S.nodes[id] ? S.nodes[id].name : 'node ' + id);
  const hospName = id => { const h = S.hospitals.find(x => x.id === id); return h ? h.name : 'hospital'; };
  const sev = t => 'sev-' + String(t).toLowerCase();

  function queueItems() {
    return S.queue.map(id => S.emergencies.find(e => e.id === id)).filter(Boolean).map((e, i) =>
      `<span class="it ${sev(e.type)}"><i class="dot"></i><b>#${e.id} ${esc(e.type)}</b> ${esc(nodeName(e.node))}` +
      `<em>${i === 0 ? 'next' : 'P' + Math.round(e.priority)}</em></span>`);
  }
  function activeItems() {
    return S.emergencies.filter(e => e.status === 'assigned').map(e => {
      let stage = 'on the way';
      try { const t = tripOf(e); stage = t.phase === 0 ? 'to patient' : t.phase === 1 ? 'to hospital' : 'arrived'; } catch (x) { /* no trip yet */ }
      return `<span class="it ${sev(e.type)}"><i class="dot"></i><b>A${e.ambulance}</b> #${e.id} ${esc(e.type)}` +
             `<em>${stage}</em> <span class="to">→ ${esc(hospName(e.hospital))}</span></span>`;
    });
  }

  const sig = {};
  function fill(k, items, empty) {
    bars.querySelector(`[data-n="${k}"]`).textContent = items.length;
    const html = items.length ? items.join('') : `<span class="it none">${empty}</span>`;
    if (sig[k] === html) return;                     // unchanged: keep the animation running
    sig[k] = html;
    const track = bars.querySelector(`[data-track="${k}"]`);
    track.classList.remove('scroll');
    track.innerHTML = html;
    requestAnimationFrame(() => {                    // scroll only when it does not fit
      const view = track.parentElement;
      if (items.length && track.scrollWidth > view.clientWidth) {
        track.innerHTML = `<span class="seq">${html}</span><span class="seq" aria-hidden="true">${html}</span>`;
        track.style.setProperty('--dur', Math.max(12, track.scrollWidth / 2 / 45) + 's');
        track.classList.add('scroll');
      }
    });
  }
  function tick() {
    if (typeof S === 'undefined' || !S) return;
    fill('queue', queueItems(), 'Queue is clear');
    fill('active', activeItems(), 'No ambulance on a call');
  }
  setInterval(tick, 1000);
  tick();

  /* ---------- full-page view ---------- */
  const fv = document.createElement('div');
  fv.className = 'fv';
  fv.hidden = true;
  fv.innerHTML = `<div class="fv-bg" data-close></div>
    <section class="fv-panel" role="dialog" aria-modal="true" aria-labelledby="fvT">
      <header class="fv-head"><h2 id="fvT"></h2>
        <button type="button" class="b fv-x" data-close aria-label="Close the full view"><i class="ti ti-x" aria-hidden="true"></i>Close</button>
      </header>
      <div class="fv-body"></div>
    </section>`;
  op.appendChild(fv);
  const body = fv.querySelector('.fv-body');
  let shown = null, back = null;

  function open(which) {
    if (shown) close(true);
    shown = which === 'queue' ? qCard : aCard;
    back = document.activeElement;
    const n = bars.querySelector(`[data-n="${which}"]`).textContent;
    fv.querySelector('#fvT').textContent = which === 'queue'
      ? `Emergency queue · ${n} waiting, in triage order`
      : `Active dispatches · ${n} ambulance${n === '1' ? '' : 's'} on a call`;
    body.appendChild(shown);
    fv.hidden = false;
    document.body.classList.add('fv-open');
    requestAnimationFrame(() => fv.classList.add('in'));
    fv.querySelector('.fv-x').focus();
  }
  function close(now) {
    if (!shown) return;
    const card = shown;
    shown = null;
    fv.classList.remove('in');
    const done = () => { store.appendChild(card); fv.hidden = true; document.body.classList.remove('fv-open'); if (back) back.focus(); };
    if (now === true) done(); else setTimeout(done, 220);
  }

  bars.addEventListener('click', e => { const b = e.target.closest('[data-open]'); if (b) open(b.dataset.open); });
  fv.addEventListener('click', e => { if (e.target.closest('[data-close]')) close(); });
  document.addEventListener('keydown', e => { if (e.key === 'Escape' && shown) close(); });
  // choosing an emergency in the full queue shows its plan on the dashboard
  qCard.addEventListener('click', e => { if (shown === qCard && e.target.closest('[data-f]')) setTimeout(close, 60); });
  // the counter tiles open the matching list too
  const kpis = document.getElementById('kpis');
  if (kpis) kpis.addEventListener('click', e => {
    const t = e.target.closest('.kpi');
    if (!t) return;
    if (t.classList.contains('kpi-wait') || t.classList.contains('kpi-crit')) open('queue');
    else if (t.classList.contains('kpi-call')) open('active');
  });
})();

/* ==========================================================================
   Dispatch desk banner: live clock and a one-line status on the right.
   Clicking the title still expands the description (opera.js).
   ========================================================================== */
(function () {
  'use strict';
  const hero = document.querySelector('#op .hero');
  if (!hero) return;
  const live = document.createElement('div');
  live.className = 'hero-live';
  live.innerHTML = '<span class="hl-dot" aria-hidden="true"></span><span class="hl-s"></span><span class="hl-t"></span>';
  hero.appendChild(live);
  const t = live.querySelector('.hl-t'), st = live.querySelector('.hl-s');
  function tick() {
    t.textContent = new Date().toLocaleTimeString([], {hour: '2-digit', minute: '2-digit', second: '2-digit'});
    if (typeof S !== 'undefined' && S) {
      const w = S.queue.length, c = S.emergencies.filter(e => e.status === 'assigned').length;
      st.textContent = `${w} waiting · ${c} on a call`;
    }
  }
  setInterval(tick, 1000);
  tick();
})();

/* ==========================================================================
   Motion and interaction (display only)
     - counters count up, and each draws a live mini-graph of its last minutes
     - a soft glow follows the cursor over cards; clicks ripple
     - a radar sweep turns over the live map preview
     - a pop-up for every new event in the activity log
     - "Dispatch next" pulses when cases wait and auto-dispatch is off
   All of it is switched off for users who ask their device to reduce motion.
   ========================================================================== */
(function () {
  'use strict';
  const op = document.getElementById('op');
  if (!op) return;
  const calm = window.matchMedia && window.matchMedia('(prefers-reduced-motion: reduce)').matches;

  /* ---- counters: count-up and live mini-graphs ---- */
  const kpis = document.getElementById('kpis');
  const hist = {};
  if (kpis) {
    kpis.querySelectorAll('.kpi').forEach(tile => {
      const k = tile.className.match(/kpi-(\w+)/)[1];
      hist[k] = [];
      tile.insertAdjacentHTML('beforeend',
        '<svg class="kpi-spark" viewBox="0 0 100 30" preserveAspectRatio="none" aria-hidden="true">' +
        '<path class="area"/><polyline class="line"/></svg>');
    });
    const counted = new Set();
    setInterval(() => {
      kpis.querySelectorAll('[data-v]').forEach(b => {
        const k = b.dataset.v, v = parseInt(b.textContent, 10);
        if (isNaN(v)) return;
        if (!counted.has(k) && !calm) {                  // first value: count up from 0
          counted.add(k);
          const t0 = performance.now();
          const step = now => {
            const f = Math.min(1, (now - t0) / 700);
            if (b.textContent === String(v) || b.dataset.counting) b.textContent = Math.round(v * (1 - Math.pow(1 - f, 3)));
            if (f < 1) requestAnimationFrame(step); else { b.textContent = v; delete b.dataset.counting; }
          };
          b.dataset.counting = '1';
          requestAnimationFrame(step);
        }
        const h = hist[k];
        h.push(v);
        if (h.length > 40) h.shift();
        const tile = b.closest('.kpi'), max = Math.max(1, ...h);
        if (h.length < 2) return;                        // a graph needs two readings
        const pts = h.map((x, i) => `${(i / (h.length - 1)) * 100},${28 - (x / max) * 24}`).join(' ');
        tile.querySelector('.kpi-spark .line').setAttribute('points', pts);
        tile.querySelector('.kpi-spark .area').setAttribute('d', `M0,30 L${pts.replace(/ /g, ' L')} L100,30 Z`);
      });
    }, 1500);
  }

  if (calm) return;

  /* ---- glow that follows the cursor ---- */
  op.addEventListener('pointermove', e => {
    const el = e.target.closest('.card, .kpi, .bar, .mapbtn, .mini2 .big');
    if (!el) return;
    const r = el.getBoundingClientRect();
    el.style.setProperty('--mx', (e.clientX - r.left) + 'px');
    el.style.setProperty('--my', (e.clientY - r.top) + 'px');
  });

  /* ---- ripple on click ---- */
  op.addEventListener('pointerdown', e => {
    const el = e.target.closest('button.pr, button.tl, .bar, .kpi, .mapbtn, .mini2 .big');
    if (!el) return;
    const r = el.getBoundingClientRect(), d = Math.max(r.width, r.height);
    const s = document.createElement('span');
    s.className = 'ripple';
    s.style.cssText = `width:${d}px;height:${d}px;left:${e.clientX - r.left - d / 2}px;top:${e.clientY - r.top - d / 2}px`;
    el.appendChild(s);
    setTimeout(() => s.remove(), 650);
  });

  /* ---- radar sweep over the map preview ---- */
  const mini = document.getElementById('miniMap');
  if (mini && !mini.parentElement.classList.contains('radar')) {
    const wrap = document.createElement('span');
    wrap.className = 'radar';
    mini.parentElement.insertBefore(wrap, mini);
    wrap.appendChild(mini);
  }

  /* ---- pop-ups for new events, and the pulsing dispatch button ---- */
  const toasts = document.createElement('div');
  toasts.className = 'toasts';
  toasts.setAttribute('aria-hidden', 'true');      // the log itself is the accessible record
  op.appendChild(toasts);
  let lastLog = null;
  const kind = t => /auto-dispatch/i.test(t) ? ['auto', 'bolt'] : /dispatched|sent to/i.test(t) ? ['go', 'ambulance']
    : /registered/i.test(t) ? ['new', 'urgent'] : /deliver|handed over|discharg/i.test(t) ? ['ok', 'circle-check']
    : /undo/i.test(t) ? ['undo', 'arrow-back-up'] : ['info', 'info-circle'];
  setInterval(() => {
    if (typeof S === 'undefined' || !S) return;
    const disp = document.getElementById('disp'), auto = document.querySelector('#op .adx.on');
    if (disp) disp.classList.toggle('attn', S.queue.length > 0 && !auto);
    const log = S.log || [];
    const head = log.length ? log[0].text + '|' + log.length : '';
    if (lastLog === null) { lastLog = head; return; }   // nothing to announce on first load
    if (head === lastLog || !log.length) return;
    lastLog = head;
    const [k, icon] = kind(log[0].text);
    const t = document.createElement('div');
    t.className = 'toast t-' + k;
    t.innerHTML = `<i class="ti ti-${icon}" aria-hidden="true"></i><span></span>`;
    t.querySelector('span').textContent = log[0].text;
    toasts.prepend(t);
    while (toasts.children.length > 3) toasts.lastChild.remove();
    setTimeout(() => t.classList.add('out'), 4200);
    setTimeout(() => t.remove(), 4700);
  }, 700);
})();

/* ==========================================================================
   Tabs, loading screen, tab pages, Register confirmation, 3D map side panels

   The dashboard shows only the banner, the live counters and one tab per part
   of the desk. Clicking a tab plays a short ambulance animation and opens the
   page. The panels are the original ones (opera.js keeps them up to date);
   they are moved from the hidden store into the tab page and back.
   ========================================================================== */
(function () {
  'use strict';
  const op = document.getElementById('op'), tabs = document.getElementById('tabs');
  const store = document.getElementById('fvStore');
  if (!op || !tabs || !store) return;
  const $q = sel => store.querySelector(sel) || document.querySelector(sel);
  const esc = s => String(s == null ? '' : s).replace(/[&<>"']/g,
    c => ({'&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;'}[c]));

  /* ---------- the tabs ---------- */
  const TABS = [
    {k: 'register', icon: 'urgent',         c: '#E5352B', t: 'Register emergency', show: ['.reg']},
    {k: 'queue',    icon: 'list-numbers',   c: '#F59E0B', t: 'Emergency queue',    show: ['.q-card']},
    {k: 'active',   icon: 'ambulance',      c: '#2563EB', t: 'Active dispatches',  show: ['.act-card']},
    {k: 'response', icon: 'route',          c: '#6366F1', t: 'Response plan',      show: ['.resp-card']},
    {k: 'map',      icon: 'map-2',          c: '#0B3A66', t: 'Live 3D city map',   click: '#seeMap'},
    {k: 'amb',      icon: 'steering-wheel', c: '#0EA5A4', t: 'Ambulances',         click: '#bAmb'},
    {k: 'beds',     icon: 'building-hospital', c: '#16A37A', t: 'Hospitals & beds', show: ['.beds-card', '.mini2']},
    {k: 'log',      icon: 'history',        c: '#64748B', t: 'Activity log',       show: ['.logcard']}
  ];
  tabs.innerHTML = TABS.map((t, i) => `
    <button type="button" class="tab" data-tab="${t.k}">
      <span class="tab-tx"><b>${t.t}</b><small data-sub="${t.k}">&nbsp;</small></span>
      <span class="tab-go" aria-hidden="true">Open</span>
    </button>`).join('');

  // one live line under each tab
  function subs() {
    if (typeof S === 'undefined' || !S) return;
    const name = id => (S.nodes[id] ? S.nodes[id].name : 'node ' + id);
    const waiting = S.queue.map(id => S.emergencies.find(e => e.id === id)).filter(Boolean);
    const onCall = S.emergencies.filter(e => e.status === 'assigned');
    const free = S.ambulances.filter(a => a.status === 'available').length;
    const beds = S.hospitals.reduce((s, h) => s + h.availableBeds, 0);
    const sel = typeof focus !== 'undefined' && focus != null ? S.emergencies.find(e => e.id === focus) : null;
    const last = (S.log || [])[0];
    const auto = document.querySelector('#op .adx.on');
    const text = {
      register: auto ? 'Auto-dispatch on: new calls go out by themselves' : 'Auto-dispatch off: you send each case',
      queue: waiting.length ? `${waiting.length} waiting · next #${waiting[0].id} ${waiting[0].type}` : 'Queue is clear',
      active: onCall.length ? `${onCall.length} ambulance${onCall.length > 1 ? 's' : ''} on a call` : 'No ambulance on a call',
      response: sel ? `#${sel.id} ${sel.type} at ${name(sel.node)}` : 'Select an emergency to see its plan',
      map: `${S.nodes.length} junctions · ${S.roads.length} roads · ${onCall.length} route${onCall.length === 1 ? '' : 's'} on the map`,
      amb: `${free} of ${S.ambulances.length} available`,
      beds: `${beds} beds free in ${S.hospitals.length} hospitals`,
      log: last ? last.text : 'Nothing has happened yet'
    };
    for (const k in text) {
      const el = tabs.querySelector(`[data-sub="${k}"]`);
      if (el && el.textContent !== text[k]) el.textContent = text[k];
    }
    tabs.querySelector('[data-tab="queue"]').classList.toggle('hot', waiting.some(e => e.type === 'Cardiac'));
  }
  setInterval(subs, 1000);
  subs();
  setTimeout(() => { try { nextUp(); } catch (e) { /* not yet */ } }, 0);

  /* ---------- Next up: the one main action on the dashboard ---------- */
  const next = document.createElement('section');
  next.className = 'nextup';
  next.setAttribute('aria-live', 'polite');
  next.innerHTML = `<div class="nu-tx"><span class="nu-k"></span><b class="nu-t"></b><span class="nu-m"></span></div>
    <button type="button" class="nu-btn"></button>`;
  tabs.parentElement.insertBefore(next, tabs);
  let nextAction = null;
  next.querySelector('.nu-btn').addEventListener('click', () => { if (nextAction) nextAction(); });
  function nextUp() {
    if (typeof S === 'undefined' || !S) return;
    const name = id => (S.nodes[id] ? S.nodes[id].name : 'node ' + id);
    const waiting = S.queue.map(id => S.emergencies.find(e => e.id === id)).filter(Boolean);
    const free = S.ambulances.filter(a => a.status === 'available').length;
    const e = waiting[0];
    next.className = 'nextup ' + (e ? 'sev-' + e.type.toLowerCase() : 'clear');
    const set = (sel, v) => { const el = next.querySelector(sel); if (el.textContent !== v) el.textContent = v; };
    if (e) {
      const wait = Math.round(e.waitingMinutes || 0);
      set('.nu-k', 'Next up');
      set('.nu-t', `#${e.id} ${e.type} at ${name(e.node)}`);
      set('.nu-m', `Priority ${Math.round(e.priority)}, waiting ${wait} min of a ${e.targetMinutes} min target` +
        (waiting.length > 1 ? `, ${waiting.length - 1} more behind it` : '') +
        (free ? `, ${free} ambulance${free === 1 ? '' : 's'} free` : ', no ambulance free right now'));
      set('.nu-btn', 'Dispatch next');
      nextAction = () => { const d = document.getElementById('disp'); if (d) d.click(); };
    } else {
      set('.nu-k', 'Queue');
      set('.nu-t', 'No emergency is waiting');
      set('.nu-m', `New calls are ranked here as soon as they are registered. ${free} ambulance${free === 1 ? '' : 's'} free.`);
      set('.nu-btn', 'Register emergency');
      nextAction = () => open('register');
    }
  }
  setInterval(nextUp, 1000);

  /* ---------- loading screen: an ambulance drives across ---------- */
  const loader = document.createElement('div');
  loader.className = 'amb-loader';
  loader.hidden = true;
  loader.setAttribute('role', 'status');
  loader.innerHTML = `
    <div class="al-box">
      <div class="al-road"><span class="al-amb"><span class="al-lights"></span>
        <svg viewBox="0 0 64 32" aria-hidden="true"><rect x="2" y="6" width="40" height="18" rx="4" fill="#fff"/>
        <path d="M42 11h10l8 7v6H42z" fill="#fff"/><rect x="45" y="13" width="8" height="5" rx="1" fill="#9CC4F0"/>
        <rect x="18" y="10" width="4" height="10" fill="#E5352B"/><rect x="15" y="13" width="10" height="4" fill="#E5352B"/>
        <rect x="2" y="17" width="40" height="2.5" fill="#E5352B"/>
        <circle cx="14" cy="26" r="4.5" fill="#1F2A30"/><circle cx="50" cy="26" r="4.5" fill="#1F2A30"/></svg></span></div>
      <b class="al-t">Opening…</b>
    </div>`;
  op.appendChild(loader);
  const MOTION = !(window.matchMedia && window.matchMedia('(prefers-reduced-motion: reduce)').matches);
  function withLoader(title, then) {
    loader.querySelector('.al-t').textContent = 'Opening ' + title + '…';
    loader.hidden = false;
    requestAnimationFrame(() => loader.classList.add('in'));
    setTimeout(() => {
      then();
      loader.classList.remove('in');
      setTimeout(() => { loader.hidden = true; }, 250);
    }, MOTION ? 1100 : 150);
  }

  /* ---------- tab pages ---------- */
  const tabPage = document.createElement('section');
  tabPage.className = 'tabpage';
  tabPage.hidden = true;
  tabPage.setAttribute('aria-labelledby', 'tpT');
  tabPage.innerHTML = `
    <header class="tp-head">
      <button type="button" class="b tp-back"><i class="ti ti-arrow-left" aria-hidden="true"></i>Dashboard</button>
      <h2 id="tpT"></h2>
    </header>
    <div class="tp-body"></div>`;
  op.appendChild(tabPage);
  const body = tabPage.querySelector('.tp-body');
  let moved = [], current = null;

  function closePage() {
    moved.forEach(el => store.appendChild(el));
    moved = [];
    current = null;
    tabPage.classList.remove('in');
    tabPage.hidden = true;
    document.body.classList.remove('tp-open');
  }
  function showPage(tab) {
    closePage();
    current = tab.k;
    tabPage.style.setProperty('--c', tab.c);
    tabPage.querySelector('#tpT').textContent = tab.t;
    tab.show.forEach(sel => { const el = $q(sel); if (el) { body.appendChild(el); moved.push(el); } });
    tabPage.hidden = false;
    document.body.classList.add('tp-open');
    requestAnimationFrame(() => tabPage.classList.add('in'));
    window.scrollTo(0, 0);
  }
  function open(k) {
    const tab = TABS.find(t => t.k === k);
    if (!tab) return;
    withLoader(tab.t, () => {
      if (tab.click) { closePage(); const b = document.querySelector(tab.click); if (b) b.click(); }
      else showPage(tab);
    });
  }
  tabs.addEventListener('click', e => { const b = e.target.closest('[data-tab]'); if (b) open(b.dataset.tab); });
  tabPage.querySelector('.tp-back').addEventListener('click', closePage);
  document.addEventListener('keydown', e => { if (e.key === 'Escape' && current) closePage(); });
  // picking an emergency in the queue opens its response plan
  const qCard = $q('.q-card');
  if (qCard) qCard.addEventListener('click', e => {
    if (current === 'queue' && e.target.closest('[data-f]')) setTimeout(() => showPage(TABS.find(t => t.k === 'response')), 80);
  });
  // the counters open their lists too
  const kpis = document.getElementById('kpis');
  if (kpis) kpis.addEventListener('click', e => {
    const t = e.target.closest('.kpi');
    if (!t) return;
    if (t.classList.contains('kpi-wait') || t.classList.contains('kpi-crit')) open('queue');
    else if (t.classList.contains('kpi-call')) open('active');
    else if (t.classList.contains('kpi-free')) open('amb');
    else if (t.classList.contains('kpi-beds')) open('beds');
  });

  /* ---------- "Do you want to register this emergency?" ---------- */
  const ask = document.createElement('div');
  ask.className = 'confirm';
  ask.hidden = true;
  ask.innerHTML = `<div class="cf-bg"></div>
    <div class="cf-box" role="alertdialog" aria-modal="true" aria-labelledby="cfT" aria-describedby="cfB">
      <h3 id="cfT">Register this emergency?</h3>
      <p id="cfB"></p>
      <div class="cf-btns"><button type="button" class="b cf-no">No</button>
        <button type="button" class="b pr cf-yes">Yes, register</button></div>
    </div>`;
  op.appendChild(ask);
  let pass = false;
  const regBtn = () => document.getElementById('reg');
  function confirmRegister() {
    const ty = document.getElementById('type'), lo = document.getElementById('loc');
    const type = ty && ty.value ? ty.value : 'emergency';
    const where = lo && lo.selectedIndex >= 0 ? lo.options[lo.selectedIndex].text : '';
    ask.querySelector('#cfB').textContent = `Do you want to register a ${type} emergency at ${where}?`;
    ask.hidden = false;
    requestAnimationFrame(() => ask.classList.add('in'));
    ask.querySelector('.cf-yes').focus();
  }
  function closeAsk() { ask.classList.remove('in'); setTimeout(() => { ask.hidden = true; }, 180); }
  document.addEventListener('click', e => {           // runs before opera.js's own click handler
    if (pass || !e.target.closest('#reg')) return;
    e.preventDefault();
    e.stopPropagation();
    confirmRegister();
  }, true);
  ask.querySelector('.cf-yes').addEventListener('click', () => {
    closeAsk();
    pass = true;
    regBtn().click();                                  // now register for real
    pass = false;
  });
  ask.querySelector('.cf-no').addEventListener('click', () => { closeAsk(); regBtn().focus(); });
  ask.querySelector('.cf-bg').addEventListener('click', closeAsk);
  document.addEventListener('keydown', e => { if (e.key === 'Escape' && !ask.hidden) closeAsk(); });

  /* ---------- information inside the full-screen 3D map ----------
     Three live sections, each in its own colour: ambulances on a call (with a
     progress bar for their trip), the waiting queue (with a priority meter)
     and beds free per hospital (coloured by how full it is). Rows are updated
     in place, so only new rows slide in. */
  const stage = document.querySelector('#mapview .stage');
  if (stage) {
    stage.insertAdjacentHTML('beforeend', `
      <aside class="hud hud-l"><h4 class="h-call"><i class="ti ti-ambulance" aria-hidden="true"></i>On a call <span data-h="calln">0</span></h4>
        <div class="hlist" data-h="call"></div></aside>
      <aside class="hud hud-r"><h4 class="h-wait"><i class="ti ti-urgent" aria-hidden="true"></i>Waiting <span data-h="waitn">0</span></h4>
        <div class="hlist" data-h="wait"></div>
        <h4 class="h-beds"><i class="ti ti-building-hospital" aria-hidden="true"></i>Beds free <span data-h="bedsn">0</span></h4>
        <div class="hlist" data-h="beds"></div></aside>`);
    const box = k => stage.querySelector(`[data-h="${k}"]`);
    const setText = (el, v) => { if (el && el.textContent !== String(v)) el.textContent = v; };
    // keep one element per id; new ones slide in, missing ones fade out
    function sync(k, items, make, update, empty) {
      const list = box(k), seen = new Set();
      items.forEach((it, i) => {
        let row = list.querySelector(`[data-id="${it.id}"]`);
        if (!row) { row = make(it); row.dataset.id = it.id; row.classList.add('enter'); }
        if (list.children[i] !== row) list.insertBefore(row, list.children[i] || null);
        update(row, it, i);
        seen.add(String(it.id));
      });
      [...list.children].forEach(el => { if (!seen.has(el.dataset.id)) el.remove(); });
      let none = list.parentElement.querySelector(`.hnone[data-for="${k}"]`);
      if (!items.length) {
        if (!none) { none = document.createElement('div'); none.className = 'hnone'; none.dataset.for = k; list.after(none); }
        none.textContent = empty;
      } else if (none) none.remove();
    }
    function hudTick() {
      if (typeof S === 'undefined' || !S || document.getElementById('mapview').hidden) return;
      const name = id => (S.nodes[id] ? S.nodes[id].name : 'node ' + id);
      const hosp = id => { const h = S.hospitals.find(x => x.id === id); return h ? h.name : 'hospital'; };
      const onCall = S.emergencies.filter(e => e.status === 'assigned').map(e => {
        let st = 'on the way', eta = '', pct = 0;
        try { const t = tripOf(e); st = ['to patient', 'to hospital', 'arrived'][t.phase]; eta = t.done ? 'arrived' : Math.ceil(t.remaining) + ' min'; pct = t.total ? t.elapsed / t.total : 1; } catch (x) { /* no trip yet */ }
        return {id: e.id, e, st, eta, pct};
      });
      setText(box('calln'), onCall.length);
      sync('call', onCall, () => {
        const r = document.createElement('div');
        r.className = 'hrow';
        r.innerHTML = '<span class="hx"><b></b><small></small><span class="hbar"><i></i></span></span><em></em>';
        return r;
      }, (r, it) => {
        r.className = 'hrow sev-' + it.e.type.toLowerCase() + (r.classList.contains('enter') ? ' enter' : '');
        setText(r.querySelector('b'), `A${it.e.ambulance} · #${it.e.id} ${it.e.type}`);
        setText(r.querySelector('small'), `${it.st} → ${hosp(it.e.hospital)}`);
        setText(r.querySelector('em'), it.eta);
        r.querySelector('.hbar i').style.width = Math.round(it.pct * 100) + '%';
      }, 'No ambulance on a call');

      const waiting = S.queue.map(id => S.emergencies.find(e => e.id === id)).filter(Boolean).slice(0, 7)
        .map((e, i) => ({id: e.id, e, i}));
      setText(box('waitn'), S.queue.length);
      sync('wait', waiting, () => {
        const r = document.createElement('div');
        r.className = 'hrow';
        r.innerHTML = '<span class="hx"><b></b><small></small><span class="hbar"><i></i></span></span><em></em>';
        return r;
      }, (r, it) => {
        r.className = 'hrow sev-' + it.e.type.toLowerCase() + (it.i === 0 ? ' next' : '') + (r.classList.contains('enter') ? ' enter' : '');
        setText(r.querySelector('b'), `#${it.e.id} ${it.e.type}`);
        setText(r.querySelector('small'), `${name(it.e.node)} · waiting ${Math.round(it.e.waitingMinutes || 0)} min`);
        setText(r.querySelector('em'), it.i === 0 ? 'next' : 'P' + Math.round(it.e.priority));
        r.querySelector('.hbar i').style.width = Math.min(100, Math.round(it.e.priority / 1.2)) + '%';
      }, 'Queue is clear');

      const beds = S.hospitals.map(h => ({id: h.id, h}));
      setText(box('bedsn'), S.hospitals.reduce((t, h) => t + h.availableBeds, 0));
      sync('beds', beds, () => {
        const r = document.createElement('div');
        r.className = 'hbed';
        r.innerHTML = '<span></span><span class="hb"><i></i></span><b></b>';
        return r;
      }, (r, it) => {
        const f = it.h.totalBeds ? it.h.availableBeds / it.h.totalBeds : 0;
        r.className = 'hbed ' + (f > 0.4 ? 'ok' : f > 0.15 ? 'low' : 'full');
        setText(r.querySelector('span'), it.h.name);
        setText(r.querySelector('b'), `${it.h.availableBeds}/${it.h.totalBeds}`);
        r.querySelector('.hb i').style.width = Math.round(f * 100) + '%';
      }, '');
      stage.querySelectorAll('.enter').forEach(el => setTimeout(() => el.classList.remove('enter'), 600));
    }
    setInterval(hudTick, 1000);
    // fill the panels the moment the map opens, not a second later
    new MutationObserver(() => { if (!document.getElementById('mapview').hidden) hudTick(); })
      .observe(document.getElementById('mapview'), {attributes: true, attributeFilter: ['hidden']});
  }
})();
