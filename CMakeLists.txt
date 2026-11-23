cmake_minimum_required(VERSION 3.20)
project(hdslave_desktop_clap LANGUAGES C CXX)

if(APPLE)
    enable_language(OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CLAP_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/third_party/clap/include")
set(DESKTOP_DIR "${CMAKE_SOURCE_DIR}/desktop")

# Build Nexus's vendored Plaits DSP sources as a static library (matches Nexus/Makefile)
file(GLOB_RECURSE NEXUS_EURORACK_SOURCES
    "${CMAKE_SOURCE_DIR}/plugins/nexus/third_party/eurorack/*.cc"
)
list(FILTER NEXUS_EURORACK_SOURCES EXCLUDE REGEX "plaits/dsp/voice\\.cc$")

add_library(nexus_eurorack STATIC
    ${NEXUS_EURORACK_SOURCES}
    "${CMAKE_SOURCE_DIR}/plugins/nexus/src/plaits_voice.cc"
)
target_compile_definitions(nexus_eurorack PRIVATE TEST _USE_MATH_DEFINES NOMINMAX)
target_include_directories(nexus_eurorack PUBLIC
    "${CMAKE_SOURCE_DIR}/plugins/nexus/third_party/eurorack"
    "${CMAKE_SOURCE_DIR}/plugins/nexus/src"
)
set_target_properties(nexus_eurorack PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(MSVC)
    target_compile_options(nexus_eurorack PRIVATE /O2 /EHsc /W0)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_compile_options(nexus_eurorack PRIVATE -Wno-c++11-narrowing -w)
    endif()
else()
    target_compile_options(nexus_eurorack PRIVATE -O3 -w)
endif()

# Helper function to create a desktop CLAP plugin target for macOS (.clap bundle) & Windows (.clap DLL)
function(add_hdslave_clap_plugin TARGET_NAME SLUG DISPLAY_NAME PLUGIN_DIR MAIN_SRC)
    set(EXTRA_SRCS ${ARGN})

    # Compile the inner MPC plugin's main .cpp with clap_entry renamed to hdslave_inner_clap_entry
    set(INNER_OBJ_TARGET "${SLUG}_inner")
    add_library(${INNER_OBJ_TARGET} OBJECT
        "${PLUGIN_DIR}/${MAIN_SRC}"
        ${EXTRA_SRCS}
    )
    target_compile_definitions(${INNER_OBJ_TARGET} PRIVATE clap_entry=hdslave_inner_clap_entry)
    target_include_directories(${INNER_OBJ_TARGET} PRIVATE
        "${CLAP_INCLUDE_DIR}"
        "${PLUGIN_DIR}/src"
    )
    set_target_properties(${INNER_OBJ_TARGET} PROPERTIES POSITION_INDEPENDENT_CODE ON)

    if(SLUG STREQUAL "nexus")
        target_compile_definitions(${INNER_OBJ_TARGET} PRIVATE TEST)
        target_include_directories(${INNER_OBJ_TARGET} PRIVATE
            "${PLUGIN_DIR}/third_party/eurorack"
        )
    endif()

    if(MSVC)
        target_include_directories(${INNER_OBJ_TARGET} PRIVATE "${DESKTOP_DIR}/win32/compat")
        target_compile_options(${INNER_OBJ_TARGET} PRIVATE /O2 /EHsc /W2 /FI"${DESKTOP_DIR}/win32/compat/msvc_compat.h" /wd4244 /wd4267 /wd4305 /wd4996)
        if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
            target_compile_options(${INNER_OBJ_TARGET} PRIVATE -Wno-c++11-narrowing -Wno-deprecated-declarations -Wno-unused-command-line-argument)
        endif()
    else()
        target_compile_options(${INNER_OBJ_TARGET} PRIVATE -O3 -ffp-contract=fast -fno-math-errno -fno-trapping-math -fvisibility=hidden)
    endif()

    # Desktop shell sources (unique Cocoa/Win32 class name per plugin so all 11 can run side-by-side in FL Studio!)
    set(SHELL_SRCS
        "${DESKTOP_DIR}/desktop_wrapper.cpp"
        "${DESKTOP_DIR}/panel_ui.cpp"
    )
    if(APPLE)
        list(APPEND SHELL_SRCS "${DESKTOP_DIR}/gui_macos.mm")
    elseif(WIN32)
        list(APPEND SHELL_SRCS "${DESKTOP_DIR}/gui_win32.cpp" "${DESKTOP_DIR}/win32/clap.def")
    endif()

    add_library(${TARGET_NAME} MODULE
        $<TARGET_OBJECTS:${INNER_OBJ_TARGET}>
        ${SHELL_SRCS}
    )

    target_include_directories(${TARGET_NAME} PRIVATE
        "${CLAP_INCLUDE_DIR}"
        "${DESKTOP_DIR}"
    )

    if(SLUG STREQUAL "nexus")
        target_link_libraries(${TARGET_NAME} PRIVATE nexus_eurorack)
    endif()

    if(APPLE)
        set(BUNDLE_NAME "${TARGET_NAME}")
        set(BUNDLE_SLUG "${SLUG}")
        set(PLIST_OUT "${CMAKE_BINARY_DIR}/plists/${TARGET_NAME}-Info.plist")
        configure_file("${DESKTOP_DIR}/macos/Info.plist.in" "${PLIST_OUT}" @ONLY)

        target_compile_definitions(${TARGET_NAME} PRIVATE
            HDSLAVE_OBJC_VIEW_CLASS=HdslavePanelView_${SLUG}
        )
        target_compile_options(${TARGET_NAME} PRIVATE -O3 -fvisibility=hidden)
        target_link_libraries(${TARGET_NAME} PRIVATE
            "-framework Cocoa"
            "-framework CoreGraphics"
            "-framework QuartzCore"
        )
        set_target_properties(${TARGET_NAME} PROPERTIES
            BUNDLE TRUE
            BUNDLE_EXTENSION "clap"
            MACOSX_BUNDLE_INFO_PLIST "${PLIST_OUT}"
            PREFIX ""
            OUTPUT_NAME "${TARGET_NAME}"
            LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/clap"
        )
    elseif(WIN32)
        target_compile_definitions(${TARGET_NAME} PRIVATE
            HDSLAVE_WIN32_CLASS_NAME=L"HdslaveDesktopClapView_${SLUG}"
            UNICODE
            _UNICODE
        )
        if(MSVC)
            target_compile_options(${TARGET_NAME} PRIVATE /O2 /EHsc /W3 /wd4244 /wd4996)
        else()
            target_compile_options(${TARGET_NAME} PRIVATE -O3)
        endif()
        target_link_libraries(${TARGET_NAME} PRIVATE user32 gdi32)
        set_target_properties(${TARGET_NAME} PROPERTIES
            PREFIX ""
            SUFFIX ".clap"
            OUTPUT_NAME "${TARGET_NAME}"
            LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/clap"
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/clap"
        )
    endif()
endfunction()

# Register all 11 hdslave CLAP plugins
add_hdslave_clap_plugin(Anvil          anvil          "hdslave Anvil"          "${CMAKE_SOURCE_DIR}/plugins/anvil"         src/anvil.cpp)
add_hdslave_clap_plugin(CortexFilter   cortex_filter  "hdslave Cortex Filter"  "${CMAKE_SOURCE_DIR}/plugins/cortex_filter" src/cortex.cpp)
add_hdslave_clap_plugin(Foundry        foundry        "hdslave Foundry"        "${CMAKE_SOURCE_DIR}/plugins/foundry"       src/foundry.cpp)
add_hdslave_clap_plugin(Machine        machine        "hdslave Machine"        "${CMAKE_SOURCE_DIR}/plugins/machine"       src/machine.cpp)
add_hdslave_clap_plugin(MoonBass       moonbass       "hdslave MoonBass"       "${CMAKE_SOURCE_DIR}/plugins/moonbass"      src/moonbass.cpp)
add_hdslave_clap_plugin(Mystik         mystik         "hdslave Mystik"         "${CMAKE_SOURCE_DIR}/plugins/mystik"        src/mystik.cpp)
add_hdslave_clap_plugin(Necronomicon   necronomicon   "hdslave Necronomicon"   "${CMAKE_SOURCE_DIR}/plugins/necronomicon"  src/necronomicon.cpp)
add_hdslave_clap_plugin(Nexus          nexus          "hdslave Nexus"          "${CMAKE_SOURCE_DIR}/plugins/nexus"         src/nexus.cpp)
add_hdslave_clap_plugin(Psychic        psychic        "hdslave Psychic"        "${CMAKE_SOURCE_DIR}/plugins/psychic"       src/psychic.cpp       "${CMAKE_SOURCE_DIR}/plugins/psychic/src/screen.cpp")
add_hdslave_clap_plugin(Reverie        reverie        "hdslave Reverie"        "${CMAKE_SOURCE_DIR}/plugins/reverie"       src/reverie.cpp       "${CMAKE_SOURCE_DIR}/plugins/reverie/src/screen.cpp")
add_hdslave_clap_plugin(TB-MPC-3-P-0   tb_mpc_3p0     "hdslave TB-MPC-3-P-0"   "${CMAKE_SOURCE_DIR}/plugins/tb_mpc_3p0"    src/tbmpc.cpp)

# Cross-platform CLAP smoke test host
add_executable(host_test "${CMAKE_SOURCE_DIR}/tests/host_test.cpp")
target_include_directories(host_test PRIVATE "${CLAP_INCLUDE_DIR}")
if(APPLE)
    target_link_libraries(host_test PRIVATE dl)
endif()
