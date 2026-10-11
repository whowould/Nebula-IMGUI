cmake_minimum_required(VERSION 3.20)
project(Nebula LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/third_party/imgui)

# FreeType (vendored, built static, no optional dependencies): hinted glyphs for sharper small text.
set(FT_DISABLE_ZLIB     ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BZIP2    ON CACHE BOOL "" FORCE)
set(FT_DISABLE_PNG      ON CACHE BOOL "" FORCE)
set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BROTLI   ON CACHE BOOL "" FORCE)
set(SKIP_INSTALL_ALL    ON CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS   OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/freetype EXCLUDE_FROM_ALL)
# The DLL version resource is useless for a static lib, and windres can't handle spaces in include paths.
set_source_files_properties(third_party/freetype/src/base/ftver.rc DIRECTORY third_party/freetype PROPERTIES HEADER_FILE_ONLY ON)

add_executable(nebula WIN32
    src/main.cpp
    src/theme.cpp
    src/widgets.cpp
    src/menu/shell.cpp
    src/menu/stats.cpp
    src/menu/page_home.cpp
    src/menu/page_visuals.cpp
    src/menu/page_audio.cpp
    src/menu/page_controls.cpp
    src/menu/page_configs.cpp
    src/menu/page_settings.cpp
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
    ${IMGUI_DIR}/backends/imgui_impl_dx9.cpp
    ${IMGUI_DIR}/misc/freetype/imgui_freetype.cpp
)

target_include_directories(nebula PRIVATE src ${IMGUI_DIR} ${IMGUI_DIR}/backends ${IMGUI_DIR}/misc/freetype)
target_compile_definitions(nebula PRIVATE
    UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN
    _WIN32_WINNT=0x0A00 WINVER=0x0A00 PSAPI_VERSION=2
    IMGUI_DEFINE_MATH_OPERATORS IMGUI_ENABLE_FREETYPE)
target_link_libraries(nebula PRIVATE freetype d3d9 dwmapi shell32 psapi windowscodecs ole32 uuid advapi32)

if(MSVC)
    target_compile_options(nebula PRIVATE /utf-8 /W3)
else()
    target_link_options(nebula PRIVATE -static -static-libgcc -static-libstdc++)
endif()
