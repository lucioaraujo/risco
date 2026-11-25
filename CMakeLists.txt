# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (C) 2026 Lúcio de Araújo
cmake_minimum_required(VERSION 3.22)
project(Risco VERSION 0.1.0 LANGUAGES C CXX)

# Song owns a bounded bank of 64 cells, each with fixed-capacity inscription
# and event storage. Match the usual Unix main-thread stack on Windows so
# validation tools and application entry points can hold temporary Songs.
if(MSVC)
    add_link_options(/STACK:8388608)
endif()

set(RISCO_AYUMI_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/ayumi" CACHE PATH
    "Audited Ayumi source used by Risco's historical AY/YM core")
if(NOT RISCO_AYUMI_SOURCE_DIR)
    set(RISCO_AYUMI_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/ayumi")
endif()
if(NOT EXISTS "${RISCO_AYUMI_SOURCE_DIR}/ayumi.c" OR
   NOT EXISTS "${RISCO_AYUMI_SOURCE_DIR}/ayumi.h" OR
   NOT EXISTS "${RISCO_AYUMI_SOURCE_DIR}/LICENSE")
    message(FATAL_ERROR "RISCO_AYUMI_SOURCE_DIR must contain ayumi.c, ayumi.h and LICENSE")
endif()
file(SHA256 "${RISCO_AYUMI_SOURCE_DIR}/ayumi.c" _risco_ayumi_c_sha)
file(SHA256 "${RISCO_AYUMI_SOURCE_DIR}/ayumi.h" _risco_ayumi_h_sha)
file(SHA256 "${RISCO_AYUMI_SOURCE_DIR}/LICENSE" _risco_ayumi_license_sha)
if(NOT _risco_ayumi_c_sha STREQUAL "8824bc343726aa9dc7e4172b14f0e9a0a06ed3f21766277ffb9686b07f19401d" OR
   NOT _risco_ayumi_h_sha STREQUAL "506e3b75d8269d670d74f75115d3b385ad0f1c232bc797391bd97cc074f17099" OR
   NOT _risco_ayumi_license_sha STREQUAL "e45be409a54a64d7473d15d7d5638959c6c7e6ce90a9f013a7f89351709a3acf")
    message(FATAL_ERROR "Ayumi source differs from audited commit 07c08b4874c359169e4a028edf73f046d8b763e2")
endif()
add_library(risco_ayumi_external STATIC "${RISCO_AYUMI_SOURCE_DIR}/ayumi.c")
target_include_directories(risco_ayumi_external PUBLIC "${RISCO_AYUMI_SOURCE_DIR}")

add_library(risco_core STATIC src/core/BytebeatExpression.cpp src/core/Composition.cpp src/core/CompositionSeed.cpp src/core/TrackGeneration.cpp src/core/AlbumPlan.cpp src/core/AlbumManifest.cpp src/core/FormulaComposition.cpp src/core/FormulaScaleMachine.cpp src/core/FormulaSeed.cpp src/core/FormulaLibrary.cpp src/core/Song.cpp
    src/core/SongIO.cpp src/core/SongPlayback.cpp src/core/OutputStage.cpp src/core/AyNoiseStudy.cpp src/core/LoudnessMeter.cpp)
target_sources(risco_core PRIVATE src/core/HistoricalAyEngine.cpp)
target_include_directories(risco_core PUBLIC src)
target_compile_features(risco_core PUBLIC cxx_std_17)
target_link_libraries(risco_core PUBLIC risco_ayumi_external)

add_executable(risco_offline_render src/tools/offline_render.cpp)
target_link_libraries(risco_offline_render PRIVATE risco_core)

add_executable(risco_formula_seed src/tools/formula_seed.cpp)
target_link_libraries(risco_formula_seed PRIVATE risco_core)

add_executable(risco_formula_repository_smoke src/tools/formula_repository_smoke.cpp)
target_link_libraries(risco_formula_repository_smoke PRIVATE risco_core)

add_executable(risco_compositional_criteria_check src/tools/compositional_criteria_check.cpp)
target_link_libraries(risco_compositional_criteria_check PRIVATE risco_core)
if(MSVC)
    # This long legacy validation entry point intentionally keeps many fixed-capacity
    # fixtures in scope; reserve extra stack for the test process, not the app.
    target_link_options(risco_compositional_criteria_check PRIVATE /STACK:33554432)
endif()
enable_testing()
add_test(NAME risco_compositional_criteria COMMAND risco_compositional_criteria_check)
add_executable(risco_loudness_check src/tools/loudness_check.cpp)
target_link_libraries(risco_loudness_check PRIVATE risco_core)
add_test(NAME risco_loudness COMMAND risco_loudness_check)
add_executable(risco_dither_check src/tools/dither_check.cpp)
target_link_libraries(risco_dither_check PRIVATE risco_core)
add_test(NAME risco_dither COMMAND risco_dither_check)
add_executable(risco_composition_seed_check src/tools/composition_seed_check.cpp)
target_link_libraries(risco_composition_seed_check PRIVATE risco_core)
add_test(NAME risco_composition_seed COMMAND risco_composition_seed_check)
add_executable(risco_track_generation_check src/tools/track_generation_check.cpp)
target_link_libraries(risco_track_generation_check PRIVATE risco_core)
add_test(NAME risco_track_generation COMMAND risco_track_generation_check)
add_executable(risco_album_plan_check src/tools/album_plan_check.cpp)
target_link_libraries(risco_album_plan_check PRIVATE risco_core)
add_test(NAME risco_album_plan COMMAND risco_album_plan_check)
add_executable(risco_album_manifest_check src/tools/album_manifest_check.cpp)
target_link_libraries(risco_album_manifest_check PRIVATE risco_core)
add_test(NAME risco_album_manifest COMMAND risco_album_manifest_check)
add_executable(risco_ay_register_contract_check src/tools/ay_register_contract_check.cpp)
target_link_libraries(risco_ay_register_contract_check PRIVATE risco_core)
add_test(NAME risco_ay_register_contract COMMAND risco_ay_register_contract_check)
add_executable(risco_historical_ay_engine_check src/tools/historical_ay_engine_check.cpp)
target_link_libraries(risco_historical_ay_engine_check PRIVATE risco_core)
add_test(NAME risco_historical_ay_engine COMMAND risco_historical_ay_engine_check)

add_executable(risco_ayumi_sandbox src/tools/ayumi_sandbox.cpp)
target_include_directories(risco_ayumi_sandbox PRIVATE src)
target_compile_features(risco_ayumi_sandbox PRIVATE cxx_std_17)
target_link_libraries(risco_ayumi_sandbox PRIVATE risco_ayumi_external)
add_test(NAME risco_ayumi_sandbox_self_check COMMAND risco_ayumi_sandbox --self-check)

option(RISCO_BUILD_JUCE_APP "Build the JUCE desktop prototype" ON)
option(RISCO_PACKAGE_LINUX "Prepare a Debian package of the Risco desktop app" OFF)
set(RISCO_JUCE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/JUCE-9.0.3"
    CACHE PATH "Path to an independent JUCE source tree")
if(RISCO_BUILD_JUCE_APP)
    if(RISCO_JUCE_SOURCE_DIR AND EXISTS "${RISCO_JUCE_SOURCE_DIR}/CMakeLists.txt")
        file(STRINGS "${RISCO_JUCE_SOURCE_DIR}/CMakeLists.txt" _risco_juce_version
             REGEX "^project\\(JUCE VERSION [0-9]+\\.[0-9]+\\.[0-9]+ LANGUAGES C CXX\\)")
        if(NOT _risco_juce_version STREQUAL
           "project(JUCE VERSION 9.0.3 LANGUAGES C CXX)")
            message(FATAL_ERROR "Risco expects JUCE 9.0.3; found: ${_risco_juce_version}")
        endif()
        add_subdirectory("${RISCO_JUCE_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/JUCE" EXCLUDE_FROM_ALL)
    else()
        find_package(JUCE CONFIG REQUIRED)
    endif()
    juce_add_gui_app(risco_app PRODUCT_NAME "Risco"
        ICON_BIG resources/icons/icon-512.png
        ICON_SMALL resources/icons/icon-48.png)
    juce_add_binary_data(risco_assets SOURCES
        resources/fonts/DejaVuSansMono.ttf
        resources/fonts/DejaVuSansMono-Bold.ttf
        resources/icons/icon-48.png)
    target_sources(risco_app PRIVATE src/app/Main.cpp src/app/DollchanCatalog.cpp)
    target_compile_features(risco_app PRIVATE cxx_std_17)
    target_compile_definitions(risco_app PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
        JUCE_DISPLAY_SPLASH_SCREEN=0 JUCE_REPORT_APP_USAGE=0)
    if(MSVC)
        target_compile_options(risco_app PRIVATE /utf-8)
    else()
        target_compile_options(risco_app PRIVATE -finput-charset=UTF-8 -fexec-charset=UTF-8)
    endif()
    target_sources(risco_app PRIVATE src/app/AlbumWaveFile.cpp src/app/AlbumRenderQueue.cpp)
    target_link_libraries(risco_app PRIVATE risco_core risco_assets juce::juce_audio_utils)
    add_executable(risco_album_wave_check src/tools/album_wave_check.cpp src/app/AlbumWaveFile.cpp)
    target_include_directories(risco_album_wave_check PRIVATE src/app src)
    target_compile_features(risco_album_wave_check PRIVATE cxx_std_17)
    target_compile_definitions(risco_album_wave_check PRIVATE JUCE_USE_CURL=0)
    target_link_libraries(risco_album_wave_check PRIVATE risco_core juce::juce_core)
    add_test(NAME risco_album_wave COMMAND risco_album_wave_check)
    set_tests_properties(risco_album_wave PROPERTIES TIMEOUT 250 PROCESSORS 1)
    add_executable(risco_album_render_queue_check
        src/tools/album_render_queue_check.cpp
        src/app/AlbumWaveFile.cpp
        src/app/AlbumRenderQueue.cpp)
    target_include_directories(risco_album_render_queue_check PRIVATE src/app src)
    target_compile_features(risco_album_render_queue_check PRIVATE cxx_std_17)
    target_compile_definitions(risco_album_render_queue_check PRIVATE JUCE_USE_CURL=0)
    target_link_libraries(risco_album_render_queue_check PRIVATE risco_core juce::juce_core)
    add_test(NAME risco_album_render_queue COMMAND risco_album_render_queue_check)
    set_tests_properties(risco_album_render_queue PROPERTIES TIMEOUT 300 PROCESSORS 1
        ENVIRONMENT "TMPDIR=${CMAKE_CURRENT_BINARY_DIR}")
    add_executable(risco_catalog_probe src/tools/catalog_probe.cpp src/app/DollchanCatalog.cpp)
    target_compile_features(risco_catalog_probe PRIVATE cxx_std_17)
    target_compile_definitions(risco_catalog_probe PRIVATE JUCE_USE_CURL=0)
    target_link_libraries(risco_catalog_probe PRIVATE risco_core juce::juce_core)
endif()

if(RISCO_PACKAGE_LINUX)
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR NOT TARGET risco_app)
        message(FATAL_ERROR "RISCO_PACKAGE_LINUX requires a Linux build of the desktop app")
    endif()
    if(NOT EXISTS "${RISCO_JUCE_SOURCE_DIR}/LICENSE.md" OR
       NOT EXISTS "${RISCO_JUCE_SOURCE_DIR}/JUCE.spdx.json")
        message(FATAL_ERROR "Set RISCO_JUCE_SOURCE_DIR to the JUCE tree used to build Risco")
    endif()
    include(packaging/linux/Package.cmake)
endif()
