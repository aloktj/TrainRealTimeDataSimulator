cmake_minimum_required(VERSION 3.16)

project(trdp-simulator
    VERSION 0.1.0
    LANGUAGES CXX)

# -------------------------
# Build settings
# -------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# For development; you can tune later
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

# -------------------------
# Dependencies
# -------------------------

# 1) Drogon (web framework)
# Expect it to be installed system-wide (e.g. via apt or from source)
find_package(Drogon REQUIRED)

# 2) nlohmann-json (header-only usually, but use find_package if available)
# If your distro has it:
#   sudo apt install nlohmann-json3-dev
# Then:
find_package(nlohmann_json QUIET)

# 3) tinyxml2 for configuration parsing
find_package(tinyxml2 REQUIRED)

if(NOT nlohmann_json_FOUND)
    message(STATUS "nlohmann_json not found via find_package, using header-only include path fallback.")
    # If you vendor it under external/nlohmann_json, you can do something like:
    # add_subdirectory(external/nlohmann_json)
    # and then link nlohmann_json::nlohmann_json
endif()

# 4) TRDP library from TCNopen
# Discover headers & library with actionable error messages so users know what to set.
set(TRDP_INCLUDE_DIR "" CACHE PATH "Path to TRDP headers (e.g. containing trdp_if_light.h)")
set(TRDP_LIB_PATH "" CACHE FILEPATH "Path to TRDP static/shared library (e.g. libtrdp.a)")

find_path(TRDP_INCLUDE_DIR_FOUND
    NAMES trdp_if_light.h trdp_if.h tau_api.h
    PATHS
        ${TRDP_INCLUDE_DIR}
        /usr/local/include
        /usr/include
)

if(NOT TRDP_INCLUDE_DIR_FOUND)
    message(FATAL_ERROR "TRDP headers not found. Set TRDP_INCLUDE_DIR to your TRDP include folder (contains trdp_if_light.h/tau_api.h)." )
endif()

find_library(TRDP_LIB_FOUND
    NAMES trdp
    PATHS
        ${TRDP_LIB_PATH}
        /usr/local/lib
        /usr/lib
)

if(NOT TRDP_LIB_FOUND)
    message(FATAL_ERROR "TRDP library not found. Set TRDP_LIB_PATH to your libtrdp.a or shared library (built from TCNopen TRDP)." )
endif()

add_library(trdp STATIC IMPORTED)
set_target_properties(trdp PROPERTIES
    IMPORTED_LOCATION "${TRDP_LIB_FOUND}"
    INTERFACE_INCLUDE_DIRECTORIES "${TRDP_INCLUDE_DIR_FOUND}"
)

# -------------------------
# Sources and includes
# -------------------------

set(TRDP_SIM_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include")
set(TRDP_SIM_SRC_DIR     "${CMAKE_CURRENT_SOURCE_DIR}/src")

include_directories(
    ${TRDP_SIM_INCLUDE_DIR}
    ${TRDP_INCLUDE_DIR_FOUND}
)

# You can list sources explicitly or glob; explicit is more robust:
set(TRDP_SIM_SOURCES
    ${TRDP_SIM_SRC_DIR}/engine_context.cpp
    ${TRDP_SIM_SRC_DIR}/config_manager.cpp
    ${TRDP_SIM_SRC_DIR}/data_types.cpp
    ${TRDP_SIM_SRC_DIR}/trdp_adapter.cpp
    ${TRDP_SIM_SRC_DIR}/pd_engine.cpp
    ${TRDP_SIM_SRC_DIR}/md_engine.cpp
    ${TRDP_SIM_SRC_DIR}/diagnostic_manager.cpp
    ${TRDP_SIM_SRC_DIR}/backend_api.cpp
    ${TRDP_SIM_SRC_DIR}/main.cpp
)

add_executable(trdp-simulator
    ${TRDP_SIM_SOURCES}
)

target_include_directories(trdp-simulator
    PUBLIC
        ${TRDP_SIM_INCLUDE_DIR}
        ${TRDP_INCLUDE_DIR_FOUND}
)

# -------------------------
# Link libraries
# -------------------------

target_link_libraries(trdp-simulator
    PRIVATE
        Drogon::Drogon   # Drogon CMake target
        trdp             # TRDP imported static lib
        tinyxml2::tinyxml2
)

# nlohmann_json target if found
if(nlohmann_json_FOUND)
    target_link_libraries(trdp-simulator PRIVATE nlohmann_json::nlohmann_json)
else()
    # If you vendor the single-header version, just ensure it's in include path.
    target_compile_definitions(trdp-simulator PRIVATE NLOHMANN_JSON_HEADER_ONLY)
endif()

# -------------------------
# Compiler warnings (optional)
# -------------------------
if(MSVC)
    target_compile_options(trdp-simulator PRIVATE /W4)
else()
    target_compile_options(trdp-simulator PRIVATE -Wall -Wextra -Wpedantic)
endif()
