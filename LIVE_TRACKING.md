# Real-time tracking (live GPS)

## Tracking modes (an admin picks one in the selector beside the LIVE badge)

| Mode | What moves the ambulance | When to use it |
|---|---|---|
| **Real time** (default) | It starts moving **automatically at dispatch, at real road speed**: 1 road minute = 1 real minute, so a 19-minute route takes 19 minutes. When a crew phone sends GPS (or a junction report), the position jumps to that real point and keeps moving from there. | Always: no phone is needed to see the ambulance move, and a phone makes it exact. |
| **GPS only** | Only real fixes from the crew phone (`crew.html`). Nothing moves between fixes. | When every ambulance has a phone sending GPS and you want no estimate at all. |
| **Simulated (demo speed)** | Moves by itself at the fast demo speed (`/api/tracking/speed`). | Short presentations only. The badge says SIMULATED. |

The badge always says which one is on: **REAL TIME · moving at road speed since dispatch**, **REAL TIME ·
moving from the last crew fix**, **GPS 4 s ago**, or **SIMULATED**.
API: `POST /api/tracking/mode {"mode": "realtime" | "gps" | "simulated"}` (admin).

## What changed, and why

| Before | Now |
|---|---|
| The shipped `opera_server.exe` was built before `LiveTracker` existed, so the server sent no positions and every browser ran its own timer animation. | Rebuild the server (see below). Every screen shows the one position the server keeps. |
| `LiveTracker` moved each ambulance forward by itself every second (dead reckoning). Only a manual "Reached junction X" button came from the ambulance. | The crew phone sends its **real GPS position** every ~2 s. The server places it on the route. **Nothing moves between fixes.** |
| The server listened on `127.0.0.1` only, so no phone could reach it. | `--host 0.0.0.0` lets phones on the same network connect (or use a tunnel, below). |

The old self-moving behaviour still exists, but only as an explicit **Simulated (demo)** mode that an admin
switches on. The badge then turns purple and says **SIMULATED**, so a simulation is never shown as real data.

| Part | File |
|---|---|
| GPS map matching, modes | `include/LiveTracker.hpp`, `src/LiveTracker.cpp`, `include/Geo.hpp` |
| Junction coordinates (`GEOREF`, `GEO`) | `data/city_map.txt`, `include/CityLoader.hpp`, `src/CityLoader.cpp` |
| API routes, `tracking` part of every reply | `include/Api.hpp`, `src/Api.cpp`, `src/StateJson.cpp` (node `lat`/`lon`) |
| Listening on the network | `include/HttpServer.hpp`, `src/HttpServer.cpp`, `src/main.cpp` (`--host`) |
| Crew phone: GPS sharing, junction set-up | `frontend/crew.html`, `frontend/crew.js`, `frontend/crew.css` |
| Dashboard / tracking page | `frontend/live.js`, `frontend/live.css`, `frontend/track.html` (caption) |
| Tests | `tests/test_live_tracker.cpp` (87 checks), `tests/test_api_tracking.cpp` (39), `tests/test_loader.cpp` (65) |

## How it works

1. A dispatch starts a trip on the server: ambulance -> patient -> hospital along the engine's Dijkstra routes.
   In GPS mode the ambulance stays at its start and the badge says **Waiting for GPS** until the first fix.
2. The crew opens `crew.html` on a phone, signs in, picks its call and presses **Start sharing GPS**.
   The phone sends `POST /api/tracking/gps {"emergency":4,"lat":30.32,"lon":78.04,"accuracy":8,"speed":11}`
   about every 2 seconds. While sharing, the screen is kept awake (phones stop GPS in background tabs), and a
   parked phone is asked for a fresh fix every 5 s so the dashboard sees "still here" and not "signal lost?".
3. The server's `LiveTracker` turns the fix and the route's junctions into flat metres and projects the fix
   onto the nearest road segment of the route. That point's distance along the route, in road minutes, is the
   live progress, so the ETA is "road minutes still ahead from where the ambulance really is".
   - Fixes that would jump backwards, or further ahead than the ambulance can have driven since the last
     on-route fix (3x the road time + 1 min), are scored down. This keeps out-and-back routes on the same road
     from snapping to the wrong visit.
   - A fix within **40 m** of a junction counts as being at it, so "arrived" is actually reached.
   - A fix more than **max(120 m, 2 x its accuracy)** from the route is shown as "N m OFF the planned route" and
     does not move the ambulance.
   - A fix worse than **±250 m** is refused (HTTP 409) with a message on the crew screen.
4. Every server reply carries the positions; the dashboard and tracking page re-read every 2 s:
   ```json
   "tracking": {"mode": "gps", "geo": true, "secondsPerMinute": 1, "trips": [
     {"emergency": 4, "ambulance": 3, "progress": 6.2, "toScene": 7, "total": 14, "phase": 0,
      "source": "gps", "reports": 0, "secondsSinceFix": 1.8, "gpsFixes": 37,
      "gps": {"lat": 30.31, "lon": 78.03, "accuracy": 8, "speed": 11.2, "offRoute": false, "offRouteMetres": 12}}]}
   ```
   `source` is `gps`, `junction report`, `waiting for gps` or `simulated`.
5. The badge shows the fix's age, accuracy and speed. It turns amber for **signal lost?** (no fix for 15 s) or
   **OFF the planned route**. A new fix glides the marker over 0.8 s; that is display only.
6. On arrival the crew presses **Patient handed over** (`POST /api/emergencies/complete`).

Without GPS the crew can still press **No GPS? Report: reached <junction>** (`POST /api/tracking/report`).

## Setting it up

### 1. Rebuild the server (required)

The old `opera_server.exe` does not contain any of this. From the repository folder:

```
g++ -std=c++17 -Iinclude src/*.cpp -lws2_32 -o opera_server.exe      (Windows / MinGW)
g++ -std=c++17 -Iinclude src/*.cpp -o opera_server                   (Linux / macOS)
```

Check: the start-up message mentions `crew.html`. If it does not, you are running the old program.

### 2. Give the junctions real coordinates

GPS can only be placed on a route whose junctions have real latitude/longitude. `data/city_map.txt` ships with
a **placeholder** `GEOREF` line: it puts "Clock Tower" near Dehradun's Ghanta Ghar at 20 m per map unit, and
places every other junction from its x, y. Those other junctions are **not** at the real places of the same name.

For a real test, choose a short route you can drive or walk (the campus works), then:

1. Rename nodes / adjust roads in `city_map.txt` to match that area if you like.
2. Open `crew.html` on a phone (step 3), open **Set up junction coordinates**, stand at each junction, pick it
   and press **Use my current position**. Wait for ±10-20 m outdoors.
3. Paste the printed `GEO <id> <lat> <lon>` lines at the end of `city_map.txt` and restart the server.
   A `GEO` line overrides `GEOREF` for that node. You can delete `GEOREF` once every node has a `GEO` line.

### 3. Open the crew page on a phone

Browsers only give GPS to **secure** pages (`https://...` or `http://localhost`). A plain
`http://192.168.x.x:8080` page gets "permission denied" without even asking. Pick one:

| Way | Steps | Good for |
|---|---|---|
| **HTTPS tunnel** (recommended) | Run the server as usual, then `cloudflared tunnel --url http://localhost:8080` (or `ngrok http 8080`). Open the printed `https://...trycloudflare.com/crew.html` on the phone. | A real drive: works over mobile data. |
| **USB port forwarding** (Android + Chrome) | Phone on USB with USB debugging on. On the laptop open `chrome://inspect/#devices` > Port forwarding > `8080` -> `localhost:8080`. On the phone open `http://localhost:8080/crew.html`. | Testing at the desk. |
| **Chrome flag** (same Wi-Fi) | Start with `opera_server --host 0.0.0.0`. On the phone's Chrome open `chrome://flags/#unsafely-treat-insecure-origin-as-secure`, add `http://<laptop IP>:8080`, relaunch, open `http://<laptop IP>:8080/crew.html`. Allow port 8080 in the Windows firewall. | Same Wi-Fi only. |

The tunnel and `--host 0.0.0.0` put the sign-in page where others can reach it: change the demo passwords first.

### 4. Test without driving

- **Chrome DevTools** on the laptop: open `http://localhost:8080/crew.html`, DevTools > More tools > **Sensors** >
  Location > enter coordinates of a junction on the route. Change them step by step and watch the dashboard.
  This goes through the real GPS code path.
- **curl**, with a token from `/api/login`:
  ```
  curl -X POST http://localhost:8080/api/tracking/gps -H "Authorization: Bearer <token>" \
       -H "Content-Type: application/json" -d "{\"emergency\":1,\"lat\":30.3245,\"lon\":78.0414,\"accuracy\":8}"
  ```

### 5. Demo without a phone

An admin can pick **Simulated (demo)** in the selector beside the badge (map page or tracking page), or call
`POST /api/tracking/mode {"mode":"simulated"}`. The demo speed (`POST /api/tracking/speed`) only applies then.
Switch back to **Real time** afterwards. The server always starts in Real time mode.

## Honest limits

- **Roads are straight lines between junctions.** On a curved road the real position can be some distance from
  the straight segment; the 120 m tolerance absorbs most of that. Adding intermediate nodes on long curved roads
  makes matching tighter.
- **Only the planned route is tracked.** If the crew takes another way, the trip shows "off the planned route"
  and the ETA stays at the last on-route point. Re-routing from the live position is not implemented.
- **Positions are tracked only for ambulances on a call.** Idle ambulances stay at their node in the data file.
- **Anyone signed in can send fixes for any call.** A separate crew role, tied to one ambulance, is the right fix
  and is not done yet.
- **HTTP only.** The server itself has no TLS; HTTPS comes from the tunnel.
- **ETA uses the map's road minutes**, not live traffic. Measure it: log fixes on a known drive and compare the
  ETA shown with the real arrival time.
