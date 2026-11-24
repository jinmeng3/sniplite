cmake_minimum_required(VERSION 3.21)
project(sniplite_qr LANGUAGES C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(ZXING_READERS ON CACHE BOOL "" FORCE)
set(ZXING_WRITERS OFF CACHE STRING "" FORCE)
set(ZXING_C_API ON CACHE BOOL "" FORCE)
set(ZXING_ENABLE_1D OFF CACHE BOOL "" FORCE)
set(ZXING_ENABLE_AZTEC OFF CACHE BOOL "" FORCE)
set(ZXING_ENABLE_DATAMATRIX OFF CACHE BOOL "" FORCE)
set(ZXING_ENABLE_MAXICODE OFF CACHE BOOL "" FORCE)
set(ZXING_ENABLE_PDF417 OFF CACHE BOOL "" FORCE)
set(ZXING_ENABLE_QRCODE ON CACHE BOOL "" FORCE)

add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../../third_party/zxing-cpp/core"
                 "${CMAKE_CURRENT_BINARY_DIR}/zxing")

add_executable(sniplite_qr
    "${CMAKE_CURRENT_LIST_DIR}/../qr_main.c"
    "${CMAKE_CURRENT_LIST_DIR}/../pixel_ops.S"
)
target_link_libraries(sniplite_qr PRIVATE ZXing::ZXing ole32 uuid windowscodecs)
set_target_properties(sniplite_qr PROPERTIES LINKER_LANGUAGE CXX)

if(MINGW)
    target_link_options(sniplite_qr PRIVATE
        -municode
        -Wl,--subsystem,console
        -static-libgcc
        -static-libstdc++
    )
endif()
