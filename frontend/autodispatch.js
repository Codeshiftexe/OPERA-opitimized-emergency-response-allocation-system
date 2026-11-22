/* ==========================================================================
   OPERA - auto-dispatch switch on the dashboard

   Loaded by opera.html after opera.js. The decision logic lives in the C++
   engine (include/AutoDispatcher.hpp): when auto-dispatch is on, the server
   dispatches waiting emergencies by itself, most urgent first, whenever an
   emergency is registered or an ambulance or bed becomes free.

   This file only shows the switch and its state:
     - every server reply carries "autoDispatch": true/false, which we read by
       wrapping opera.js's request() function (opera.js itself is unchanged);
     - clicking the switch sends POST /api/autodispatch {"enabled": ...}.
   Any signed-in dispatcher (admin or operator) can switch it.
   ========================================================================== */
(function () {
  'use strict';

  const card = document.querySelector('#op .reg');
  if (!card || typeof request !== 'function' || typeof call !== 'function') return;

  let state = null;   // true / false once the server has told us

  // ---- the switch ----
  const row = document.createElement('div');
  row.className = 'adx';
  row.innerHTML = `
    <button type="button" class="adx-sw" role="switch" aria-checked="false" aria-labelledby="adxT">
      <span class="adx-knob"></span>
    </button>
    <div class="adx-tx">
      <b id="adxT">Auto-dispatch <span class="adx-st">off</span></b>
      <span id="adxD">Off: you send each emergency with Dispatch next.</span>
    </div>`;
  card.appendChild(row);
  const sw = row.querySelector('.adx-sw'), st = row.querySelector('.adx-st'), desc = row.querySelector('#adxD');

  function show() {
    const on = state === true;
    row.classList.toggle('on', on);
    sw.setAttribute('aria-checked', on);
    st.textContent = on ? 'on' : 'off';
    desc.textContent = on
      ? 'Fully automatic: every waiting emergency is sent at once, most urgent first. When an ambulance reaches the hospital it hands over the patient and takes the next waiting case by itself.'
      : 'Off: you send each emergency with Dispatch next. Switch on to let the engine dispatch everything automatically.';
    sw.disabled = state === null;
    sw.title = 'Switch auto-dispatch on or off';
  }

  // ---- read the state from every server reply ----
  const plainRequest = request;
  window.request = async function (method, path, body) {
    const r = await plainRequest(method, path, body);
    if (r && r.data && typeof r.data.autoDispatch === 'boolean' && r.data.autoDispatch !== state) {
      state = r.data.autoDispatch;
      show();
    }
    return r;
  };

  sw.addEventListener('click', async () => {
    if (state === null) return;
    await call('POST', '/api/autodispatch', {enabled: !state});
  });

  // the role is known only after sign-in; refresh the switch now and then
  setInterval(show, 1000);
  show();
})();
