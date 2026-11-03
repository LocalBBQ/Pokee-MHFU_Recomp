set(MHFU_PROFILE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")

# The AOT corpus is derived from the user's executable and is not committed.
# Until scripts/generate.sh has produced it, link an empty registry instead.
if(PSPRECOMP_COVERAGE)
    # Public coverage never compiles the user's game-derived corpus.
    set(MHFU_GENERATED)
else()
    file(GLOB MHFU_GENERATED CONFIGURE_DEPENDS "${MHFU_PROFILE_DIR}/generated/*.cpp")
endif()
if(MHFU_GENERATED)
    list(LENGTH MHFU_GENERATED MHFU_GENERATED_COUNT)
    message(STATUS "mhfu: ${MHFU_GENERATED_COUNT} generated AOT units")
    if(MSVC)
        if(PSPRECOMP_GENERATED_OPT_LEVEL STREQUAL "0")
            set(MHFU_GENERATED_OPTIONS "/Od;/bigobj")
        else()
            set(MHFU_GENERATED_OPTIONS "/O${PSPRECOMP_GENERATED_OPT_LEVEL};/Ob0;/bigobj")
        endif()
    else()
        set(MHFU_GENERATED_OPTIONS "-O${PSPRECOMP_GENERATED_OPT_LEVEL};-g0")
    endif()
    set_source_files_properties(${MHFU_GENERATED} PROPERTIES COMPILE_OPTIONS "${MHFU_GENERATED_OPTIONS}")

    # The analog camera wraps the camera update, and main.cpp has to hand it
    # the generated unit that update was compiled into. Which unit that is
    # depends on how the generator partitions the code, so read it from the
    # corpus instead of naming it: a new partition then finds its own unit,
    # and a corpus without the update leaves analog camera off.
    set(MHFU_CAMERA_HELPER_UNIT "")
    foreach(unit IN LISTS MHFU_GENERATED)
        file(STRINGS "${unit}" hit REGEX "register_function\\(0x08886E50u, &recomp_unit_[0-9]+," LIMIT_COUNT 1)
        if(hit)
            string(REGEX MATCH "recomp_unit_[0-9]+" MHFU_CAMERA_HELPER_UNIT "${hit}")
            break()
        endif()
    endforeach()
    if(MHFU_CAMERA_HELPER_UNIT)
        message(STATUS "mhfu: camera update in ${MHFU_CAMERA_HELPER_UNIT}")
    else()
        message(STATUS "mhfu: corpus has no camera update; analog camera stays off")
    endif()

    # Hiding the HUD (host/gpu/game_hud.cpp) wraps the game's 2D functions the
    # same way, so their units are read from the corpus too, in one pass.
    set(MHFU_HUD_FUNCTIONS
        088596C8 088952A4 08891474 088915B0 08891700 0889183C 0889198C)
    list(JOIN MHFU_HUD_FUNCTIONS "|" hud_pattern)
    set(hud_lines "")
    foreach(unit IN LISTS MHFU_GENERATED)
        file(STRINGS "${unit}" hits REGEX "register_function\\(0x(${hud_pattern})u, &recomp_unit_[0-9]+,")
        foreach(hit IN LISTS hits)
            string(REGEX MATCH "0x[0-9A-F]+u, &recomp_unit_[0-9]+" pair "${hit}")
            string(REPLACE ", &" ", " pair "${pair}")
            string(APPEND hud_lines "MHFU_HUD_FUNCTION(${pair})\n")
        endforeach()
    endforeach()
    set(MHFU_HUD_INC "${CMAKE_CURRENT_BINARY_DIR}/generated_hooks/hud_functions.inc")
    file(CONFIGURE OUTPUT "${MHFU_HUD_INC}" CONTENT "${hud_lines}" @ONLY)

    # Layered armor (host/game/layered_armor_hook.cpp) stands in for the game's
    # lookup of an armor part's model, the cont_task hunter task, and the load
    # step. The task list is not wrapped: stepping a finished load from it
    # cleared a pointer the quest reads, and embarking then stopped.
    set(MHFU_LAYERED_FUNCTIONS 088516A4 0885187C 08851C3C 088BC7F0 088C0E58 088D6388)
    list(JOIN MHFU_LAYERED_FUNCTIONS "|" layered_pattern)
    set(layered_lines "")
    foreach(unit IN LISTS MHFU_GENERATED)
        file(STRINGS "${unit}" hits REGEX "register_function\\(0x(${layered_pattern})u, &recomp_unit_[0-9]+,")
        foreach(hit IN LISTS hits)
            string(REGEX MATCH "0x[0-9A-F]+u, &recomp_unit_[0-9]+" pair "${hit}")
            string(REPLACE ", &" ", " pair "${pair}")
            string(APPEND layered_lines "MHFU_LAYERED_FUNCTION(${pair})\n")
        endforeach()
    endforeach()
    set(MHFU_LAYERED_INC "${CMAKE_CURRENT_BINARY_DIR}/generated_hooks/layered_armor_functions.inc")
    file(CONFIGURE OUTPUT "${MHFU_LAYERED_INC}" CONTENT "${layered_lines}" @ONLY)
    # A regenerated corpus with the same file names must be read again.
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${MHFU_GENERATED})
else()
    message(STATUS "mhfu: no generated AOT units; run profiles/mhfu/scripts/generate.sh")
    set(MHFU_GENERATED host/generated_stub.cpp)
endif()

# Renderer: SDL3 for the window and Vulkan for drawing. Both are optional so the
# profile still builds headless on a machine without them.
option(MHFU_RENDERER "Build the Vulkan renderer" ON)
set(MHFU_RENDERER_SOURCES)
if(MHFU_RENDERER)
    find_package(SDL3 QUIET)
    find_package(Vulkan QUIET)
    find_program(MHFU_GLSLANG NAMES glslangValidator glslang)
    if(SDL3_FOUND AND Vulkan_FOUND AND MHFU_GLSLANG)
        set(MHFU_SHADER_INC "${CMAKE_CURRENT_BINARY_DIR}/generated_shaders/ge_shaders.inc")
        set(MHFU_SHADERS
            "kGeVertexShader=${MHFU_PROFILE_DIR}/host/gpu/shaders/ge.vert"
            "kGeRawVertexShader=GE_RAW_VERTICES@${MHFU_PROFILE_DIR}/host/gpu/shaders/ge.vert"
            "kGeCheckVertexShader=GE_RAW_VERTICES,GE_CHECK_DECODE@${MHFU_PROFILE_DIR}/host/gpu/shaders/ge.vert"
            "kGeFragmentShader=${MHFU_PROFILE_DIR}/host/gpu/shaders/ge.frag"
            "kGeFragmentShaderPlain=GE_NO_SPECIALIZATION@${MHFU_PROFILE_DIR}/host/gpu/shaders/ge.frag"
            # The game's HUD drawn again over the interface's marks.
            "kHudCoverVertexShader=${MHFU_PROFILE_DIR}/host/gpu/shaders/hud_cover.vert"
            "kHudCoverFragmentShader=${MHFU_PROFILE_DIR}/host/gpu/shaders/hud_cover.frag")
        if(ANDROID)
            # Pre-rotation of the finished frame for a display turned sideways.
            list(APPEND MHFU_SHADERS
                "kRotateVertexShader=${MHFU_PROFILE_DIR}/host/gpu/shaders/rotate.vert"
                "kRotateFragmentShader=${MHFU_PROFILE_DIR}/host/gpu/shaders/rotate.frag")
        endif()
        set(MHFU_SHADER_SOURCES ${MHFU_SHADERS})
        list(TRANSFORM MHFU_SHADER_SOURCES REPLACE "^[A-Za-z]+=([A-Z_,]+@)?" "")
        list(REMOVE_DUPLICATES MHFU_SHADER_SOURCES)
        # A real interpreter, not whatever `python3` names on PATH: on Windows
        # that can be the Microsoft Store's placeholder, which runs nothing.
        # The registry's installs come first there.
        find_package(Python3 REQUIRED COMPONENTS Interpreter)
        add_custom_command(
            OUTPUT "${MHFU_SHADER_INC}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/generated_shaders"
            COMMAND "${Python3_EXECUTABLE}" "${MHFU_PROFILE_DIR}/tools/embed_shaders.py" "${MHFU_GLSLANG}" "${MHFU_SHADER_INC}"
                    ${MHFU_SHADERS}
            DEPENDS ${MHFU_SHADER_SOURCES}
                    "${MHFU_PROFILE_DIR}/tools/embed_shaders.py"
            COMMENT "Compiling GE shaders to SPIR-V")
        add_custom_target(mhfu_shaders DEPENDS "${MHFU_SHADER_INC}")
        # Dear ImGui (third_party/imgui, unmodified) draws the port's own
        # interface over the game.
        set(MHFU_RENDERER_SOURCES
            host/gpu/descriptor_pools.cpp
            host/gpu/device_report.cpp
            host/gpu/frame_interpolation.cpp
            host/gpu/frame_pacing.cpp
            host/gpu/replacement_textures.cpp
            host/gpu/mmpx.cpp
            host/gpu/ui_motion.cpp
            host/gpu/ui_textures.cpp
            host/gpu/vulkan_renderer.cpp
            host/input/gamepad_devices.cpp
            host/ui/bindings_editor.cpp
            host/ui/controllers_screen.cpp
            host/ui/event_quests_screen.cpp
            host/ui/file_browser.cpp
            host/ui/font_menu.cpp
            host/ui/input_script.cpp
            host/ui/layer.cpp
            host/ui/menu.cpp
            host/ui/layered_armor_screen.cpp
            host/ui/mods_screen.cpp
            host/ui/save_screen.cpp
            host/ui/setup_screens.cpp
            host/ui/text_input.cpp
            host/ui/touch_editor.cpp
            host/ui/touch_overlay.cpp
            host/ui/texture_pack_screen.cpp
            host/ui/widgets.cpp
            third_party/imgui/imgui.cpp
            third_party/imgui/imgui_draw.cpp
            third_party/imgui/imgui_tables.cpp
            third_party/imgui/imgui_widgets.cpp
            third_party/imgui/backends/imgui_impl_sdl3.cpp
            third_party/imgui/backends/imgui_impl_vulkan.cpp)
        message(STATUS "mhfu: Vulkan renderer enabled")
    else()
        if(PSPRECOMP_COVERAGE)
            message(FATAL_ERROR "Requested renderer coverage requires SDL3, Vulkan and glslang")
        endif()
        message(STATUS "mhfu: renderer disabled (SDL3, Vulkan or glslang not found)")
        set(MHFU_RENDERER OFF)
    endif()
endif()

# The build the in-game menu shows: `git describe` of the checkout, refreshed
# on every build but rewritten only when it changes, so only the menu
# recompiles after a commit.
set(MHFU_VERSION_HEADER "${CMAKE_CURRENT_BINARY_DIR}/generated_version/pokee_version.hpp")
add_custom_target(mhfu_version
    COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${PROJECT_SOURCE_DIR} -DOUTPUT=${MHFU_VERSION_HEADER}
            -P "${MHFU_PROFILE_DIR}/tools/write_version.cmake"
    BYPRODUCTS "${MHFU_VERSION_HEADER}"
    COMMENT "Checking the build version")

# FFmpeg decodes the game's music and its movies; by default it is built with
# the project (see cmake/FFmpeg.cmake).
include("${MHFU_PROFILE_DIR}/cmake/FFmpeg.cmake")

# GE parsing, texture decoding and the audio mixer are backend independent and
# always built; only the device the sink opens depends on SDL.
set(MHFU_HOST_SOURCES
    host/app_paths.cpp
    host/audio/atrac_decoder.cpp
    host/movie/avc_decoder.cpp
    host/movie/psmf_demuxer.cpp
    host/audio/audio_sink.cpp
    host/audio/sas_core.cpp
    host/camera_probe.cpp
    host/camera/camera_input.cpp
    host/camera/game_aspect.cpp
    host/camera/field_of_view.cpp
    host/camera/game_camera.cpp
    host/camera/lock_on.cpp
    host/camera/hunt_icon.cpp
    host/gpu/game_hud.cpp
    host/gpu/ge_state.cpp
    host/gpu/texture_decode.cpp
    host/input/bindings.cpp
    host/input/chords.cpp
    host/input/presets.cpp
    host/input/gamepad_mapping.cpp
    host/input/touch_action.cpp
    host/input/touch_controls.cpp
    host/gpu/texture_pack.cpp
    host/gpu/texture_pack_import.cpp
    host/gpu/screenshot.cpp
    host/perf/frame_stats.cpp
    host/perf/stall_stacks.cpp
    host/perf/perf_overlay.cpp
    host/platform/utf8_path.cpp
    host/overlays.cpp
    host/mhfu_profile.cpp
    host/kernel/kernel.cpp
    host/kernel/load_trace.cpp
    host/kernel/fast_loading.cpp
    host/kernel/fast_forward.cpp
    host/kernel/fast_forward_switch.cpp
    host/kernel/load_detector.cpp
    host/kernel/iso_image.cpp
    host/hle/hle_common.cpp
    host/hle/hle_threadman.cpp
    host/hle/hle_sysmem.cpp
    host/hle/hle_io.cpp
    host/hle/hle_system.cpp
    host/hle/hle_media.cpp
    host/audio/channel_queue.cpp
    host/audio/guest_pcm.cpp
    host/hle/hle_atrac.cpp
    host/hle/hle_mpeg.cpp
    host/fonts/game_font.cpp
    host/hle/hle_font.cpp
    host/hle/hle_utility.cpp
    host/hle/hle_savedata.cpp
    host/hle/hle_adhoc.cpp
    host/adhoc/client.cpp
    host/adhoc/discovery.cpp
    host/adhoc/host.cpp
    host/adhoc/server.cpp
    host/save_data/aes128.cpp
    host/save_data/param_sfo.cpp
    host/save_data/savedata_crypto.cpp
    host/save_data/savedata_store.cpp
    host/save_data/save_transfer.cpp
    host/install/executable_preparation.cpp
    host/install/front_end.cpp
    host/install/installer.cpp
    host/install/installer_dialogs.cpp
    host/install/user_data.cpp
    host/settings/settings.cpp
    host/mods/file_overlay.cpp
    host/mods/mod_import.cpp
    host/mods/mod_ini.cpp
    host/mods/mod_library.cpp
    host/mods/mod_session.cpp
    host/mods/mhfu_data_bin.cpp
    host/mods/mhfu_mod_format.cpp
    host/mods/mhfu_mods.cpp
    host/game/guest_ram.cpp
    host/game/game_data.cpp
    host/game/equipment_models.cpp
    host/game/layered_armor.cpp
    host/game/layered_armor_hook.cpp
    host/game/damage_numbers.cpp
    host/game/damage_sync.cpp
    host/game/gather_art.cpp
    host/game/gather_icon.cpp
    host/game/button_glyphs.cpp
    host/game/item_atlas.cpp
    host/game/hit_trace.cpp
    host/game/body_trace.cpp
    host/game/item_box.cpp
    host/game/empty_item_slot.cpp
    host/game/give_rarity.cpp
    host/game/true_attack.cpp
    host/game/true_element.cpp
    host/game/hall_equipment.cpp
    host/game/hall_meal.cpp
    host/game/key_quests.cpp
    host/game/install_data.cpp
    host/game/install_data_served.cpp
    host/game/event_quest_import.cpp
    host/game/event_quests.cpp
    host/game/event_quests_hook.cpp
    host/game/title_menu.cpp
    host/game/title_menu_hook.cpp
    host/game/hooks.cpp
    host/game/guest_calls.cpp
    host/game/item_sets.cpp
    host/game/item_sets_menu.cpp
    host/game/pouch_page.cpp
    host/game/sharpness_gauge.cpp
    third_party/tiny_aes/aes.c
)
# tiny-AES-c is C; the project only enables C++, which compiles it unchanged.
set_source_files_properties(third_party/tiny_aes/aes.c PROPERTIES LANGUAGE CXX)

# The recompiled game code gets a target of its own. It needs only the
# psprecomp headers, so definitions and include paths the host adds to
# Pokee (a renderer switch, a new library's headers) do not change its
# compile commands and rebuild all of it. Its objects still link straight into
# the executable, which exports their symbols to the overlay libraries.
add_library(mhfu_generated OBJECT ${MHFU_GENERATED})
target_include_directories(mhfu_generated PRIVATE
    "${PROJECT_SOURCE_DIR}/include"
    "${MHFU_PROFILE_DIR}/generated")
set_target_properties(mhfu_generated PROPERTIES JOB_POOL_COMPILE psprecomp_generated)

# An Android app is a shared library, libmain.so, that SDL's Java activity
# loads and calls; the command-line executable still builds for Android without
# this and runs from adb shell.
option(MHFU_ANDROID_APP "Build the Android app's libmain.so instead of an executable" OFF)
if(MHFU_ANDROID_APP AND NOT (ANDROID AND MHFU_RENDERER))
    message(FATAL_ERROR "MHFU_ANDROID_APP needs the Android NDK toolchain and the renderer (SDL3 and Vulkan)")
endif()
set(MHFU_PROGRAM_SOURCES
    host/main.cpp
    ${MHFU_HOST_SOURCES}
    ${MHFU_RENDERER_SOURCES}
    $<TARGET_OBJECTS:mhfu_generated>)
if(MHFU_ANDROID_APP)
    add_library(Pokee SHARED ${MHFU_PROGRAM_SOURCES} host/platform/android_app.cpp
        host/platform/android_jni.cpp host/platform/android_documents.cpp host/platform/android_performance.cpp
        host/platform/android_fatal.cpp)
    target_compile_definitions(Pokee PRIVATE MHFU_ANDROID_APP=1)
    set_target_properties(Pokee PROPERTIES
        OUTPUT_NAME main
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
else()
    add_executable(Pokee ${MHFU_PROGRAM_SOURCES})
endif()
if(WIN32)
    set(MHFU_WINDOWS_ICON "${MHFU_PROFILE_DIR}/packaging/windows/emblem.ico")
    configure_file(packaging/windows/Pokee.rc.in Pokee.rc @ONLY)
    target_sources(Pokee PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/Pokee.rc")
endif()
if(MHFU_RENDERER)
    add_dependencies(Pokee mhfu_shaders mhfu_version)
    # The sink opens its own SDL audio subsystem, so it only needs SDL to exist.
    target_compile_definitions(Pokee PRIVATE MHFU_HAS_RENDERER=1 MHFU_HAS_SDL_AUDIO=1 MHFU_HAS_SDL=1)
    target_include_directories(Pokee PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}/generated_shaders"
        "${CMAKE_CURRENT_BINARY_DIR}/generated_version"
        "${MHFU_PROFILE_DIR}/third_party/imgui")
    target_link_libraries(Pokee PRIVATE SDL3::SDL3 Vulkan::Vulkan)
endif()
# The host only: the generated code never sees FFmpeg.
mhfu_use_ffmpeg(Pokee)
target_include_directories(Pokee PRIVATE
    # generated/ is first. main.cpp includes <corpus_identity.hpp> so this
    # directory wins over host/corpus_identity.hpp, which sits beside main.cpp
    # and would otherwise be found by a quoted include.
    "${MHFU_PROFILE_DIR}/generated"
    "${MHFU_PROFILE_DIR}/host"
    "${MHFU_PROFILE_DIR}/third_party")
target_link_libraries(Pokee PRIVATE psprecomp_core ${CMAKE_DL_LIBS})
if(WIN32)
    # The ad hoc client's and server's sockets, and the list of interfaces;
    # CommandLineToArgvW for the arguments as UTF-8.
    target_link_libraries(Pokee PRIVATE ws2_32 iphlpapi shell32)
endif()
find_package(Threads REQUIRED)
target_link_libraries(Pokee PRIVATE Threads::Threads)
# The overlay libraries resolve psprecomp and runtime symbols against the
# executable that loads them, so it has to export them.
set_target_properties(Pokee PROPERTIES
    ENABLE_EXPORTS TRUE
    WINDOWS_EXPORT_ALL_SYMBOLS TRUE)
# Only main.cpp needs the camera helper's unit; keeping it off the target keeps
# it off every other unit's command line.
if(MHFU_CAMERA_HELPER_UNIT)
    set_property(SOURCE host/main.cpp APPEND PROPERTY COMPILE_DEFINITIONS
        MHFU_CAMERA_HELPER_UNIT=${MHFU_CAMERA_HELPER_UNIT})
endif()
if(MHFU_HUD_INC)
    target_compile_definitions(Pokee PRIVATE
        "MHFU_HUD_FUNCTIONS_INC=\"${MHFU_HUD_INC}\"")
endif()
if(MHFU_LAYERED_INC)
    target_compile_definitions(Pokee PRIVATE
        "MHFU_LAYERED_FUNCTIONS_INC=\"${MHFU_LAYERED_INC}\"")
endif()
# The NID table is compiled in, so the program reads nothing from the checkout
# it was built in.
set(MHFU_NIDS_CSV "${PROJECT_SOURCE_DIR}/configs/nids.csv")
set(MHFU_NIDS_INC "${CMAKE_CURRENT_BINARY_DIR}/generated_nids/nid_table.inc")
add_custom_command(
    OUTPUT "${MHFU_NIDS_INC}"
    COMMAND ${CMAKE_COMMAND} -DINPUT=${MHFU_NIDS_CSV} -DOUTPUT=${MHFU_NIDS_INC}
            -P "${MHFU_PROFILE_DIR}/tools/embed_nids.cmake"
    DEPENDS "${MHFU_NIDS_CSV}" "${MHFU_PROFILE_DIR}/tools/embed_nids.cmake"
    COMMENT "Embedding the NID table")
target_sources(Pokee PRIVATE "${MHFU_NIDS_INC}")
target_include_directories(Pokee PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated_nids")

# The site hooks list (config/site_hooks.txt) is compiled in too: the
# recompiler reads it to emit the hooks, the host to know what each does.
set(MHFU_SITE_HOOKS "${MHFU_PROFILE_DIR}/config/site_hooks.txt")
set(MHFU_SITE_HOOKS_INC "${CMAKE_CURRENT_BINARY_DIR}/generated_sites/site_hooks.inc")
add_custom_command(
    OUTPUT "${MHFU_SITE_HOOKS_INC}"
    COMMAND ${CMAKE_COMMAND} -DINPUT=${MHFU_SITE_HOOKS} -DOUTPUT=${MHFU_SITE_HOOKS_INC}
            -P "${MHFU_PROFILE_DIR}/tools/embed_text.cmake"
    DEPENDS "${MHFU_SITE_HOOKS}" "${MHFU_PROFILE_DIR}/tools/embed_text.cmake"
    COMMENT "Embedding the site hooks list")
add_custom_target(mhfu_site_hooks DEPENDS "${MHFU_SITE_HOOKS_INC}")
add_dependencies(Pokee mhfu_site_hooks)
target_include_directories(Pokee PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated_sites")

# A release build runs from wherever it is unpacked: it has no fallback to
# this checkout's game directory, and on Linux it finds the libraries it ships
# in lib/ next to the executable and nothing else.
option(MHFU_RELEASE "Build for distribution: no checkout paths, libraries from lib/ next to the executable" OFF)
if(NOT MHFU_RELEASE)
    # The checkout's profiles/mhfu/game, a developer fallback after the
    # per-user data directory. Only main.cpp reads it; on the target it would be
    # on every generated unit's command line, so moving the checkout would
    # rebuild all of them and no other checkout could reuse their cache entries.
    set_property(SOURCE host/main.cpp APPEND PROPERTY COMPILE_DEFINITIONS
        MHFU_DEFAULT_GAME_DIR="${MHFU_PROFILE_DIR}/game")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set_target_properties(Pokee PROPERTIES
        BUILD_WITH_INSTALL_RPATH TRUE
        INSTALL_RPATH "$ORIGIN/lib")
    # The C++ runtime is linked in statically, so a build made against an old
    # glibc runs wherever that glibc does, and its symbols stay private: the
    # executable exports its own symbols to the overlay libraries, and a
    # second, exported libstdc++ would clash with the system's one that the
    # graphics driver loads.
    set(MHFU_RELEASE_LINK_OPTIONS -static-libstdc++ "LINKER:--exclude-libs,libstdc++.a:libsupc++.a")
    target_link_options(Pokee PRIVATE ${MHFU_RELEASE_LINK_OPTIONS})
endif()
set_target_properties(Pokee PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    JOB_POOL_COMPILE psprecomp_generated)

# Save-data checks that need no game data: crypto test vectors and round trips.
add_executable(mhfu_savedata_tests
    tests/savedata_tests.cpp
    host/save_data/aes128.cpp
    host/save_data/param_sfo.cpp
    host/save_data/savedata_crypto.cpp
    host/save_data/savedata_store.cpp
    host/save_data/save_transfer.cpp
    host/platform/utf8_path.cpp
    third_party/tiny_aes/aes.c)
target_include_directories(mhfu_savedata_tests PRIVATE
    "${MHFU_PROFILE_DIR}/host"
    "${MHFU_PROFILE_DIR}/third_party")
set_target_properties(mhfu_savedata_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(PSPRECOMP_BUILD_TESTS)
    add_test(NAME mhfu_savedata_tests COMMAND mhfu_savedata_tests)
endif()

# Camera dispatch, ownership and integration checks without game data.
add_executable(mhfu_camera_tests
    tests/analog_camera_tests.cpp
    host/camera/camera_input.cpp
    host/camera/game_camera.cpp
    host/camera/lock_on.cpp
    host/input/bindings.cpp
    host/input/chords.cpp
    host/input/presets.cpp
    host/input/touch_action.cpp
    host/input/touch_controls.cpp
    host/platform/utf8_path.cpp)
target_include_directories(mhfu_camera_tests PRIVATE "${MHFU_PROFILE_DIR}/host")
target_link_libraries(mhfu_camera_tests PRIVATE psprecomp_core)
set_target_properties(mhfu_camera_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(PSPRECOMP_BUILD_TESTS)
    add_test(NAME mhfu_camera_tests COMMAND mhfu_camera_tests)
endif()

# Lock-on: which monster a tap picks, and how it turns the game's camera.
add_executable(mhfu_lock_on_tests
    tests/lock_on_tests.cpp
    host/camera/camera_input.cpp
    host/camera/game_camera.cpp
    host/camera/lock_on.cpp
    host/input/bindings.cpp
    host/input/chords.cpp
    host/input/presets.cpp
    host/input/touch_action.cpp
    host/input/touch_controls.cpp
    host/platform/utf8_path.cpp)
target_include_directories(mhfu_lock_on_tests PRIVATE "${MHFU_PROFILE_DIR}/host")
target_link_libraries(mhfu_lock_on_tests PRIVATE psprecomp_core)
set_target_properties(mhfu_lock_on_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(PSPRECOMP_BUILD_TESTS)
    add_test(NAME mhfu_lock_on_tests COMMAND mhfu_lock_on_tests)
endif()

# The game's view shape: what is written to guest memory, and what is left.
add_executable(mhfu_aspect_tests
    tests/game_aspect_tests.cpp
    host/camera/game_aspect.cpp)
target_include_directories(mhfu_aspect_tests PRIVATE "${MHFU_PROFILE_DIR}/host")
target_link_libraries(mhfu_aspect_tests PRIVATE psprecomp_core)
set_target_properties(mhfu_aspect_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(PSPRECOMP_BUILD_TESTS)
    add_test(NAME mhfu_aspect_tests COMMAND mhfu_aspect_tests)
endif()

# The field of view: what its wrappers show the camera's updates, and what
# they leave the game.
add_executable(mhfu_field_of_view_tests
    tests/field_of_view_tests.cpp
    host/camera/field_of_view.cpp)
target_include_directories(mhfu_field_of_view_tests PRIVATE "${MHFU_PROFILE_DIR}/host")
target_link_libraries(mhfu_field_of_view_tests PRIVATE psprecomp_core)
set_target_properties(mhfu_field_of_view_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(PSPRECOMP_BUILD_TESTS)
    add_test(NAME mhfu_field_of_view_tests COMMAND mhfu_field_of_view_tests)
endif()

# Hiding the HUD: the resident words it requires, and the overlay functions
# that count only while those words are still there.
add_executable(mhfu_hud_tests
    tests/game_hud_tests.cpp
    host/gpu/game_hud.cpp
    host/settings/settings.cpp
    host/install/user_data.cpp
    host/app_paths.cpp
    host/input/bindings.cpp
    host/input/chords.cpp
    host/input/presets.cpp
    host/input/touch_action.cpp
    host/input/touch_controls.cpp
    host/platform/utf8_path.cpp)
target_include_directories(mhfu_hud_tests PRIVATE "${MHFU_PROFILE_DIR}/host")
target_link_libraries(mhfu_hud_tests PRIVATE psprecomp_core)
set_target_properties(mhfu_hud_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(PSPRECOMP_BUILD_TESTS)
    add_test(NAME mhfu_hud_tests COMMAND mhfu_hud_tests)
endif()

# Each platform's settings defaults.
add_executable(mhfu_settings_tests
    tests/settings_defaults_tests.cpp
    host/settings/settings.cpp
    host/install/user_data.cpp
    host/app_paths.cpp
    host/input/bindings.cpp
    host/input/chords.cpp
    host/input/presets.cpp
    host/input/touch_action.cpp
    host/input/touch_controls.cpp
    host/platform/utf8_path.cpp)
target_include_directories(mhfu_settings_tests PRIVATE "${MHFU_PROFILE_DIR}/host")
target_link_libraries(mhfu_settings_tests PRIVATE psprecomp_core)
set_target_properties(mhfu_settings_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(PSPRECOMP_BUILD_TESTS)
    add_test(NAME mhfu_settings_tests COMMAND mhfu_settings_tests)
endif()

# Interface sizes replayed over traced screens: each quad's piece, the rule
# that decided it and where it went must match tests/hud_scale_fixtures.
add_executable(mhfu_hud_scale_replay_tests tests/hud_scale_replay_tests.cpp)
target_include_directories(mhfu_hud_scale_replay_tests PRIVATE "${MHFU_PROFILE_DIR}/host")
target_compile_definitions(mhfu_hud_scale_replay_tests PRIVATE
    "MHFU_HUD_SCALE_FIXTURE_DIR=\"${MHFU_PROFILE_DIR}/tests/hud_scale_fixtures\"")
set_target_properties(mhfu_hud_scale_replay_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(PSPRECOMP_BUILD_TESTS)
    add_test(NAME mhfu_hud_scale_replay_tests COMMAND mhfu_hud_scale_replay_tests)
endif()

# Damage numbers: hitzone colour, the health a hit removes, the watch
# around that hit, and the MHFU_TRACE_HITS lines. No game data.
add_executable(mhfu_damage_numbers_tests
    tests/damage_numbers_tests.cpp
    host/game/damage_numbers.cpp
    host/game/damage_sync.cpp
    host/game/hit_trace.cpp)

# The MHFU_TRACE_BODIES trace: which fields of a dead monster count, and when
# a body is reported. No game data.
add_executable(mhfu_body_trace_tests
    tests/body_trace_tests.cpp
    host/game/body_trace.cpp)
target_include_directories(mhfu_body_trace_tests PRIVATE "${MHFU_PROFILE_DIR}/host")
target_link_libraries(mhfu_body_trace_tests PRIVATE psprecomp_core)
set_target_properties(mhfu_body_trace_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(PSPRECOMP_BUILD_TESTS)
    add_test(NAME mhfu_body_trace_tests COMMAND mhfu_body_trace_tests)
endif()

# Sharpness gauge: the colour and its fill, the weapon's colour ends through
# the game's tables, and where the gauge stands. No game data.
add_executable(mhfu_sharpness_gauge_tests
    tests/sharpness_gauge_tests.cpp
    host/game/sharpness_gauge.cpp
    host/game/guest_ram.cpp)
target_include_directories(mhfu_sharpness_gauge_tests PRIVATE "${MHFU_PROFILE_DIR}/host")
target_link_libraries(mhfu_sharpness_gauge_tests PRIVATE psprecomp_core)
set_target_properties(mhfu_sharpness_gauge_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(PSPRECOMP_BUILD_TESTS)
    add_test(NAME mhfu_sharpness_gauge_tests COMMAND mhfu_sharpness_gauge_tests)
endif()

# Gather marks: the radius the game searches, where the mark sits, and how it
# moves between game frames.
add_executable(mhfu_gather_icon_tests
    tests/gather_icon_tests.cpp
    host/game/hooks.cpp
    host/game/gather_art.cpp
    host/game/gather_icon.cpp
    host/game/damage_numbers.cpp
    host/game/hit_trace.cpp
    host/input/bindings.cpp
    host/input/chords.cpp
    host/input/presets.cpp
    host/input/touch_action.cpp
    host/input/touch_controls.cpp
    host/platform/utf8_path.cpp)
target_include_directories(mhfu_gather_icon_tests PRIVATE "${MHFU_PROFILE_DIR}/host")
target_link_libraries(mhfu_gather_icon_tests PRIVATE psprecomp_core)
set_target_properties(mhfu_gather_icon_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(PSPRECOMP_BUILD_TESTS)
    add_test(NAME mhfu_gather_icon_tests COMMAND mhfu_gather_icon_tests)
endif()
target_include_directories(mhfu_damage_numbers_tests PRIVATE "${MHFU_PROFILE_DIR}/host")
target_link_libraries(mhfu_damage_numbers_tests PRIVATE psprecomp_core)
set_target_properties(mhfu_damage_numbers_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(PSPRECOMP_BUILD_TESTS)
    add_test(NAME mhfu_damage_numbers_tests COMMAND mhfu_damage_numbers_tests)
endif()

# Item sets: loading a set through the box, the sets file, the screens'
# buttons, and the hooks and the game calls they make on stand-ins for the
# item box menu. No game data.
add_executable(mhfu_item_sets_tests
    tests/item_sets_tests.cpp
    host/game/game_data.cpp
    host/game/guest_calls.cpp
    host/game/item_sets.cpp
    host/game/hooks.cpp
    host/game/item_sets_menu.cpp
    host/game/guest_ram.cpp
    host/settings/settings.cpp
    host/install/user_data.cpp
    host/app_paths.cpp
    host/input/bindings.cpp
    host/input/chords.cpp
    host/input/presets.cpp
    host/input/touch_action.cpp
    host/input/touch_controls.cpp
    host/platform/utf8_path.cpp)
target_include_directories(mhfu_item_sets_tests PRIVATE "${MHFU_PROFILE_DIR}/host")
target_link_libraries(mhfu_item_sets_tests PRIVATE psprecomp_core)
set_target_properties(mhfu_item_sets_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(PSPRECOMP_BUILD_TESTS)
    add_test(NAME mhfu_item_sets_tests COMMAND mhfu_item_sets_tests)
endif()

# Fourth pouch page: the site list built in, what each action does to the
# registers, the blocks beside the game's arrays, the pages file, and the
# frame, save and load on a runtime. No game data.
add_executable(mhfu_pouch_page_tests
    tests/pouch_page_tests.cpp
    host/game/pouch_page.cpp
    host/game/game_data.cpp
    host/game/guest_calls.cpp
    host/game/item_sets.cpp
    host/game/hooks.cpp
    host/game/item_sets_menu.cpp
    host/game/guest_ram.cpp
    host/settings/settings.cpp
    host/install/user_data.cpp
    host/app_paths.cpp
    host/input/bindings.cpp
    host/input/chords.cpp
    host/input/presets.cpp
    host/input/touch_action.cpp
    host/input/touch_controls.cpp
    host/platform/utf8_path.cpp)
add_dependencies(mhfu_pouch_page_tests mhfu_site_hooks)
target_include_directories(mhfu_pouch_page_tests PRIVATE
    "${MHFU_PROFILE_DIR}/host" "${CMAKE_CURRENT_BINARY_DIR}/generated_sites")
target_link_libraries(mhfu_pouch_page_tests PRIVATE psprecomp_core)
set_target_properties(mhfu_pouch_page_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(PSPRECOMP_BUILD_TESTS)
    add_test(NAME mhfu_pouch_page_tests COMMAND mhfu_pouch_page_tests)
endif()

# Fast item box: the shortened wait, the lobby_task code check, and the wrapper
# around a stand-in transfer menu. No game data.
add_executable(mhfu_item_box_tests
    tests/item_box_tests.cpp
    host/game/hooks.cpp
    host/game/item_box.cpp
    host/settings/settings.cpp
    host/install/user_data.cpp
    host/app_paths.cpp
    host/input/bindings.cpp
    host/input/chords.cpp
    host/input/presets.cpp
    host/input/touch_action.cpp
    host/input/touch_controls.cpp
    host/platform/utf8_path.cpp)
target_include_directories(mhfu_item_box_tests PRIVATE "${MHFU_PROFILE_DIR}/host")
target_link_libraries(mhfu_item_box_tests PRIVATE psprecomp_core)
set_target_properties(mhfu_item_box_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(PSPRECOMP_BUILD_TESTS)
    add_test(NAME mhfu_item_box_tests COMMAND mhfu_item_box_tests)
endif()

# Give at any rarity: the item lookup, the code check, and the wrapper
# around a stand-in check. No game data.
add_executable(mhfu_give_rarity_tests
    tests/give_rarity_tests.cpp
    host/game/give_rarity.cpp
    host/settings/settings.cpp
    host/install/user_data.cpp
    host/app_paths.cpp
    host/input/bindings.cpp
    host/input/chords.cpp
    host/input/presets.cpp
    host/input/touch_action.cpp
    host/input/touch_controls.cpp
    host/platform/utf8_path.cpp)
target_include_directories(mhfu_give_rarity_tests PRIVATE "${MHFU_PROFILE_DIR}/host")
target_link_libraries(mhfu_give_rarity_tests PRIVATE psprecomp_core)
set_target_properties(mhfu_give_rarity_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(PSPRECOMP_BUILD_TESTS)
    add_test(NAME mhfu_give_rarity_tests COMMAND mhfu_give_rarity_tests)
endif()

# True attack: the code check and the factor table. No game data.
add_executable(mhfu_true_attack_tests
    tests/true_attack_tests.cpp
    host/game/true_attack.cpp
    host/settings/settings.cpp
    host/install/user_data.cpp
    host/app_paths.cpp
    host/input/bindings.cpp
    host/input/chords.cpp
    host/input/presets.cpp
    host/input/touch_action.cpp
    host/input/touch_controls.cpp
    host/platform/utf8_path.cpp)
target_include_directories(mhfu_true_attack_tests PRIVATE "${MHFU_PROFILE_DIR}/host")
target_link_libraries(mhfu_true_attack_tests PRIVATE psprecomp_core)
set_target_properties(mhfu_true_attack_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(PSPRECOMP_BUILD_TESTS)
    add_test(NAME mhfu_true_attack_tests COMMAND mhfu_true_attack_tests)
endif()

# True element: the format rewrite, the code check and the wrapper around a
# stand-in sprintf. No game data.
add_executable(mhfu_true_element_tests
    tests/true_element_tests.cpp
    host/game/true_element.cpp
    host/game/hooks.cpp
    host/settings/settings.cpp
    host/install/user_data.cpp
    host/app_paths.cpp
    host/input/bindings.cpp
    host/input/chords.cpp
    host/input/presets.cpp
    host/input/touch_action.cpp
    host/input/touch_controls.cpp
    host/platform/utf8_path.cpp)
target_include_directories(mhfu_true_element_tests PRIVATE "${MHFU_PROFILE_DIR}/host")
target_link_libraries(mhfu_true_element_tests PRIVATE psprecomp_core)
set_target_properties(mhfu_true_element_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(PSPRECOMP_BUILD_TESTS)
    add_test(NAME mhfu_true_element_tests COMMAND mhfu_true_element_tests)
endif()

# Wrappers that share a guest function: two in either order run once each,
# and a function registered again starts a new chain. No game data.
add_executable(mhfu_hooks_tests
    tests/hooks_tests.cpp
    host/game/hooks.cpp)
target_include_directories(mhfu_hooks_tests PRIVATE "${MHFU_PROFILE_DIR}/host")
target_link_libraries(mhfu_hooks_tests PRIVATE psprecomp_core)
set_target_properties(mhfu_hooks_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(PSPRECOMP_BUILD_TESTS)
    add_test(NAME mhfu_hooks_tests COMMAND mhfu_hooks_tests)
endif()

# Change Equipment in the Guild Hall: the button read, the refused choice, the
# label, the code check, and the wrappers around a stand-in box. No game data.
add_executable(mhfu_hall_equipment_tests
    tests/hall_equipment_tests.cpp
    host/game/hall_equipment.cpp
    host/game/hooks.cpp
    host/settings/settings.cpp
    host/install/user_data.cpp
    host/app_paths.cpp
    host/input/bindings.cpp
    host/input/chords.cpp
    host/input/presets.cpp
    host/input/touch_action.cpp
    host/input/touch_controls.cpp
    host/platform/utf8_path.cpp)
target_include_directories(mhfu_hall_equipment_tests PRIVATE "${MHFU_PROFILE_DIR}/host")
target_link_libraries(mhfu_hall_equipment_tests PRIVATE psprecomp_core)
set_target_properties(mhfu_hall_equipment_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(PSPRECOMP_BUILD_TESTS)
    add_test(NAME mhfu_hall_equipment_tests COMMAND mhfu_hall_equipment_tests)
endif()

add_executable(mhfu_hall_meal_tests
    tests/hall_meal_tests.cpp
    host/game/hall_meal.cpp
    host/game/hooks.cpp
    host/settings/settings.cpp
    host/install/user_data.cpp
    host/app_paths.cpp
    host/input/bindings.cpp
    host/input/chords.cpp
    host/input/presets.cpp
    host/input/touch_action.cpp
    host/input/touch_controls.cpp
    host/platform/utf8_path.cpp)
target_include_directories(mhfu_hall_meal_tests PRIVATE "${MHFU_PROFILE_DIR}/host")
target_link_libraries(mhfu_hall_meal_tests PRIVATE psprecomp_core)
set_target_properties(mhfu_hall_meal_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(PSPRECOMP_BUILD_TESTS)
    add_test(NAME mhfu_hall_meal_tests COMMAND mhfu_hall_meal_tests)
endif()

# Key quests: a quest pack's key names found, a pack found in memory, tagged
# and put back, and the switch. Built packs only; no game data.
add_executable(mhfu_key_quests_tests
    tests/key_quests_tests.cpp
    host/game/key_quests.cpp
    host/settings/settings.cpp
    host/install/user_data.cpp
    host/app_paths.cpp
    host/input/bindings.cpp
    host/input/chords.cpp
    host/input/presets.cpp
    host/input/touch_action.cpp
    host/input/touch_controls.cpp
    host/platform/utf8_path.cpp)
target_include_directories(mhfu_key_quests_tests PRIVATE "${MHFU_PROFILE_DIR}/host")
target_link_libraries(mhfu_key_quests_tests PRIVATE psprecomp_core)
set_target_properties(mhfu_key_quests_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(PSPRECOMP_BUILD_TESTS)
    add_test(NAME mhfu_key_quests_tests COMMAND mhfu_key_quests_tests)
endif()

# Built-in event quests: quest files, the getter and lookup on a guest memory
# laid out as the save's slots, and the list's extension. No game data.
add_executable(mhfu_event_quests_tests
    tests/event_quests_tests.cpp
    host/game/event_quest_import.cpp
    host/game/event_quests.cpp
    host/platform/utf8_path.cpp)
target_include_directories(mhfu_event_quests_tests PRIVATE "${MHFU_PROFILE_DIR}/host")
target_link_libraries(mhfu_event_quests_tests PRIVATE psprecomp_core)
set_target_properties(mhfu_event_quests_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(PSPRECOMP_BUILD_TESTS)
    add_test(NAME mhfu_event_quests_tests COMMAND mhfu_event_quests_tests)
endif()

# Served Data Install: the packing, reads across its ranges, the file names
# and the title menu without its entry. No game data; --check compares the packing with a real install.
add_executable(mhfu_install_data_tests
    tests/install_data_tests.cpp
    host/game/install_data.cpp
    host/game/title_menu.cpp
    host/mods/mhfu_data_bin.cpp
    host/mods/file_overlay.cpp
    host/mods/mod_ini.cpp)
target_include_directories(mhfu_install_data_tests PRIVATE "${MHFU_PROFILE_DIR}/host")
target_link_libraries(mhfu_install_data_tests PRIVATE psprecomp_core)
set_target_properties(mhfu_install_data_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(PSPRECOMP_BUILD_TESTS)
    add_test(NAME mhfu_install_data_tests COMMAND mhfu_install_data_tests)
endif()

# Empty item slot: the scroll with an empty stop, the code and caller checks,
# and the wrapper around a stand-in step. No game data.
add_executable(mhfu_empty_item_slot_tests
    tests/empty_item_slot_tests.cpp
    host/game/empty_item_slot.cpp
    host/settings/settings.cpp
    host/install/user_data.cpp
    host/app_paths.cpp
    host/input/bindings.cpp
    host/input/chords.cpp
    host/input/presets.cpp
    host/input/touch_action.cpp
    host/input/touch_controls.cpp
    host/platform/utf8_path.cpp)
target_include_directories(mhfu_empty_item_slot_tests PRIVATE "${MHFU_PROFILE_DIR}/host")
target_link_libraries(mhfu_empty_item_slot_tests PRIVATE psprecomp_core)
set_target_properties(mhfu_empty_item_slot_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(PSPRECOMP_BUILD_TESTS)
    add_test(NAME mhfu_empty_item_slot_tests COMMAND mhfu_empty_item_slot_tests)
endif()

# Installer release identities, and preparation of a supported executable when
# a disc image path is passed. The default run uses no game data.
add_executable(mhfu_install_identity_tests
    tests/install_identity_tests.cpp
    host/install/executable_preparation.cpp
    host/kernel/iso_image.cpp
    host/platform/utf8_path.cpp
    third_party/tiny_aes/aes.c)
target_include_directories(mhfu_install_identity_tests PRIVATE
    "${MHFU_PROFILE_DIR}/host"
    "${MHFU_PROFILE_DIR}/third_party")
target_link_libraries(mhfu_install_identity_tests PRIVATE psprecomp_core)
set_target_properties(mhfu_install_identity_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(PSPRECOMP_BUILD_TESTS)
    add_test(NAME mhfu_install_identity_tests COMMAND mhfu_install_identity_tests)
endif()

# What several features read in the game: the overlay in a slot, the camera
# object and its hunter, the quest entity list. No game data.
add_executable(mhfu_guest_layout_tests tests/guest_layout_tests.cpp host/game/guest_ram.cpp)
target_include_directories(mhfu_guest_layout_tests PRIVATE "${MHFU_PROFILE_DIR}/host")
target_link_libraries(mhfu_guest_layout_tests PRIVATE psprecomp_core)
set_target_properties(mhfu_guest_layout_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(PSPRECOMP_BUILD_TESTS)
    add_test(NAME mhfu_guest_layout_tests COMMAND mhfu_guest_layout_tests)
endif()

# Freedom Unite overlay bases. Header only: no game data.
add_executable(mhfu_overlay_slot_tests tests/overlay_slot_tests.cpp)
target_include_directories(mhfu_overlay_slot_tests PRIVATE "${MHFU_PROFILE_DIR}/host")
target_link_libraries(mhfu_overlay_slot_tests PRIVATE psprecomp_core)
set_target_properties(mhfu_overlay_slot_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(PSPRECOMP_BUILD_TESTS)
    add_test(NAME mhfu_overlay_slot_tests COMMAND mhfu_overlay_slot_tests)
endif()

# Where the data directory is (portable or per-user) and the copy of an
# installed copy's data into a portable folder. No game data.
add_executable(mhfu_data_directory_tests
    tests/data_directory_tests.cpp
    host/install/user_data.cpp
    host/app_paths.cpp
    host/platform/utf8_path.cpp)
target_include_directories(mhfu_data_directory_tests PRIVATE "${MHFU_PROFILE_DIR}/host")
target_link_libraries(mhfu_data_directory_tests PRIVATE psprecomp_core)
set_target_properties(mhfu_data_directory_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(PSPRECOMP_BUILD_TESTS)
    add_test(NAME mhfu_data_directory_tests COMMAND mhfu_data_directory_tests)
endif()

# Screenshots: their names, and the PNG files written. Works in a temporary
# directory; no game data.
add_executable(mhfu_screenshot_tests
    tests/screenshot_tests.cpp
    host/gpu/screenshot.cpp
    host/install/user_data.cpp
    host/app_paths.cpp
    host/platform/utf8_path.cpp)
target_include_directories(mhfu_screenshot_tests PRIVATE
    "${MHFU_PROFILE_DIR}/host"
    "${MHFU_PROFILE_DIR}/third_party")
target_link_libraries(mhfu_screenshot_tests PRIVATE psprecomp_core Threads::Threads)
set_target_properties(mhfu_screenshot_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(PSPRECOMP_BUILD_TESTS)
    add_test(NAME mhfu_screenshot_tests COMMAND mhfu_screenshot_tests)
endif()

# Fast loading's decision: when a load runs ahead of real time.
add_executable(mhfu_fast_loading_tests
    tests/fast_loading_tests.cpp
    host/kernel/load_detector.cpp)
target_include_directories(mhfu_fast_loading_tests PRIVATE "${MHFU_PROFILE_DIR}/host")
set_target_properties(mhfu_fast_loading_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(PSPRECOMP_BUILD_TESTS)
    add_test(NAME mhfu_fast_loading_tests COMMAND mhfu_fast_loading_tests)
endif()

# Fast-forward's switch: hold, toggle, and what keeps real time.
add_executable(mhfu_fast_forward_tests
    tests/fast_forward_tests.cpp
    host/kernel/fast_forward_switch.cpp)
target_include_directories(mhfu_fast_forward_tests PRIVATE "${MHFU_PROFILE_DIR}/host")
set_target_properties(mhfu_fast_forward_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(PSPRECOMP_BUILD_TESTS)
    add_test(NAME mhfu_fast_forward_tests COMMAND mhfu_fast_forward_tests)
endif()

# The on-screen touch controls: layout, fingers and the stick's maths.
add_executable(mhfu_touch_tests
    tests/touch_controls_tests.cpp
    host/input/touch_action.cpp
    host/input/touch_controls.cpp)
target_include_directories(mhfu_touch_tests PRIVATE "${MHFU_PROFILE_DIR}/host")
set_target_properties(mhfu_touch_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(PSPRECOMP_BUILD_TESTS)
    add_test(NAME mhfu_touch_tests COMMAND mhfu_touch_tests)
endif()

# Texture pack import: finding a pack in the layouts packs come in, the
# checks made before copying, and the copy, swap and backup; and the PSP's DXT
# block layout. No game data.
add_executable(mhfu_texture_pack_tests
    tests/texture_pack_tests.cpp
    host/game/button_glyphs.cpp
    host/gpu/texture_decode.cpp
    host/gpu/texture_pack.cpp
    host/gpu/texture_pack_import.cpp
    host/platform/utf8_path.cpp)
target_include_directories(mhfu_texture_pack_tests PRIVATE
    "${MHFU_PROFILE_DIR}/host"
    "${MHFU_PROFILE_DIR}/host/gpu"
    "${MHFU_PROFILE_DIR}/third_party")
target_link_libraries(mhfu_texture_pack_tests PRIVATE psprecomp_core Threads::Threads)
set_target_properties(mhfu_texture_pack_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(PSPRECOMP_BUILD_TESTS)
    add_test(NAME mhfu_texture_pack_tests COMMAND mhfu_texture_pack_tests)
endif()

# Keyboard and mouse bindings: names, settings.ini spelling, the menu's
# assignment rules and what held keys press. No SDL or game data.
add_executable(mhfu_input_tests
    tests/input_tests.cpp
    host/input/bindings.cpp
    host/input/chords.cpp
    host/input/presets.cpp)
target_include_directories(mhfu_input_tests PRIVATE "${MHFU_PROFILE_DIR}/host")
set_target_properties(mhfu_input_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(PSPRECOMP_BUILD_TESTS)
    add_test(NAME mhfu_input_tests COMMAND mhfu_input_tests)
endif()

# Mappings for gamepads SDL does not know (#147): the setup's detection and
# the mapping lines and file it writes. No SDL.
add_executable(mhfu_gamepad_mapping_tests
    tests/gamepad_mapping_tests.cpp
    host/input/gamepad_mapping.cpp)
target_include_directories(mhfu_gamepad_mapping_tests PRIVATE "${MHFU_PROFILE_DIR}/host")
set_target_properties(mhfu_gamepad_mapping_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(PSPRECOMP_BUILD_TESTS)
    add_test(NAME mhfu_gamepad_mapping_tests COMMAND mhfu_gamepad_mapping_tests)
endif()

# Mods: the community's mod folders, choices, conflicts and import, the
# DATA.BIN obfuscation and an archive served with mods' files. No game data.
add_executable(mhfu_mods_tests
    tests/mods_tests.cpp
    host/mods/file_overlay.cpp
    host/mods/mhfu_data_bin.cpp
    host/mods/mhfu_mod_format.cpp
    host/mods/mod_import.cpp
    host/mods/mod_ini.cpp
    host/mods/mod_library.cpp
    host/mods/mod_session.cpp)
target_include_directories(mhfu_mods_tests PRIVATE "${MHFU_PROFILE_DIR}/host")
set_target_properties(mhfu_mods_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(PSPRECOMP_BUILD_TESTS)
    add_test(NAME mhfu_mods_tests COMMAND mhfu_mods_tests)
endif()

# Which file holds a piece of equipment's model, read from the game's tables
# as equipment mods need it, on a buffer standing for guest memory. No game data.
add_executable(mhfu_equipment_models_tests
    tests/equipment_models_tests.cpp
    host/game/game_data.cpp
    host/game/equipment_models.cpp
    host/game/layered_armor.cpp)
target_include_directories(mhfu_equipment_models_tests PRIVATE "${MHFU_PROFILE_DIR}/host")
set_target_properties(mhfu_equipment_models_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(PSPRECOMP_BUILD_TESTS)
    add_test(NAME mhfu_equipment_models_tests COMMAND mhfu_equipment_models_tests)
endif()

# Renderer helpers that need no GPU.
# MMPX, the 2D interface's pixel-art upscaler (host/gpu/mmpx.hpp).
add_executable(mhfu_mmpx_tests tests/mmpx_tests.cpp host/gpu/mmpx.cpp)
target_include_directories(mhfu_mmpx_tests PRIVATE "${MHFU_PROFILE_DIR}/host")
set_target_properties(mhfu_mmpx_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(PSPRECOMP_BUILD_TESTS)
    add_test(NAME mhfu_mmpx_tests COMMAND mhfu_mmpx_tests)
endif()

# Per-frame vertex budget: the larger one only when the device can spare it.
add_executable(mhfu_vertex_budget_tests tests/vertex_budget_tests.cpp)
target_include_directories(mhfu_vertex_budget_tests PRIVATE "${MHFU_PROFILE_DIR}/host")
set_target_properties(mhfu_vertex_budget_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(PSPRECOMP_BUILD_TESTS)
    add_test(NAME mhfu_vertex_budget_tests COMMAND mhfu_vertex_budget_tests)
endif()

add_executable(mhfu_vertex_hold_tests tests/vertex_hold_tests.cpp)
target_include_directories(mhfu_vertex_hold_tests PRIVATE "${MHFU_PROFILE_DIR}/host")
set_target_properties(mhfu_vertex_hold_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(PSPRECOMP_BUILD_TESTS)
    add_test(NAME mhfu_vertex_hold_tests COMMAND mhfu_vertex_hold_tests)
endif()

# Which cached spans give way when the vertex cache is full, and the spare
# vectors later misses reuse. No GPU is used.
add_executable(mhfu_vertex_cache_tests tests/vertex_cache_tests.cpp)
target_include_directories(mhfu_vertex_cache_tests PRIVATE "${MHFU_PROFILE_DIR}/host")
set_target_properties(mhfu_vertex_cache_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(PSPRECOMP_BUILD_TESTS)
    add_test(NAME mhfu_vertex_cache_tests COMMAND mhfu_vertex_cache_tests)
endif()

# Descriptor pools counted the way a phone's driver counts them: growth,
# freeing, a driver that refuses early, the pool limit. No GPU is used.
if(Vulkan_FOUND)
    add_executable(mhfu_descriptor_pool_tests tests/descriptor_pool_tests.cpp host/gpu/descriptor_pools.cpp)
    target_include_directories(mhfu_descriptor_pool_tests PRIVATE "${MHFU_PROFILE_DIR}/host"
                                                                     ${Vulkan_INCLUDE_DIRS})
    target_link_libraries(mhfu_descriptor_pool_tests PRIVATE Vulkan::Vulkan)
    set_target_properties(mhfu_descriptor_pool_tests PROPERTIES
                          RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    if(PSPRECOMP_BUILD_TESTS)
        add_test(NAME mhfu_descriptor_pool_tests COMMAND mhfu_descriptor_pool_tests)
    endif()
endif()

add_executable(mhfu_ge_state_tests tests/ge_state_tests.cpp host/gpu/ge_state.cpp)
target_include_directories(mhfu_ge_state_tests PRIVATE "${MHFU_PROFILE_DIR}/host")
target_link_libraries(mhfu_ge_state_tests PRIVATE psprecomp_core)
set_target_properties(mhfu_ge_state_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(PSPRECOMP_BUILD_TESTS)
    add_test(NAME mhfu_ge_state_tests COMMAND mhfu_ge_state_tests)
    add_custom_target(mhfu_ge_state_slow_tests DEPENDS mhfu_ge_state_tests)
    add_test(NAME mhfu_ge_state_slow_tests COMMAND mhfu_ge_state_tests)
    set_tests_properties(mhfu_ge_state_slow_tests PROPERTIES ENVIRONMENT "MHFU_NO_FAST_DECODE=1")
    add_custom_target(mhfu_ge_state_checked_tests DEPENDS mhfu_ge_state_tests)
    add_test(NAME mhfu_ge_state_checked_tests COMMAND mhfu_ge_state_tests)
    set_tests_properties(mhfu_ge_state_checked_tests PROPERTIES
        ENVIRONMENT "MHFU_CHECK_DECODE=1;MHFU_TRACE_MATERIAL=1;MHFU_TRACE_LIGHTING=1;MHFU_TRACE_FB_TEXTURES=1"
        TIMEOUT 30 FAIL_REGULAR_EXPRESSION "FAIL:;\\[decode-check\\].*differs over")
endif()

add_executable(mhfu_texture_decode_tests
    tests/texture_decode_tests.cpp
    host/gpu/texture_decode.cpp)
target_include_directories(mhfu_texture_decode_tests PRIVATE "${MHFU_PROFILE_DIR}/host")
target_link_libraries(mhfu_texture_decode_tests PRIVATE psprecomp_core)
set_target_properties(mhfu_texture_decode_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(PSPRECOMP_BUILD_TESTS)
    add_test(NAME mhfu_texture_decode_tests COMMAND mhfu_texture_decode_tests)
    add_custom_target(mhfu_texture_decode_slow_tests DEPENDS mhfu_texture_decode_tests)
    add_test(NAME mhfu_texture_decode_slow_tests COMMAND mhfu_texture_decode_tests)
    set_tests_properties(mhfu_texture_decode_slow_tests PROPERTIES ENVIRONMENT "MHFU_NO_FAST_TEXTURE_DECODE=1;MHFU_NO_BUFFER_REUSE=1")
    add_custom_target(mhfu_texture_decode_checked_tests DEPENDS mhfu_texture_decode_tests)
    add_test(NAME mhfu_texture_decode_checked_tests COMMAND mhfu_texture_decode_tests)
    set_tests_properties(mhfu_texture_decode_checked_tests PROPERTIES
        ENVIRONMENT "MHFU_CHECK_TEXTURE_DECODE=1" TIMEOUT 30
        FAIL_REGULAR_EXPRESSION "FAIL:;[1-9][0-9]* differed")
endif()

add_executable(mhfu_render_tests tests/render_tests.cpp)
target_include_directories(mhfu_render_tests PRIVATE "${MHFU_PROFILE_DIR}/host")
target_link_libraries(mhfu_render_tests PRIVATE psprecomp_core)
set_target_properties(mhfu_render_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(PSPRECOMP_BUILD_TESTS)
    add_test(NAME mhfu_render_tests COMMAND mhfu_render_tests)
endif()

# Frame interpolation checks: matching draws, cuts, blending, the present
# clock and the frame rate governor.
add_executable(mhfu_interpolation_tests
    tests/interpolation_tests.cpp
    host/gpu/frame_interpolation.cpp
    host/gpu/frame_pacing.cpp)
target_include_directories(mhfu_interpolation_tests PRIVATE
    "${MHFU_PROFILE_DIR}/host"
    "${PROJECT_SOURCE_DIR}/include")
set_target_properties(mhfu_interpolation_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(PSPRECOMP_BUILD_TESTS)
    add_test(NAME mhfu_interpolation_tests COMMAND mhfu_interpolation_tests)
endif()

# The built-in ad hoc server against the client and a raw protocol peer, and
# local network discovery, all over the loopback address.
add_executable(mhfu_adhoc_tests
    tests/adhoc_tests.cpp
    host/adhoc/client.cpp
    host/adhoc/discovery.cpp
    host/adhoc/server.cpp)
target_include_directories(mhfu_adhoc_tests PRIVATE "${MHFU_PROFILE_DIR}/host")
target_link_libraries(mhfu_adhoc_tests PRIVATE Threads::Threads)
if(WIN32)
    target_link_libraries(mhfu_adhoc_tests PRIVATE ws2_32 iphlpapi)
endif()
set_target_properties(mhfu_adhoc_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(PSPRECOMP_BUILD_TESTS)
    add_test(NAME mhfu_adhoc_tests COMMAND mhfu_adhoc_tests)
endif()

# Files under folder names outside ASCII: a Cyrillic user folder, a Japanese
# folder for the disc image. Settings, saves, texture packs, fonts, the disc
# image and the executable, in a temporary folder. No game data.
add_executable(mhfu_path_tests
    tests/path_tests.cpp
    host/app_paths.cpp
    host/fonts/game_font.cpp
    host/gpu/screenshot.cpp
    host/gpu/texture_pack.cpp
    host/gpu/texture_pack_import.cpp
    host/input/bindings.cpp
    host/input/chords.cpp
    host/input/presets.cpp
    host/input/touch_action.cpp
    host/input/touch_controls.cpp
    host/install/user_data.cpp
    host/kernel/iso_image.cpp
    host/platform/utf8_path.cpp
    host/save_data/aes128.cpp
    host/save_data/param_sfo.cpp
    host/save_data/savedata_crypto.cpp
    host/save_data/savedata_store.cpp
    host/save_data/save_transfer.cpp
    host/settings/settings.cpp
    third_party/tiny_aes/aes.c)
target_include_directories(mhfu_path_tests PRIVATE
    "${MHFU_PROFILE_DIR}/host"
    "${MHFU_PROFILE_DIR}/host/gpu"
    "${MHFU_PROFILE_DIR}/third_party")
target_link_libraries(mhfu_path_tests PRIVATE psprecomp_core Threads::Threads)
if(WIN32)
    target_link_libraries(mhfu_path_tests PRIVATE shell32)
endif()
set_target_properties(mhfu_path_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(PSPRECOMP_BUILD_TESTS)
    add_test(NAME mhfu_path_tests COMMAND mhfu_path_tests)
endif()

# The host's sources are UTF-8: its text literals (menu text, the Japanese
# sample the font picker checks fonts with) must stay UTF-8 under MSVC, which
# otherwise reads sources in the system's ANSI code page. The recompiled code
# is ASCII and keeps its own flags.
if(MSVC)
    get_property(mhfu_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
    foreach(target IN LISTS mhfu_targets)
        get_target_property(target_type ${target} TYPE)
        if((target STREQUAL "Pokee" OR target MATCHES "^mhfu_.*_tests$")
                AND NOT target_type STREQUAL "UTILITY")
            target_compile_options(${target} PRIVATE /utf-8)
        endif()
    endforeach()
endif()

# Chained AOT calls nest native frames; give the guest-executing main thread
# the same 64 MiB stack the Windows profiles use.
if(MSVC)
    target_link_options(Pokee PRIVATE /STACK:67108864)
elseif(APPLE)
    target_link_options(Pokee PRIVATE "LINKER:-stack_size,0x4000000")
endif()

# Recompiled overlay corpora, produced by tools/add_overlay.py. Each directory
# under overlays/ holds one corpus and becomes one shared module that the host
# loads at run time, so a new overlay never relinks the executable.
set(MHFU_OVERLAY_DIR "${CMAKE_BINARY_DIR}/bin/overlays")

function(mhfu_add_overlay meta_path)
    get_filename_component(source_dir "${meta_path}" DIRECTORY)
    get_filename_component(MHFU_OVERLAY_PREFIX "${source_dir}" NAME)
    file(STRINGS "${meta_path}" meta_lines REGEX "^(base|size|code_size|hash|name|dispatch)=")
    set(MHFU_OVERLAY_DISPATCH "")
    foreach(line IN LISTS meta_lines)
        string(REGEX MATCH "^([a-z_]+)=(.+)$" matched "${line}")
        if(NOT matched)
            continue()
        endif()
        string(TOUPPER "${CMAKE_MATCH_1}" key)
        set(MHFU_OVERLAY_${key} "${CMAKE_MATCH_2}")
    endforeach()
    if(NOT MHFU_OVERLAY_BASE OR NOT MHFU_OVERLAY_SIZE OR NOT MHFU_OVERLAY_CODE_SIZE
       OR NOT MHFU_OVERLAY_HASH OR NOT MHFU_OVERLAY_NAME)
        message(WARNING "mhfu: incomplete overlay metadata in ${meta_path}")
        return()
    endif()
    # The calls generated through the runtime (config/overlay_dispatch.txt), as
    # C++ array elements for the entry point.
    set(MHFU_OVERLAY_DISPATCH_LIST "")
    separate_arguments(dispatched UNIX_COMMAND "${MHFU_OVERLAY_DISPATCH}")
    foreach(address IN LISTS dispatched)
        string(APPEND MHFU_OVERLAY_DISPATCH_LIST "${address}u, ")
    endforeach()

    file(GLOB sources CONFIGURE_DEPENDS "${source_dir}/*.cpp")
    if(NOT sources)
        message(WARNING "mhfu: no sources for overlay ${MHFU_OVERLAY_PREFIX}")
        return()
    endif()
    set(entry_point "${CMAKE_CURRENT_BINARY_DIR}/overlays/${MHFU_OVERLAY_PREFIX}_module.cpp")
    configure_file("${MHFU_PROFILE_DIR}/host/overlay_module.cpp.in" "${entry_point}" @ONLY)

    set(target "overlay_${MHFU_OVERLAY_PREFIX}")
    add_library(${target} MODULE ${sources} "${entry_point}")
    # Headers only: linking psprecomp_core would give the module its own copy of
    # the runtime state the host already owns.
    target_include_directories(${target} PRIVATE
        "${source_dir}" "${MHFU_PROFILE_DIR}/host" "${PROJECT_SOURCE_DIR}/include")
    if(WIN32 OR APPLE OR MHFU_ANDROID_APP)
        # Both linkers want the host binary while linking the module: MSVC for
        # its import library, ld64 as the bundle loader. ELF leaves the host
        # symbols undefined and resolves them when the module is loaded. In an
        # Android app the host is libmain.so, which the app loads privately,
        # so the module names it as a dependency to find its symbols.
        target_link_libraries(${target} PRIVATE Pokee)
    else()
        add_dependencies(${target} Pokee)
        if(ANDROID)
            # The NDK links every shared library with --no-undefined; these
            # leave the host's symbols to the loader like any other ELF.
            target_link_options(${target} PRIVATE "LINKER:-z,undefs")
        endif()
    endif()
    if(MSVC)
        target_compile_definitions(${target} PRIVATE PSPRECOMP_IMPORT_HOST_SYMBOLS=1)
        if(PSPRECOMP_GENERATED_OPT_LEVEL STREQUAL "0")
            target_compile_options(${target} PRIVATE /Od /bigobj)
        else()
            target_compile_options(${target} PRIVATE /O${PSPRECOMP_GENERATED_OPT_LEVEL} /Ob0 /bigobj)
        endif()
    else()
        target_compile_options(${target} PRIVATE -O${PSPRECOMP_GENERATED_OPT_LEVEL} -g0)
    endif()
    if(MHFU_RELEASE_LINK_OPTIONS)
        target_link_options(${target} PRIVATE ${MHFU_RELEASE_LINK_OPTIONS})
    endif()
    set_target_properties(${target} PROPERTIES
        JOB_POOL_COMPILE psprecomp_generated
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON
        PREFIX ""
        SUFFIX "${CMAKE_SHARED_LIBRARY_SUFFIX}"
        OUTPUT_NAME "${MHFU_OVERLAY_PREFIX}"
        LIBRARY_OUTPUT_DIRECTORY "${MHFU_OVERLAY_DIR}")
    if(MHFU_ANDROID_APP)
        # An APK only carries libraries named lib*.so.
        set_target_properties(${target} PROPERTIES PREFIX "lib")
    endif()
endfunction()

file(GLOB MHFU_OVERLAY_METADATA CONFIGURE_DEPENDS "${MHFU_PROFILE_DIR}/overlays/*/meta.txt")
foreach(meta IN LISTS MHFU_OVERLAY_METADATA)
    mhfu_add_overlay("${meta}")
endforeach()
list(LENGTH MHFU_OVERLAY_METADATA MHFU_OVERLAY_COUNT)
message(STATUS "mhfu: ${MHFU_OVERLAY_COUNT} overlay librar(ies) in ${MHFU_OVERLAY_DIR}")

# Guest PCM staging validates the full mapped range before allocating.
add_executable(mhfu_guest_pcm_tests
    tests/guest_pcm_tests.cpp
    host/audio/guest_pcm.cpp)
target_include_directories(mhfu_guest_pcm_tests PRIVATE "${MHFU_PROFILE_DIR}/host")
target_link_libraries(mhfu_guest_pcm_tests PRIVATE psprecomp_core)
set_target_properties(mhfu_guest_pcm_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(PSPRECOMP_BUILD_TESTS)
    add_test(NAME mhfu_guest_pcm_tests COMMAND mhfu_guest_pcm_tests)
endif()

# How long a blocking audio output waits, draining a channel included.
add_executable(mhfu_audio_channel_queue_tests
    tests/audio_channel_queue_tests.cpp
    host/audio/channel_queue.cpp)
target_include_directories(mhfu_audio_channel_queue_tests PRIVATE "${MHFU_PROFILE_DIR}/host")
set_target_properties(mhfu_audio_channel_queue_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(PSPRECOMP_BUILD_TESTS)
    add_test(NAME mhfu_audio_channel_queue_tests COMMAND mhfu_audio_channel_queue_tests)
endif()

# PNG size bounds, stride handling and allocation failure recovery.
add_executable(mhfu_png_size_tests tests/png_size_tests.cpp)
target_include_directories(mhfu_png_size_tests PRIVATE "${MHFU_PROFILE_DIR}/third_party")
set_target_properties(mhfu_png_size_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(PSPRECOMP_BUILD_TESTS)
    add_test(NAME mhfu_png_size_tests COMMAND mhfu_png_size_tests)
endif()

# Synthetic 16-bit PNG conversion and checked allocation bounds.
add_executable(mhfu_image_conversion_tests tests/image_conversion_tests.cpp)
target_include_directories(mhfu_image_conversion_tests PRIVATE "${MHFU_PROFILE_DIR}/third_party")
set_target_properties(mhfu_image_conversion_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(PSPRECOMP_BUILD_TESTS)
    add_test(NAME mhfu_image_conversion_tests COMMAND mhfu_image_conversion_tests)
endif()

# Exercise the actual vendor debug formatter, including 64-bit texture IDs.
add_executable(mhfu_imgui_security_tests
    tests/imgui_security_tests.cpp
    third_party/imgui/imgui_draw.cpp
    third_party/imgui/imgui_widgets.cpp
    third_party/imgui/imgui_tables.cpp)
target_include_directories(mhfu_imgui_security_tests PRIVATE "${MHFU_PROFILE_DIR}/third_party/imgui")
set_target_properties(mhfu_imgui_security_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(PSPRECOMP_BUILD_TESTS)
    add_test(NAME mhfu_imgui_security_tests COMMAND mhfu_imgui_security_tests)
endif()

# Both independently maintained stb_truetype copies use the same synthetic font cases.
foreach(font_test IN ITEMS mhfu_font_bitmap_tests mhfu_imgui_font_bitmap_tests)
    add_executable(${font_test} tests/font_bitmap_security_tests.cpp)
    target_include_directories(${font_test} PRIVATE
        "${MHFU_PROFILE_DIR}/third_party" "${MHFU_PROFILE_DIR}/third_party/imgui")
    set_target_properties(${font_test} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    if(PSPRECOMP_BUILD_TESTS)
        add_test(NAME ${font_test} COMMAND ${font_test})
    endif()
endforeach()
target_compile_definitions(mhfu_imgui_font_bitmap_tests PRIVATE TEST_IMGUI_STB)

# Synthetic HLE suites link the real headless host with a public empty registry.
add_library(mhfu_test_host STATIC ${MHFU_HOST_SOURCES} host/generated_stub.cpp "${MHFU_NIDS_INC}")
add_dependencies(mhfu_test_host mhfu_site_hooks)
target_include_directories(mhfu_test_host PUBLIC
    "${MHFU_PROFILE_DIR}/host" "${MHFU_PROFILE_DIR}/third_party"
    "${CMAKE_CURRENT_BINARY_DIR}/generated_nids" "${CMAKE_CURRENT_BINARY_DIR}/generated_sites")
target_link_libraries(mhfu_test_host PUBLIC psprecomp_core Threads::Threads ${CMAKE_DL_LIBS})
mhfu_use_ffmpeg(mhfu_test_host)
if(WIN32)
    target_link_libraries(mhfu_test_host PUBLIC ws2_32 iphlpapi shell32)
endif()
# The stall trace's kinds and the [slow-frame] line, on the headless host.
add_executable(mhfu_frame_stats_tests tests/frame_stats_tests.cpp)
target_link_libraries(mhfu_frame_stats_tests PRIVATE mhfu_test_host)
set_target_properties(mhfu_frame_stats_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(PSPRECOMP_BUILD_TESTS)
    add_test(NAME mhfu_frame_stats_tests COMMAND mhfu_frame_stats_tests)
endif()
add_executable(mhfu_kernel_hle_tests tests/kernel_hle_tests.cpp)
target_compile_definitions(mhfu_kernel_hle_tests PRIVATE PSPRECOMP_TEST_NIDS_CSV="${PROJECT_SOURCE_DIR}/configs/nids.csv")
target_link_libraries(mhfu_kernel_hle_tests PRIVATE mhfu_test_host)

# Two synthetic overlay modules sharing one slot. The host resolves their
# framework symbols from this test, the way a game build resolves them from Pokee.
if(NOT WIN32)
    set(MHFU_OVERLAY_FIXTURE_DIR "${CMAKE_BINARY_DIR}/bin/overlay-fixtures")
    foreach(mark IN ITEMS 1 2)
        set(fixture_target "mhfu_overlay_fixture_${mark}")
        add_library(${fixture_target} MODULE tests/overlay_swap_module.cpp)
        target_include_directories(${fixture_target} PRIVATE
            "${MHFU_PROFILE_DIR}/host" "${PROJECT_SOURCE_DIR}/include")
        target_compile_definitions(${fixture_target} PRIVATE "MHFU_OVERLAY_FIXTURE_MARK=${mark}")
        # The test loads the fixtures, so it cannot be their bundle loader the
        # way Pokee is a real overlay's: ld64 leaves the framework symbols to
        # the loader instead, and the NDK's --no-undefined lets them through as
        # for a real overlay.
        if(APPLE)
            target_link_options(${fixture_target} PRIVATE "LINKER:-undefined,dynamic_lookup")
        elseif(ANDROID)
            target_link_options(${fixture_target} PRIVATE "LINKER:-z,undefs")
        endif()
        set_target_properties(${fixture_target} PROPERTIES
            CXX_VISIBILITY_PRESET hidden
            VISIBILITY_INLINES_HIDDEN ON
            PREFIX ""
            LIBRARY_OUTPUT_DIRECTORY "${MHFU_OVERLAY_FIXTURE_DIR}"
            OUTPUT_NAME "fixture_${mark}")
    endforeach()
    add_executable(mhfu_overlay_swap_tests tests/overlay_swap_tests.cpp)
    target_compile_definitions(mhfu_overlay_swap_tests PRIVATE
        "MHFU_OVERLAY_FIXTURE_DIR=\"${MHFU_OVERLAY_FIXTURE_DIR}\"")
    target_include_directories(mhfu_overlay_swap_tests PRIVATE "${MHFU_PROFILE_DIR}/host")
    target_link_libraries(mhfu_overlay_swap_tests PRIVATE mhfu_test_host)
    if(APPLE)
        target_link_options(mhfu_overlay_swap_tests PRIVATE "LINKER:-export_dynamic")
    else()
        target_link_options(mhfu_overlay_swap_tests PRIVATE "LINKER:--export-dynamic")
    endif()
    add_dependencies(mhfu_overlay_swap_tests mhfu_overlay_fixture_1 mhfu_overlay_fixture_2)
    set_target_properties(mhfu_overlay_swap_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    if(PSPRECOMP_BUILD_TESTS)
        add_test(NAME mhfu_overlay_swap_tests COMMAND mhfu_overlay_swap_tests)
        set_tests_properties(mhfu_overlay_swap_tests PROPERTIES TIMEOUT 60)
    endif()
endif()
set_target_properties(mhfu_kernel_hle_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(PSPRECOMP_BUILD_TESTS)
    add_test(NAME mhfu_kernel_hle_tests COMMAND mhfu_kernel_hle_tests)
    set_tests_properties(mhfu_kernel_hle_tests PROPERTIES TIMEOUT 60 ENVIRONMENT "MHFU_NO_RENDER=1;MHFU_TRACE_OSK=1")
endif()

add_executable(mhfu_media_tests
    tests/media_tests.cpp
    host/audio/atrac_decoder.cpp
    host/movie/avc_decoder.cpp
    host/audio/sas_core.cpp
    host/movie/psmf_demuxer.cpp)
target_include_directories(mhfu_media_tests PRIVATE "${MHFU_PROFILE_DIR}/host")
target_link_libraries(mhfu_media_tests PRIVATE psprecomp_core)
mhfu_use_ffmpeg(mhfu_media_tests)
set_target_properties(mhfu_media_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(PSPRECOMP_BUILD_PROFILE_TESTS)
    add_test(NAME mhfu_media_tests COMMAND mhfu_media_tests)
endif()

add_executable(mhfu_media_hle_tests tests/media_hle_tests.cpp)
target_compile_definitions(mhfu_media_hle_tests PRIVATE PSPRECOMP_TEST_NIDS_CSV="${PROJECT_SOURCE_DIR}/configs/nids.csv")
target_link_libraries(mhfu_media_hle_tests PRIVATE mhfu_test_host)
set_target_properties(mhfu_media_hle_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(PSPRECOMP_BUILD_TESTS)
    add_test(NAME mhfu_media_hle_tests COMMAND mhfu_media_hle_tests)
    set_tests_properties(mhfu_media_hle_tests PROPERTIES TIMEOUT 60 ENVIRONMENT "MHFU_NO_RENDER=1;MHFU_NO_AUDIO=1")
endif()

# Real public-buffer renderer/UI contracts are opt-in: use software Vulkan and
# a bounded Xvfb display in CI, or an explicitly selected local GPU.
option(MHFU_RENDERER_TESTS "Run real SDL/Vulkan public-buffer renderer tests" OFF)
if(MHFU_RENDERER_TESTS)
    if(NOT MHFU_RENDERER)
        message(FATAL_ERROR "Renderer tests require MHFU_RENDERER=ON")
    endif()
    add_executable(mhfu_renderer_ui_tests tests/renderer_ui_tests.cpp
        ${MHFU_HOST_SOURCES} ${MHFU_RENDERER_SOURCES}
        $<TARGET_OBJECTS:mhfu_generated>)
    add_dependencies(mhfu_renderer_ui_tests mhfu_shaders mhfu_version)
    target_sources(mhfu_renderer_ui_tests PRIVATE "${MHFU_NIDS_INC}")
    target_compile_definitions(mhfu_renderer_ui_tests PRIVATE MHFU_HAS_RENDERER=1 MHFU_HAS_SDL_AUDIO=1 MHFU_HAS_SDL=1
        PSPRECOMP_TEST_NIDS_CSV="${PROJECT_SOURCE_DIR}/configs/nids.csv")
    target_include_directories(mhfu_renderer_ui_tests PRIVATE
        "${MHFU_PROFILE_DIR}/host" "${MHFU_PROFILE_DIR}/third_party"
        "${MHFU_PROFILE_DIR}/third_party/imgui"
        "${CMAKE_CURRENT_BINARY_DIR}/generated_shaders"
        "${CMAKE_CURRENT_BINARY_DIR}/generated_version"
        "${CMAKE_CURRENT_BINARY_DIR}/generated_nids")
    target_link_libraries(mhfu_renderer_ui_tests PRIVATE psprecomp_core SDL3::SDL3 Vulkan::Vulkan Threads::Threads ${CMAKE_DL_LIBS})
    mhfu_use_ffmpeg(mhfu_renderer_ui_tests)
    if(WIN32)
        target_link_libraries(mhfu_renderer_ui_tests PRIVATE ws2_32 iphlpapi shell32)
    endif()
    set_target_properties(mhfu_renderer_ui_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    if(PSPRECOMP_BUILD_TESTS)
        add_test(NAME mhfu_renderer_ui_tests COMMAND mhfu_renderer_ui_tests)
        set_tests_properties(mhfu_renderer_ui_tests PROPERTIES TIMEOUT 90)
        add_custom_target(mhfu_input_script_tests DEPENDS mhfu_renderer_ui_tests)
        add_test(NAME mhfu_input_script_tests COMMAND mhfu_renderer_ui_tests --input-script)
        set_tests_properties(mhfu_input_script_tests PROPERTIES TIMEOUT 45)
        add_custom_target(mhfu_input_script_static_tests DEPENDS mhfu_renderer_ui_tests)
        add_test(NAME mhfu_input_script_static_tests COMMAND mhfu_renderer_ui_tests --input-script-static)
        set_tests_properties(mhfu_input_script_static_tests PROPERTIES TIMEOUT 45)
        add_custom_target(mhfu_ui_backend_lifecycle_tests DEPENDS mhfu_renderer_ui_tests)
        add_test(NAME mhfu_ui_backend_lifecycle_tests COMMAND mhfu_renderer_ui_tests --ui-lifecycle)
        set_tests_properties(mhfu_ui_backend_lifecycle_tests PROPERTIES TIMEOUT 45)
        add_custom_target(mhfu_renderer_texture_cache_tests DEPENDS mhfu_renderer_ui_tests)
        add_test(NAME mhfu_renderer_texture_cache_tests COMMAND mhfu_renderer_ui_tests --texture-cache)
        set_tests_properties(mhfu_renderer_texture_cache_tests PROPERTIES TIMEOUT 45
            ENVIRONMENT "MHFU_TEXTURE_CACHE_LIMIT=2")
        # Exercise the documented conservative path even when Auto keeps it off.
        add_custom_target(mhfu_renderer_ui_compat_tests DEPENDS mhfu_renderer_ui_tests)
        add_test(NAME mhfu_renderer_ui_compat_tests COMMAND mhfu_renderer_ui_tests)
        set_tests_properties(mhfu_renderer_ui_compat_tests PROPERTIES
            TIMEOUT 90
            ENVIRONMENT "MHFU_GPU_COMPAT=on")
        # Documented rollback paths must preserve the same independent pixel contracts.
        add_custom_target(mhfu_renderer_ui_legacy_tests DEPENDS mhfu_renderer_ui_tests)
        add_test(NAME mhfu_renderer_ui_legacy_tests COMMAND mhfu_renderer_ui_tests)
        set_tests_properties(mhfu_renderer_ui_legacy_tests PROPERTIES
            TIMEOUT 90
            ENVIRONMENT "MHFU_PAD_FOLLOW=0;MHFU_NO_DIRECT_VERTICES=1;MHFU_NO_LOOKUP_CACHE=1;MHFU_NO_BUFFER_REUSE=1;MHFU_NO_DRAW_MERGE=1;MHFU_NO_FAST_STORE=1;MHFU_NO_ALPHA_VARIANTS=1;MHFU_NO_CLEAR_LOAD=1;MHFU_SYNC_UPLOADS=1;MHFU_SYNC_TEXTURE_DECODE=1")
        # The same public buffers also check shader decode and direct index expansion
        # against independently specified host vertices in the diagnostic paths.
        add_custom_target(mhfu_renderer_ui_checked_tests DEPENDS mhfu_renderer_ui_tests)
        add_test(NAME mhfu_renderer_ui_checked_tests COMMAND mhfu_renderer_ui_tests)
        set_tests_properties(mhfu_renderer_ui_checked_tests PROPERTIES
            TIMEOUT 90
            ENVIRONMENT "MHFU_CHECK_DIRECT_VERTICES=1;MHFU_CHECK_GPU_DECODE=1;MHFU_CHECK_REPLAY=1;MHFU_TRACE_FB_TEXTURES=1;MHFU_TRACE_3D=1"
            PASS_REGULAR_EXPRESSION "replay check at frame.*: 0 of"
            FAIL_REGULAR_EXPRESSION "FAIL:;\\[gpu-decode-check\\].*differs;\\[direct-check\\] draw.*differ;replay check at frame.*: [1-9][0-9]* of")
    endif()
endif()
