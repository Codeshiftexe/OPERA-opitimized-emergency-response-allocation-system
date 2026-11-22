# Batch dispatch (backtracking)

**Dispatch next** serves one emergency: the most urgent case takes the ambulance nearest
to it. That is *greedy*, and greedy is not optimal across several waiting cases.

**Dispatch all waiting (optimal)** serves the most urgent cases **together**. The engine
searches every way of handing the free ambulances to those cases by backtracking, and
commits the plan with the lowest total priority-weighted travel time.

## The greedy trap

```
Ambulance A: 2 min to Cardiac,  4 min to Trauma
Ambulance B: 3 min to Cardiac, 20 min to Trauma

one at a time:  Cardiac <- A (2),  Trauma <- B (20)   total 22 min
whole batch:    Cardiac <- B (3),  Trauma <- A  (4)   total  7 min
```

The cardiac case waits one extra minute; the trauma case saves sixteen.

## Try it on the shipped city map

Sign in, register **Cardiac at Mall Road (node 5)** and **Trauma at Station Depot
(node 7)**, then press **Dispatch all waiting (optimal)**. The engine reports:

```
Batch dispatch of 2: #1 <- Ambulance 6 (7.0 min), #2 <- Ambulance 3 (0.0 min).
Priority-weighted travel time 700 instead of 1080 one at a time (35% less).
```

Dispatching the two one at a time instead gives Ambulance 3 to the cardiac case and
leaves the trauma case with a far ambulance.

Other pairs on this map where the batch plan wins: Cardiac 2 + Trauma 11 (30% less),
Cardiac 6 + Trauma 7 (22%), Cardiac 10 + Trauma 7 (24%). Everywhere else the two
plans agree, and the engine says so.

## What it optimises

Cost of one plan = sum over served cases of **priority at the current time x minutes
from its ambulance to the scene**.

Weighting by priority is what keeps triage honest: a cardiac case (base priority 100)
costs five times as much per minute as a transfer (base 20), so the search will not
delay a critical case to save time on a routine one. Priority includes the aging bonus,
so a long-waiting case pulls harder the longer it waits.

Leaving a case for later costs `priority x 1e6`, far more than any real trip. So the
planner first serves **as many of the most urgent cases as it can**, and only then
minimises travel time among the plans that serve them.

## The search

`BatchPlanner::plan` (in `src/BatchPlanner.cpp`):

1. One Dijkstra run per free ambulance gives its travel time to every scene.
2. For each case, the reachable ambulances are sorted cheapest first, so a good plan
   is found early and the bound prunes harder.
3. `search(case, total)` tries each unused ambulance for this case, recurses, then
   **undoes the choice** (the backtracking step), and finally tries leaving the case
   for later.
4. **Pruning**: costs never decrease, so `if (total >= bestCost) return;` abandons any
   branch that already costs as much as the best complete plan found.

At most `kMaxCases = 6` cases are planned at once, so the search space is bounded by
6! = 720 orderings. In practice pruning cuts it far below that: the two-case example
above explores 23 nodes. Every reply reports `explored`, so the saving is visible.

## What it does not change

- **The planner only decides.** It never touches an ambulance, a bed or an emergency.
  `DispatchCoordinator::dispatchBatch` commits the plan through the same
  `commit()` path as a single dispatch, so nothing can be double-booked.
- **Hospitals are still chosen one at a time**, most urgent case first, by
  `nearestHospitalWithBed`. Beds are not part of the search.
- **Cases it cannot serve go back into the triage queue** unchanged, in their proper
  place.
- **Undo is unaffected**: a batch pushes one record per dispatch, so the undo stack
  reverses them one at a time, newest first.

## Where it lives

| Part | File |
|---|---|
| The search | `include/BatchPlanner.hpp`, `src/BatchPlanner.cpp` |
| Commit and queue handling | `DispatchCoordinator::dispatchBatch`, `commit` |
| API route | `POST /api/dispatch/batch` (`src/Api.cpp`) |
| JSON | `writeBatchResult` (`src/StateJson.cpp`) |
| Button | `frontend/opera.html` (`#dispAll`), `frontend/opera.js` (`dispatchBatch`) |
| Tests | `tests/test_batch.cpp` (67 checks), `tests/test_api.cpp` (batch route) |
