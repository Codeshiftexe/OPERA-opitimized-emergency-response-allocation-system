# Auto-dispatch

With auto-dispatch **on**, nobody has to press **Dispatch next**: the engine dispatches waiting
emergencies by itself, most urgent first, as soon as an ambulance and a hospital bed are free.

## How to use it

On the dashboard, under **Register / Dispatch next**, there is an **Auto-dispatch** switch.
Any signed-in dispatcher (admin or operator) can turn it on or off. It starts **off** every time
the server starts.

## How it works

| Part | File |
|---|---|
| The module | `include/AutoDispatcher.hpp`, `src/AutoDispatcher.cpp` |
| Connected to the API | `include/Api.hpp`, `src/Api.cpp` (small, marked additions) |
| Dashboard switch | `frontend/autodispatch.js`, `frontend/autodispatch.css` |
| Tests | `tests/test_autodispatch.cpp` (27 checks), `tests/test_api_autodispatch.cpp` (23 checks) |

`AutoDispatcher::run()` keeps calling the existing `DispatchCoordinator::dispatchNext()` while an
emergency is waiting and an ambulance and a bed are free. It adds **no new decision rules**, so automatic
dispatches follow exactly the same rules as a manual click: triage order with aging, nearest available
ambulance and nearest hospital with a bed (Dijkstra), and check-then-commit, so nothing is double-booked.

It stops at the first case that has to wait. The queue is served strictly in triage order, so a
lower-priority case can never jump ahead of a more urgent one that is waiting for an ambulance.

The API runs it after every event that can make a dispatch possible:

- a new emergency is registered,
- a patient is delivered (an ambulance is free again),
- a patient is discharged (a bed is free again),
- an ambulance is brought back online,
- the clock moves forward,
- auto-dispatch is switched on (the waiting backlog is served at once).

**Fully automatic while it is on.** The server checks about once a second, even when no browser
is open (`Api::tick()`, called from the server loop). When an ambulance reaches its hospital, the
patient is handed over automatically, the ambulance becomes free, and it immediately takes the next
waiting emergency, most urgent first. With auto-dispatch off, deliveries are confirmed by the
dispatcher as before.

It deliberately does **not** run after an **undo**, otherwise the undone emergency would be
dispatched again immediately.

## API

```
POST /api/autodispatch   {"enabled": true}     any signed-in dispatcher
```

Every reply now also contains `"autoDispatch": true|false`. Automatic dispatches are written to the
activity log as `Auto-dispatch: Ambulance 3 sent to #5 at Mall Road, then Railway Hospital (...)`.
