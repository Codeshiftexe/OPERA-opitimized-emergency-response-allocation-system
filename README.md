# OPERA – Optimized Emergency Response Allocation

OPERA ranks emergencies, sends the nearest free ambulance along the fastest
route, and assigns the nearest hospital with a free bed, in one dispatch step.
A C++ engine makes every decision; a web interface shows them on a live city map.

## What it does

- **Register** an emergency (Cardiac, Trauma or Transfer) at a location on the map.
- **Rank** waiting emergencies in a triage queue: priority comes from the emergency
  type, and grows the longer a case waits, so low-priority cases are not starved.
- **Dispatch** the top emergency: Dijkstra's algorithm finds the nearest available
  ambulance and the nearest hospital with a free bed; a bed is reserved and the
  ambulance is marked busy.
- **Track** the dispatch on a live page where the ambulance follows the route the
  engine computed.
- **Undo** a dispatch within 10 seconds, **deliver** the patient, **discharge** a
  patient to free a bed, and take ambulances **offline/online** (admin only).
- **Sign in** with role-based accounts (admin, operator), checked by the server.

## Data structures and algorithms (hand-written)

| Structure / algorithm | Used for | Where |
|---|---|---|
| Templated binary min-heap | Triage queue, Dijkstra's frontier | `include/MinHeap.hpp` |
| Weighted graph (adjacency list) | City road network | `include/Graph.hpp`, `src/Graph.cpp` |
| Dijkstra with lazy deletion + path reconstruction | Fastest routes | `src/RoutingEngine.cpp` |
| Linked-list stack | Undo history of dispatches | `include/Stack.hpp` |
| Class hierarchy with polymorphism | Cardiac / Trauma / Transfer emergencies | `include/Emergency.hpp` |
| State machines | Ambulance (available / busy / offline), hospital beds | `src/Ambulance.cpp`, `src/Hospital.cpp` |
| SHA-256 | Salted password hashes for sign-in | `src/Sha256.cpp` |

## How it fits together

```
Browser (frontend/)  --HTTP + JSON-->  opera_server (C++)
  opera.html / .js                      HttpServer -> Api -> DispatchCoordinator
  track.html / .js   <--full state----  (engine: Registry, TriageQueue, Dijkstra, Stack)
```

The server loads `data/` at start (map, hospitals, ambulances, accounts), keeps
everything in memory, and serves the web page on `http://localhost:8080`.
The API routes are listed in [`include/Api.hpp`](include/Api.hpp).

## Repository layout

```
include/  src/  tests/   C++ engine, loader, sign-in, JSON API, HTTP server, tests
data/                    city_map.txt, hospitals.txt, ambulances.txt, passwords.txt
frontend/                opera.html, opera.css, opera.js, track.html, track.js, captcha.txt
```

## Run it

See [`HOW_TO_RUN.md`](HOW_TO_RUN.md). In short, from this folder:

```
g++ -std=c++17 -Iinclude src/*.cpp -lws2_32 -o opera_server.exe     (Windows; drop -lws2_32 elsewhere)
.\opera_server.exe
```

Then open `http://localhost:8080`.

## Testing

14 test programs, 778 checks, covering the data structures, routing, domain
classes, dispatch, loader, JSON, API and sign-in. All pass with C++14 and C++17.

## Planned

Not implemented yet: congestion-aware re-routing, a Bellman–Ford cross-check,
hospital ranking with sorting and binary search, a hand-written hash map, and
saving state to disk.

## Team Coreshift (T124)

Aditya Singh (lead) · Parth Pharasi · Vivek Goswami · Shourya Tomar
Mentor: Mr. Darshan Singh · Graphic Era (Deemed to be University), Dehradun
