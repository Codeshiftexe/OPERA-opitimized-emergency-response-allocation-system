cmake_minimum_required(VERSION 3.16)
project(opera VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

if(MSVC)
    set(OPERA_WARNINGS /W4)
else()
    set(OPERA_WARNINGS -Wall -Wextra -Wpedantic)
endif()

# Engine (your original files) + the loader.
add_library(opera_core STATIC
    src/Graph.cpp
    src/RoutingEngine.cpp
    src/Ambulance.cpp
    src/Hospital.cpp
    src/Emergency.cpp
    src/Registry.cpp
    src/TriageQueue.cpp
    src/DispatchCoordinator.cpp
    src/CityLoader.cpp
)
target_include_directories(opera_core PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_compile_options(opera_core PRIVATE ${OPERA_WARNINGS})

# JSON + serialisation + sign-in + API routes (no sockets, so it is unit-testable).
add_library(opera_api STATIC
    src/Sha256.cpp
    src/Auth.cpp
    src/Json.cpp
    src/StateJson.cpp
    src/Api.cpp
)
target_link_libraries(opera_api PUBLIC opera_core)
target_compile_options(opera_api PRIVATE ${OPERA_WARNINGS})

# The program: socket server + main().
add_executable(opera_server src/main.cpp src/HttpServer.cpp)
target_link_libraries(opera_server PRIVATE opera_api)
target_compile_options(opera_server PRIVATE ${OPERA_WARNINGS})
if(WIN32)
    target_link_libraries(opera_server PRIVATE ws2_32)
endif()
target_compile_definitions(opera_server PRIVATE
    OPERA_DEFAULT_DATA_DIR="${PROJECT_SOURCE_DIR}/data"
    OPERA_DEFAULT_WEB_DIR="${PROJECT_SOURCE_DIR}/frontend"
)

# Tests: one program per file in tests/.
enable_testing()
foreach(t test_heap test_stack test_stack_edge test_graph test_routing test_resources
          test_emergency test_registry test_triage test_dispatch test_loader)
    add_executable(${t} tests/${t}.cpp)
    target_link_libraries(${t} PRIVATE opera_core)
    add_test(NAME ${t} COMMAND ${t})
endforeach()
foreach(t test_json test_api test_auth)
    add_executable(${t} tests/${t}.cpp)
    target_link_libraries(${t} PRIVATE opera_api)
    add_test(NAME ${t} COMMAND ${t})
endforeach()
target_compile_definitions(test_loader PRIVATE OPERA_DATA_DIR="${PROJECT_SOURCE_DIR}/data")
