# How to run OPERA

C++ engine + web interface, run as one program: the browser sends each action to
the C++ server as JSON over HTTP, and the server replies with the new state.

```
OPERA/
├── include/  src/  tests/   C++ engine, loader, sign-in, JSON API, HTTP server, tests
├── data/                    city map, hospitals, ambulances, accounts (read by the server)
└── frontend/                opera.html, opera.css, opera.js, track.html, track.js, captcha.txt
```

## 1. Build the server (once, and again after changing C++ code)

From the repository folder (the one that contains `src/`):

```
g++ -std=c++17 -Iinclude src/*.cpp -o opera_server
```

On Windows (MinGW) add Winsock and the .exe name:

```
g++ -std=c++17 -Iinclude src/*.cpp -lws2_32 -o opera_server.exe
```

If the compiler says `unrecognized command line option '-std=c++17'`, your g++
is older: use `-std=c++14` instead (the project builds as C++14 too).

## 2. Start the server

From the repository folder (it looks for `data/` and `frontend/` there):

```
./opera_server            (Windows: .\opera_server.exe)
```

Leave this terminal open while you use OPERA.

## 3. Open the page

Either of these works **while opera_server is running**:

- go to **http://localhost:8080**, or
- in VS Code, right-click `frontend/opera.html` → **Open with Live Server**.

The sign-in page says "Connected to the OPERA server" in green when it has found
the server. Demo accounts (change them before real use):

| Name | Password | Role |
|---|---|---|
| admin | Admin@2026 | admin (can also take ambulances offline/online) |
| operator1 | Oper1@2026 | operator |
| operator2 | Oper2@2026 | operator |

Opening `opera.html` by double-clicking it does **not** work; use one of the two ways above.

Options: `--port 8080`, `--data <folder>`, `--web <folder>`. (With Live Server,
keep the default port 8080: that is where the page looks for the server.)

## Run the tests (plain g++)

From the repository folder, for any test file (example: the dispatch tests):

```
g++ -std=c++17 -Iinclude tests/test_dispatch.cpp src/Graph.cpp src/RoutingEngine.cpp src/Ambulance.cpp src/Hospital.cpp src/Emergency.cpp src/Registry.cpp src/TriageQueue.cpp src/DispatchCoordinator.cpp src/CityLoader.cpp src/Json.cpp src/StateJson.cpp src/Api.cpp src/Sha256.cpp src/Auth.cpp -o test_dispatch
./test_dispatch
```

Use the same command with `test_api`, `test_auth`, `test_json`, `test_loader`,
`test_stack_edge` or any original test. (`src/main.cpp` and `src/HttpServer.cpp`
are left out of test builds because they hold the program's `main()` and the sockets.)

## Build with CMake (optional)

```
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

With CMake the server finds `data/` and `frontend/` from any folder.
