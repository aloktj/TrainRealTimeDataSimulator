cmake_minimum_required(VERSION 3.16)

project(trdp-simulator
    VERSION 0.1.0
    LANGUAGES CXX)

# -------------------------
# Options
# -------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(TRDP_USE_STUBS "Use built-in TRDP stub implementation (CI-friendly)" ON)
option(TRDP_ENABLE_APP "Build the simulator executable" ON)
option(TRDP_ENABLE_TESTS "Build unit tests" ON)
option(TRDP_WARNINGS_AS_ERRORS "Treat warnings as errors" OFF)
option(TRDP_PI_OPTIMIZED "Enable Raspberry Pi-specific tuning flags" OFF)
set(TRDP_SANITIZER "none" CACHE STRING "Sanitizer preset (none, address, undefined, thread)")
option(TRDP_USE_TCNOPEN "Build TRDP from the TCNopen submodule" OFF)
set(TRDP_INCLUDE_DIR "" CACHE PATH "Path to TRDP headers (used when TRDP_USE_STUBS=OFF)")
set(TRDP_LIB_PATH "" CACHE FILEPATH "Path to TRDP static/shared library (used when TRDP_USE_STUBS=OFF)")

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

include(FetchContent)
include(GNUInstallDirs)

# -------------------------
# Dependencies
# -------------------------
find_package(tinyxml2 QUIET)
if(NOT tinyxml2_FOUND)
    set(_tinyxml2_dir "${CMAKE_CURRENT_SOURCE_DIR}/external/tinyxml2")
    if(EXISTS "${_tinyxml2_dir}/CMakeLists.txt")
        message(STATUS "tinyxml2 not found in system paths; using submodule at external/tinyxml2")
        add_subdirectory(${_tinyxml2_dir} EXCLUDE_FROM_ALL)
        if(NOT TARGET tinyxml2::tinyxml2)
            add_library(tinyxml2::tinyxml2 ALIAS tinyxml2)
        endif()
    else()
        message(FATAL_ERROR "tinyxml2 not found. Install libtinyxml2-dev or initialize the external/tinyxml2 submodule.")
    endif()
endif()

find_package(nlohmann_json QUIET)
find_package(OpenSSL REQUIRED)

set(TRDP_SIM_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include")
set(TRDP_SIM_SRC_DIR     "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(TRDP_INCLUDE_DIR_FOUND "")

# TRDP discovery (skipped when stubbing)
set(TRDP_LIB_TARGET "")
if(TRDP_USE_TCNOPEN)
    set(TRDP_USE_STUBS OFF CACHE BOOL "" FORCE)
    set(_tcnopen_dir "${CMAKE_CURRENT_SOURCE_DIR}/external/TCNopen")
    if(NOT EXISTS "${_tcnopen_dir}/CMakeLists.txt")
        message(FATAL_ERROR "TCNopen submodule not found. Run `git submodule update --init --recursive`." )
    endif()
    add_subdirectory(${_tcnopen_dir})
    if(TARGET trdp)
        set(TRDP_LIB_TARGET trdp)
    elseif(TARGET TCNopen::trdp)
        set(TRDP_LIB_TARGET TCNopen::trdp)
    else()
        message(FATAL_ERROR "TCNopen is enabled but no `trdp` library target was exported.")
    endif()
endif()

if(NOT TRDP_USE_STUBS AND NOT TRDP_LIB_TARGET)
    find_path(TRDP_INCLUDE_DIR_FOUND
        NAMES trdp_if_light.h trdp_if.h tau_api.h
        PATHS ${TRDP_INCLUDE_DIR} /usr/local/include /usr/include)
    if(NOT TRDP_INCLUDE_DIR_FOUND)
        message(FATAL_ERROR "TRDP headers not found. Set TRDP_INCLUDE_DIR to your TRDP include folder.")
    endif()

    find_library(TRDP_LIB_FOUND
        NAMES trdp
        PATHS ${TRDP_LIB_PATH} /usr/local/lib /usr/lib)
    if(NOT TRDP_LIB_FOUND)
        message(FATAL_ERROR "TRDP library not found. Set TRDP_LIB_PATH to your TRDP library path.")
    endif()

    add_library(trdp STATIC IMPORTED)
    set_target_properties(trdp PROPERTIES
        IMPORTED_LOCATION "${TRDP_LIB_FOUND}"
        INTERFACE_INCLUDE_DIRECTORIES "${TRDP_INCLUDE_DIR_FOUND}")
    set(TRDP_LIB_TARGET trdp)
endif()

# -------------------------
# Sources and includes
# -------------------------
set(TRDP_ADAPTER_SOURCE
    ${TRDP_SIM_SRC_DIR}/trdp_adapter.cpp)
if(TRDP_USE_STUBS)
    set(TRDP_ADAPTER_SOURCE ${TRDP_SIM_SRC_DIR}/trdp_adapter_stub.cpp)
endif()

set(TRDP_SIM_CORE_SOURCES
    ${TRDP_SIM_SRC_DIR}/engine_context.cpp
    ${TRDP_SIM_SRC_DIR}/config_manager.cpp
    ${TRDP_SIM_SRC_DIR}/xml_loader.cpp
    ${TRDP_SIM_SRC_DIR}/data_types.cpp
    ${TRDP_SIM_SRC_DIR}/data_marshalling.cpp
    ${TRDP_SIM_SRC_DIR}/performance_harness.cpp
    ${TRDP_SIM_SRC_DIR}/pd_engine.cpp
    ${TRDP_SIM_SRC_DIR}/md_engine.cpp
    ${TRDP_SIM_SRC_DIR}/diagnostic_manager.cpp
    ${TRDP_SIM_SRC_DIR}/backend_engine.cpp
    ${TRDP_SIM_SRC_DIR}/auth_manager.cpp
    ${TRDP_ADAPTER_SOURCE}
)

add_library(trdp-simulator-core ${TRDP_SIM_CORE_SOURCES})
target_include_directories(trdp-simulator-core
    PUBLIC
        ${TRDP_SIM_INCLUDE_DIR}
        ${TRDP_SIM_INCLUDE_DIR}/trdp_sim
        ${TRDP_INCLUDE_DIR_FOUND}
)
target_link_libraries(trdp-simulator-core
    PUBLIC
        OpenSSL::Crypto
    PRIVATE
        tinyxml2::tinyxml2
)
if(nlohmann_json_FOUND)
    target_link_libraries(trdp-simulator-core PRIVATE nlohmann_json::nlohmann_json)
else()
    target_compile_definitions(trdp-simulator-core PRIVATE NLOHMANN_JSON_HEADER_ONLY)
endif()

if(NOT TRDP_USE_STUBS)
    if(NOT TRDP_LIB_TARGET)
        message(FATAL_ERROR "No TRDP library target configured. Check TRDP settings.")
    endif()
    target_link_libraries(trdp-simulator-core PRIVATE ${TRDP_LIB_TARGET})
else()
    target_compile_definitions(trdp-simulator-core PUBLIC TRDP_USE_STUBS)
endif()

# -------------------------
# Build flags
# -------------------------
set(_warn_flags)
if(MSVC)
    list(APPEND _warn_flags /W4)
else()
    list(APPEND _warn_flags -Wall -Wextra -Wpedantic)
    if(TRDP_WARNINGS_AS_ERRORS)
        list(APPEND _warn_flags -Werror)
    endif()
endif()

set(_san_flags)
if(TRDP_SANITIZER STREQUAL "address")
    set(_san_flags -fsanitize=address)
elif(TRDP_SANITIZER STREQUAL "undefined")
    set(_san_flags -fsanitize=undefined)
elif(TRDP_SANITIZER STREQUAL "thread")
    set(_san_flags -fsanitize=thread)
endif()

set(_pi_flags)
if(TRDP_PI_OPTIMIZED)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm|aarch64")
        list(APPEND _pi_flags -mcpu=cortex-a72 -mfpu=neon-fp-armv8 -mfloat-abi=hard)
    else()
        message(WARNING "TRDP_PI_OPTIMIZED is enabled but the target architecture is not ARM; flags will be omitted.")
    endif()
endif()

foreach(tgt trdp-simulator-core)
    target_compile_options(${tgt} PRIVATE ${_warn_flags} ${_san_flags} ${_pi_flags})
    target_link_options(${tgt} PRIVATE ${_san_flags})
endforeach()

# -------------------------
# Executable
# -------------------------
if(TRDP_ENABLE_APP)
    find_package(Drogon QUIET)
    if(NOT Drogon_FOUND)
        set(_drogon_dir "${CMAKE_CURRENT_SOURCE_DIR}/external/drogon")
        if(EXISTS "${_drogon_dir}/CMakeLists.txt")
            message(STATUS "Drogon not found in system paths; using submodule at external/drogon")
            add_subdirectory(${_drogon_dir} EXCLUDE_FROM_ALL)
        else()
            message(FATAL_ERROR "Drogon not found. Install libdrogon-dev or initialize the external/drogon submodule.")
        endif()
    endif()

    if(NOT TARGET Drogon::Drogon)
        message(FATAL_ERROR "Drogon target not available; ensure Drogon is installed or the submodule is initialized.")
    endif()

    add_executable(trdp-simulator
        ${TRDP_SIM_SRC_DIR}/backend_api.cpp
        ${TRDP_SIM_SRC_DIR}/realtime_hub.cpp
        ${TRDP_SIM_SRC_DIR}/realtime_stream.cpp
        ${TRDP_SIM_SRC_DIR}/main.cpp)

    target_include_directories(trdp-simulator
        PUBLIC
            ${TRDP_SIM_INCLUDE_DIR}
            ${TRDP_SIM_INCLUDE_DIR}/trdp_sim
            ${TRDP_INCLUDE_DIR_FOUND}
    )

    target_link_libraries(trdp-simulator
        PRIVATE
            trdp-simulator-core
            Drogon::Drogon
    )

    if(nlohmann_json_FOUND)
        target_link_libraries(trdp-simulator PRIVATE nlohmann_json::nlohmann_json)
    else()
        target_compile_definitions(trdp-simulator PRIVATE NLOHMANN_JSON_HEADER_ONLY)
    endif()

    foreach(tgt trdp-simulator)
        target_compile_options(${tgt} PRIVATE ${_warn_flags} ${_san_flags} ${_pi_flags})
        target_link_options(${tgt} PRIVATE ${_san_flags})
    endforeach()
endif()

# -------------------------
# Tests
# -------------------------
if(TRDP_ENABLE_TESTS)
    include(CTest)
    enable_testing()
    include(FetchContent)
    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    file(GLOB TEST_SOURCES CONFIGURE_DEPENDS tests/*.cpp)
    add_executable(trdp-simulator-tests ${TEST_SOURCES})
    target_link_libraries(trdp-simulator-tests
        PRIVATE
            trdp-simulator-core
            GTest::gtest_main
    )
    target_compile_options(trdp-simulator-tests PRIVATE ${_warn_flags} ${_san_flags})
    target_link_options(trdp-simulator-tests PRIVATE ${_san_flags})

    include(GoogleTest)
    gtest_discover_tests(trdp-simulator-tests)
endif()

# -------------------------
# Install & packaging
# -------------------------
if(TRDP_ENABLE_APP)
    install(TARGETS trdp-simulator RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(FILES config/trdp.xml DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/trdp-simulator)
    install(FILES config/trdp-simulator.env DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/default)
    install(FILES config/systemd/trdp-simulator.service
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/systemd/system)
endif()

set(CPACK_PACKAGE_VENDOR "TrainRealTimeDataSimulator")
set(CPACK_PACKAGE_CONTACT "maintainers@trainrealtimedatasimulator.local")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "TRDP simulator with HTTP API, PD/MD controls, and diagnostics")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_PACKAGE_FILE_NAME "trdp-simulator-${PROJECT_VERSION}-linux")
set(CPACK_GENERATOR "DEB")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "TRDP Simulator Team")
set(CPACK_DEBIAN_PACKAGE_SECTION "net")
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libdrogon-dev, libssl3, libtinyxml2-9, nlohmann-json3-dev")
set(CPACK_DEBIAN_ARCHITECTURE ${CMAKE_SYSTEM_PROCESSOR})

include(CPack)
