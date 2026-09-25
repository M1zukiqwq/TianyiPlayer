cmake_minimum_required(VERSION 3.21)

project(TianyiPlayer
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# ---- Player engine: SemiPlayer (git submodule) ----
# Only the C ABI dynamic library is needed; tests/examples stay off.
set(SEMI_BUILD_DLL ON CACHE BOOL "" FORCE)
set(SEMI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SEMI_BUILD_SDL_PLAYER OFF CACHE BOOL "" FORCE)
set(SEMI_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/semi_player)

# ---- UI dependencies ----
find_package(SDL3 CONFIG REQUIRED)
find_package(SDL3_image CONFIG REQUIRED)
find_package(SDL3_ttf CONFIG REQUIRED)

add_executable(tianyi_player
    src/main.cpp
    src/ui/draw.cpp
    src/player/player_controller.cpp
)
target_compile_features(tianyi_player PRIVATE cxx_std_23)
target_include_directories(tianyi_player PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(tianyi_player PRIVATE
    semi_player
    SDL3::SDL3
    SDL3_image::SDL3_image
    SDL3_ttf::SDL3_ttf
)

set_target_properties(tianyi_player PROPERTIES
    BUILD_RPATH "${CMAKE_BINARY_DIR}/bin"
    INSTALL_RPATH "$ORIGIN"
)

# Copy runtime DLLs and theme assets next to the executable.
if(WIN32)
    add_custom_command(TARGET tianyi_player POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:semi_player>
            $<TARGET_FILE_DIR:tianyi_player>
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:SDL3::SDL3>
            $<TARGET_FILE_DIR:tianyi_player>
        COMMENT "Copy runtime libraries next to TianyiPlayer"
    )

    # Pull in the MinGW/MSYS2 runtime the app links against (FFmpeg, spdlog,
    # SDL3_image/ttf, libgcc/libstdc++). Names are version-suffixed, so glob.
    get_filename_component(SEMI_TOOLCHAIN_BIN "${CMAKE_CXX_COMPILER}" DIRECTORY)
    set(TIANYI_RUNTIME_PATTERNS
        "avcodec-*.dll" "avformat-*.dll" "avutil-*.dll"
        "swresample-*.dll" "swscale-*.dll"
        "SDL3_image.dll" "SDL3_ttf.dll"
        "libspdlog-*.dll" "libgcc_s_seh-*.dll" "libstdc++-*.dll" "libwinpthread-*.dll"
    )
    set(TIANYI_RUNTIME_DLLS "")
    foreach(pattern IN LISTS TIANYI_RUNTIME_PATTERNS)
        file(GLOB matched "${SEMI_TOOLCHAIN_BIN}/${pattern}")
        list(APPEND TIANYI_RUNTIME_DLLS ${matched})
    endforeach()
    if(TIANYI_RUNTIME_DLLS)
        add_custom_command(TARGET tianyi_player POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${TIANYI_RUNTIME_DLLS}
                $<TARGET_FILE_DIR:tianyi_player>
            COMMENT "Copy MinGW/MSYS2 runtime DLLs next to TianyiPlayer"
        )
    endif()
endif()

add_custom_command(TARGET tianyi_player POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/assets
        $<TARGET_FILE_DIR:tianyi_player>/assets
    COMMENT "Copy theme assets next to TianyiPlayer"
)
