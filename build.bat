@echo off
rem OPERA - build the server on Windows (MinGW g++). Double-click, or run: .\build.bat
rem Always builds from this folder, whatever folder the terminal is in.
cd /d "%~dp0"

where g++ >nul 2>nul
if errorlevel 1 (
  echo g++ was not found. Install MinGW-w64 and add its bin folder to PATH.
  exit /b 1
)

echo Building opera_server.exe (C++17)...
g++ -std=c++17 -Iinclude src\Ambulance.cpp src\Api.cpp src\Auth.cpp src\AutoDispatcher.cpp src\BatchPlanner.cpp src\Captcha.cpp src\CityLoader.cpp src\DispatchCoordinator.cpp src\Emergency.cpp src\Graph.cpp src\Hospital.cpp src\HttpParse.cpp src\HttpServer.cpp src\Json.cpp src\LiveTracker.cpp src\main.cpp src\Registry.cpp src\RoutingEngine.cpp src\SecureRandom.cpp src\Sha256.cpp src\StateJson.cpp src\TriageQueue.cpp -lws2_32 -ladvapi32 -o opera_server.exe
if not errorlevel 1 goto ok

echo C++17 build failed. Trying C++14 (older g++)...
g++ -std=c++14 -Iinclude src\Ambulance.cpp src\Api.cpp src\Auth.cpp src\AutoDispatcher.cpp src\BatchPlanner.cpp src\Captcha.cpp src\CityLoader.cpp src\DispatchCoordinator.cpp src\Emergency.cpp src\Graph.cpp src\Hospital.cpp src\HttpParse.cpp src\HttpServer.cpp src\Json.cpp src\LiveTracker.cpp src\main.cpp src\Registry.cpp src\RoutingEngine.cpp src\SecureRandom.cpp src\Sha256.cpp src\StateJson.cpp src\TriageQueue.cpp -lws2_32 -ladvapi32 -o opera_server.exe
if errorlevel 1 (
  echo Build failed. If it says "Permission denied", close the running opera_server.exe first.
  exit /b 1
)

:ok
echo Done. Start the server with:  .\opera_server.exe
