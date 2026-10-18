OPERA UI  -  setup guide (version 5, connected to the C++ engine)
=================================================================

The page is now a front end for the C++ program opera_server. The sign-in
and every button are answered by the engine.

START HERE
1. Build opera_server once (see HOW_TO_RUN.md in the OPERA folder).
2. Start opera_server from the OPERA repository folder and leave it running.
3. Open the page in ONE of these ways:
     - go to http://localhost:8080
     - or in VS Code: right-click opera.html > "Open with Live Server"

The sign-in card says "Connected to the OPERA server" in green when it works.
If it says the server is not running, start opera_server: the page connects
by itself within a few seconds.

DEMO LOGINS (change them before any real use)
  admin       Admin@2026     admin: can also take ambulances offline/online
  operator1   Oper1@2026     operator
  operator2   Oper2@2026     operator
Type the captcha exactly as shown (capital or small letters both work).
After 5 wrong passwords a name is locked for 30 seconds.

FILES
  opera.html    the page (sign-in first, then the dispatch desk).
  opera.css     colours and layout.
  opera.js      finds the server, signs in, sends each action to /api/...,
                and redraws everything from the state the server sends back.
  track.html    live tracking page, opened automatically after a dispatch.
  track.js      moves the ambulance along the route the engine calculated.
  captcha.txt   captcha codes, one per line (unchanged).

Accounts now live in data/passwords.txt (the server checks them; the
browser can no longer read that file). City data lives in data/.
Edit those files and restart opera_server.

CHANGING PASSWORDS
On the sign-in page open "Make a passwords.txt line", fill it in, paste the
line into data/passwords.txt, delete the old line for that name, and
restart opera_server.

WHAT CHANGED IN VERSION 5
  - Connected to the C++ engine through a JSON API (routes in include/Api.hpp).
  - Sign-in is checked by the server; sessions survive a page reload.
  - Works from Live Server too, as long as opera_server is running.
  - Removed features the engine does not support yet: severity slider, triage
    details, notes, redo, the 10 second undo window, congestion toggle,
    add/delete node, add ambulance, driver/vehicle/hospital detail fields,
    operator activity page.
  - Added buttons for engine features that had none: Patient delivered and
    Discharge a patient.
  - Dispatch opens a live tracking webpage (track.html): the ambulance travels
    along the route the engine computed (1 second per road minute, speed
    adjustable). When it arrives, press "Confirm patient delivered".

DASHBOARD (version 7)
  - Top bar: OPERA brand, connection, your name and role, Sign out.
  - After "Dispatch next" the tracking page opens with an "Undo dispatch"
    button for 10 seconds. After that the dispatch can no longer be undone.
  - Right column also shows Active dispatches, Hospital beds and a live
    mini-map in the See map card.
  - The simulation clock buttons (+1 / +5 min) were removed, so waiting times
    and priority aging stay at zero.

DASHBOARD (version 6)
  - Top row: "Dispatch desk" (click the arrow to read what it does) and the
    activity log, which keeps scrolling; hover over it to pause.
  - Left: Register emergency / Dispatch next, and the emergency queue.
  - Right: small Ambulances and Hospitals buttons (their numbers replace the
    old top-bar counters), the Response panel, and See map at the bottom.

IF SOMETHING LOOKS WRONG
  "The OPERA server is not running"  ->  start opera_server.
  "Your session has ended"           ->  the server was restarted; sign in again.
  No icons, only plain symbols       ->  no internet for the icon font; still works.
