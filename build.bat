@echo off
rem Build sniplite: x86-64 assembly where speed matters (capture, composition, drawing,
rem the capture overlay, images: src\*.S, GNU as through gcc) and C for the rest
rem (src\*.c). No C runtime: src\crt.c has the few library functions the C uses.
rem Each program carries its texts in every language (src\lang.c, src\lang_text.h).
rem Uses the project-local w64devkit when present, otherwise MinGW-w64 on PATH. Output goes to build\.
setlocal
cd /d "%~dp0"
if exist "%~dp0w64devkit\bin\gcc.exe" set "PATH=%~dp0w64devkit\bin;%PATH%"
where gcc >nul 2>nul || (echo gcc not found: put w64devkit in the project root or install MinGW-w64 on PATH & exit /b 1)
if not exist build mkdir build

rem src\start.S is the entry point (start); every program links only what it uses.
set CF=-Isrc -Ires -std=c11 -Os -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers ^
 -D_WIN32_WINNT=0x0A00 -DWINVER=0x0A00 -fno-math-errno -ffunction-sections -fdata-sections -fno-ident
set APP_LDFLAGS=-nostdlib -nostartfiles -e start -s -Wl,--gc-sections -Wl,--nxcompat -Wl,--dynamicbase ^
 -Wl,--high-entropy-va

windres --input-format=rc -O coff -I res res\sniplite.rc build\sniplite.res || exit /b 1
windres --input-format=rc -O coff -I res -DSNIPLITE_CLI res\sniplite.rc build\cli.res || exit /b 1

gcc %CF% -Wl,--subsystem,windows -o build\sniplite.exe src\start.S src\main.c src\ov_base.S src\ov_edit.S ^
 src\ov_paint.S src\ov_show.S src\ov_input.S src\present.S src\shot.S src\draw.S src\pin.c src\image.S ^
 src\config.c src\settings.c src\update.c src\uninstall.c src\util.c src\ipc.c src\lang.c src\crt.c ^
 src\ocr.c src\ocr_ui.c src\ocr_table.c src\ocr_get.c src\scroll.c build\sniplite.res ^
 %APP_LDFLAGS% -lkernel32 -luser32 -lgdi32 -ldwmapi -limm32 -luuid -lgcc || exit /b 1
gcc %CF% -DCONSOLE -Wl,--subsystem,console -o build\sniplite.com src\start.S src\cli.c src\update.c src\util.c ^
 src\ipc.c src\lang.c src\crt.c build\cli.res %APP_LDFLAGS% -lkernel32 -luser32 -lgdi32 -lgcc || exit /b 1

rem Build the separate QR helper and ZXing-C++ core as a QR-only static executable.
where cmake >nul 2>nul || (echo cmake not found: use the project-local w64devkit or install CMake & exit /b 1)
where ninja >nul 2>nul || (echo ninja not found: use the project-local w64devkit or install Ninja & exit /b 1)
cmake -S src\qr -B build\qr -G Ninja -DCMAKE_BUILD_TYPE=Release ^
 -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ || exit /b 1
cmake --build build\qr --config Release || exit /b 1
copy /y build\qr\sniplite_qr.exe build\sniplite_qr.exe >nul || exit /b 1

rem The installer carries the screenshot program, CLI, and QR helper as resources, so it is built last.
windres --input-format=rc -O coff -I res -I build res\setup.rc build\setup.res || exit /b 1
gcc %CF% -DSETUP -Wl,--subsystem,windows -o build\sniplite_win_setup_amd64.exe src\start.S src\setup.c ^
 src\util.c src\lang.c src\crt.c build\setup.res %APP_LDFLAGS% -lkernel32 -luser32 -lgdi32 -lole32 -lshell32 -ladvapi32 -luuid -lgcc ^
 || exit /b 1

for %%f in (build\sniplite.exe build\sniplite.com build\sniplite_qr.exe build\sniplite_win_setup_amd64.exe) do echo %%~nxf  %%~zf bytes
