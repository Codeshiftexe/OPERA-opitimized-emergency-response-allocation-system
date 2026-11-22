# OPERA 3D map (Three.js)

The dashboard's **See map** view and the **tracking page** show the city as a 3D model:
a city on a raised platform, with streets, sidewalks and street lamps, roundabouts at every junction,
buildings with lit windows, trees, a park, a lake, city traffic that drives the real road graph (keeping left,
with headlights at night), hospitals with glowing signs and helipads, and ambulances driving the engine's routes. **Night** (default) and **Day** modes, with a smooth change between them.

The 2D SVG map is still there: the **3D view** button above each map switches between them.

## Run it

1. Once, in this `frontend` folder: `npm install` (installs Three.js into `node_modules`).
2. Build and start the C++ server as usual (see `../HOW_TO_RUN.md`).
3. Open `http://localhost:8080`, sign in, and click **See map**, or dispatch an emergency.

Always open the page through the server or VS Code Live Server. Double-clicking the HTML file
does not work, because browsers block JavaScript modules on `file://` pages.

## Files

| File | What it does |
|---|---|
| `city3d.js` | The renderer: camera and controls, day/night lighting, sky and stars, glow (bloom), intro fly-in, keeping the scene in sync with the engine state, clicking |
| `city3d-world.js` | The city generated around the road graph: platform, streets, roundabouts, buildings, trees, street lamps, park, lake |
| `city3d-models.js` | Ambulance, hospital, incident beacon and route models, and their shaders (lit windows, flowing route light, light beam, water ripples) |
| `map3d.js` | Connects the renderer to the dashboard's **See map** view |
| `track3d.js` | Connects the renderer to the tracking page, with **Follow ambulance** |
| `opera3d.css` | Styles for the 3D map, its labels (day and night) and legend |
| `autodispatch.js`, `autodispatch.css` | The **Auto-dispatch** switch on the dashboard (see `../AUTO_DISPATCH.md`) |
| `live.js`, `live.css` | Real-time tracking: every map uses the server's live positions (see `../LIVE_TRACKING.md`) |
| `crew.html`, `crew.js`, `crew.css` | The ambulance crew's screen that reports real positions |
| `.gitignore` | Keeps `node_modules/` out of Git |

`opera.js` and `track.js` are **not changed**. The only edit to existing frontend files is one commented
block at the end of `opera.html` and `track.html` that loads Three.js and the new files.

## How it stays correct

The 3D files decide nothing. Every frame they read the same engine state the 2D map uses and call
`tripOf()`, which `live.js` connects to the server's live positions, so every screen (2D, 3D, progress bar,
ETA) shows the ambulance at the same place.
Routes and road times are the C++ engine's Dijkstra results.

The city around the graph is generated from the map itself with a seeded random generator, so the same
map always gives the same city. A node named "...Park..." gets a park, one named "...Lake..." gets a lake.

## Colours

| Emergency | Colour | Meaning |
|---|---|---|
| Cardiac | Red | Severe |
| Trauma | Amber | Serious |
| Transfer | Green | Not severe |

An ambulance on a call, its flowing route light and the patient's light beam take the emergency's colour.
Available ambulances are white, offline ones grey, and the part of a route already driven is grey.
A hospital's sign is green, and red (blinking at night) when it has no free bed.

## Controls

Drag to rotate, scroll to zoom, right-drag to pan. Buttons: zoom, show the whole city, ☾/☀ day and night,
⟳ slow cinematic orbit (dashboard), ◎ follow the ambulance (tracking page).
Clicking a junction, hospital or ambulance on the dashboard map opens its page.

## If something does not show

- **Map stays 2D:** the browser has no WebGL, or Three.js is missing (run `npm install`).
- **Slow on an old laptop:** switch to Day mode (less glow), or use the 2D view.
- **Icons missing:** the icon font loads from the internet; without it, plain symbols are used.
