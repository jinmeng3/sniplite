cmake_minimum_required(VERSION 3.16)
project(sniplite_microtex LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(MT_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../third_party/microtex")
set(XML_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../third_party/tinyxml2")
file(GLOB MT_SOURCES CONFIGURE_DEPENDS
    "${MT_ROOT}/src/atom/*.cpp" "${MT_ROOT}/src/box/*.cpp"
    "${MT_ROOT}/src/core/*.cpp" "${MT_ROOT}/src/fonts/*.cpp"
    "${MT_ROOT}/src/utils/*.cpp" "${MT_ROOT}/src/res/builtin/*.cpp"
    "${MT_ROOT}/src/res/font/*.cpp" "${MT_ROOT}/src/res/parser/*.cpp"
    "${MT_ROOT}/src/res/reg/*.cpp" "${MT_ROOT}/src/res/sym/*.cpp")
add_library(sniplite_microtex SHARED microtex.cpp ${MT_SOURCES}
    "${MT_ROOT}/src/latex.cpp" "${MT_ROOT}/src/render.cpp"
    "${MT_ROOT}/src/platform/gdi_win/graphic_win32.cpp" "${XML_ROOT}/tinyxml2.cpp")
target_include_directories(sniplite_microtex PRIVATE "${MT_ROOT}/src" "${XML_ROOT}")
target_compile_definitions(sniplite_microtex PRIVATE MICROTEX_BUILD BUILD_WIN32
    NOMINMAX WIN32_LEAN_AND_MEAN NDEBUG)
target_link_libraries(sniplite_microtex PRIVATE gdiplus gdi32 user32)
if(MINGW)
    target_compile_options(sniplite_microtex PRIVATE -O2 -ffunction-sections -fdata-sections -fno-ident)
    target_link_options(sniplite_microtex PRIVATE -static-libgcc -static-libstdc++ -static
        -s -Wl,--gc-sections,--exclude-all-symbols,--nxcompat,--dynamicbase,--high-entropy-va,--no-insert-timestamp)
endif()
set_target_properties(sniplite_microtex PROPERTIES PREFIX "" OUTPUT_NAME "sniplite_microtex")
