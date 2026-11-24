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
gcc -c -o build\inference_kernels.o src\inference_kernels.S || exit /b 1
gcc -c -o build\inference_math.o src\inference_math.S || exit /b 1
gcc -c -o build\inference_depthwise.o src\inference_depthwise.S || exit /b 1
gcc -c -o build\inference_convert.o src\inference_convert.S || exit /b 1
gcc -c -o build\ocr_crop_kernels.o src\ocr_crop_kernels.S || exit /b 1
gcc -c -o build\ocr_detect_kernels.o src\ocr_detect_kernels.S || exit /b 1
set "INFERENCE_KERNELS=-Isrc -DSNIPLITE_INFERENCE_ASM build\inference_kernels.o build\inference_math.o build\inference_depthwise.o build\inference_convert.o build\ocr_crop_kernels.o build\ocr_detect_kernels.o"
gcc -c -o build\pixel_ops.o src\pixel_ops.S || exit /b 1
set "OCR_PIXEL_KERNELS=-DSNIPLITE_OCR_PREPROCESS build\pixel_ops.o src\ocr_preprocess.cpp"
rem Build generic CPU, AVX2 CPU, and Vulkan-first formula inference engines.
for %%n in (ggml ggml-base ggml-cpu) do dlltool -d third_party\formulanet\ggml\%%n.def -l build\%%n-import.a || exit /b 1
g++ -std=c++17 -O3 -DNOMINMAX -DWIN32_LEAN_AND_MEAN %INFERENCE_KERNELS% ^
 -ffunction-sections -fdata-sections -fno-ident -Ithird_party/formulanet -Ithird_party/formulanet/ggml ^
 -shared -static-libgcc -static-libstdc++ -s -Wl,--gc-sections,--exclude-all-symbols,--nxcompat,--dynamicbase,--high-entropy-va,--no-insert-timestamp ^
 -o build\sniplite_formula.dll third_party\formulanet\ppformulanet_ocr.cpp third_party\formulanet\core\gguf_loader.cpp ^
 third_party\formulanet\formula.def build\ggml-import.a build\ggml-base-import.a build\ggml-cpu-import.a || exit /b 1
g++ -std=c++17 -O3 -mavx2 -mfma -mf16c -DNOMINMAX -DWIN32_LEAN_AND_MEAN %INFERENCE_KERNELS% ^
 -ffunction-sections -fdata-sections -fno-ident -Ithird_party/formulanet -Ithird_party/formulanet/ggml ^
 -shared -static-libgcc -static-libstdc++ -s -Wl,--gc-sections,--exclude-all-symbols,--nxcompat,--dynamicbase,--high-entropy-va,--no-insert-timestamp ^
 -o build\sniplite_formula_avx2.dll third_party\formulanet\ppformulanet_ocr.cpp third_party\formulanet\core\gguf_loader.cpp ^
 third_party\formulanet\formula.def build\ggml-import.a build\ggml-base-import.a build\ggml-cpu-import.a || exit /b 1
g++ -std=c++17 -O3 -DNOMINMAX -DWIN32_LEAN_AND_MEAN -DSNIPLITE_VULKAN_PREFERRED %INFERENCE_KERNELS% ^
 -ffunction-sections -fdata-sections -fno-ident -Ithird_party/formulanet -Ithird_party/formulanet/ggml ^
 -shared -static-libgcc -static-libstdc++ -s -Wl,--gc-sections,--exclude-all-symbols,--nxcompat,--dynamicbase,--high-entropy-va,--no-insert-timestamp ^
 -o build\sniplite_formula_vulkan.dll third_party\formulanet\ppformulanet_ocr.cpp third_party\formulanet\core\gguf_loader.cpp ^
 third_party\formulanet\formula.def build\ggml-import.a build\ggml-base-import.a build\ggml-cpu-import.a || exit /b 1
if exist build\formula\texo-distill-q8_0.gguf (
 copy /y build\sniplite_formula.dll build\formula\sniplite_formula.dll >nul || exit /b 1
 copy /y third_party\formulanet\LICENSE build\formula\LICENSE-FormulaEngine.txt >nul || exit /b 1
)
rem Build generic CPU, AVX2 CPU, and Vulkan-first OCR engines with corrected recognition strides.
g++ -std=c++17 -O3 -DNOMINMAX -DWIN32_LEAN_AND_MEAN -DSNIPLITE_OCR_BUILD -DSNIPLITE_CPU_ONLY %INFERENCE_KERNELS% %OCR_PIXEL_KERNELS% ^
 -ffunction-sections -fdata-sections -fno-ident -Ithird_party/ppocrv6 ^
 -Ithird_party/formulanet -Ithird_party/formulanet/core -Ithird_party/formulanet/ggml ^
 -shared -static-libgcc -static-libstdc++ -s -Wl,--gc-sections,--exclude-all-symbols,--nxcompat,--dynamicbase,--high-entropy-va,--no-insert-timestamp ^
 -o build\sniplite_ocr.dll src\ocr_engine.cpp third_party\ppocrv6\ppocrv6_ocr.cpp ^
 third_party\ppocrv6\ppocrv6_det.cpp third_party\ppocrv6\ocr_detect.cpp third_party\ppocrv6\ocr_crop.cpp ^
 third_party\ppocrv6\text_angle.cpp third_party\formulanet\core\gguf_loader.cpp src\ocr_engine.def ^
 build\ggml-import.a build\ggml-base-import.a build\ggml-cpu-import.a || exit /b 1
g++ -std=c++17 -O3 -mavx2 -mfma -mf16c -DNOMINMAX -DWIN32_LEAN_AND_MEAN -DSNIPLITE_OCR_BUILD -DSNIPLITE_CPU_ONLY %INFERENCE_KERNELS% %OCR_PIXEL_KERNELS% ^
 -ffunction-sections -fdata-sections -fno-ident -Ithird_party/ppocrv6 ^
 -Ithird_party/formulanet -Ithird_party/formulanet/core -Ithird_party/formulanet/ggml ^
 -shared -static-libgcc -static-libstdc++ -s -Wl,--gc-sections,--exclude-all-symbols,--nxcompat,--dynamicbase,--high-entropy-va,--no-insert-timestamp ^
 -o build\sniplite_ocr_avx2.dll src\ocr_engine.cpp third_party\ppocrv6\ppocrv6_ocr.cpp ^
 third_party\ppocrv6\ppocrv6_det.cpp third_party\ppocrv6\ocr_detect.cpp third_party\ppocrv6\ocr_crop.cpp ^
 third_party\ppocrv6\text_angle.cpp third_party\formulanet\core\gguf_loader.cpp src\ocr_engine.def ^
 build\ggml-import.a build\ggml-base-import.a build\ggml-cpu-import.a || exit /b 1
g++ -std=c++17 -O3 -DNOMINMAX -DWIN32_LEAN_AND_MEAN -DSNIPLITE_OCR_BUILD -DSNIPLITE_VULKAN_PREFERRED %INFERENCE_KERNELS% %OCR_PIXEL_KERNELS% ^
 -ffunction-sections -fdata-sections -fno-ident -Ithird_party/ppocrv6 ^
 -Ithird_party/formulanet -Ithird_party/formulanet/core -Ithird_party/formulanet/ggml ^
 -shared -static-libgcc -static-libstdc++ -s -Wl,--gc-sections,--exclude-all-symbols,--nxcompat,--dynamicbase,--high-entropy-va,--no-insert-timestamp ^
 -o build\sniplite_ocr_vulkan.dll src\ocr_engine.cpp third_party\ppocrv6\ppocrv6_ocr.cpp ^
 third_party\ppocrv6\ppocrv6_det.cpp third_party\ppocrv6\ocr_detect.cpp third_party\ppocrv6\ocr_crop.cpp ^
 third_party\ppocrv6\text_angle.cpp third_party\formulanet\core\gguf_loader.cpp src\ocr_engine.def ^
 build\ggml-import.a build\ggml-base-import.a build\ggml-cpu-import.a || exit /b 1
if exist build\ocr\PP-OCRv6_tiny_rec-f16.gguf (
 copy /y build\sniplite_ocr.dll build\ocr\sniplite_ocr.dll >nul || exit /b 1
 copy /y third_party\ppocrv6\LICENSE build\ocr\LICENSE-OCREngine.txt >nul || exit /b 1
)
rem Build the native formula renderer and embed its resources in the main executable.
where cmake >nul 2>nul || (echo cmake not found: use the project-local w64devkit or install CMake & exit /b 1)
where ninja >nul 2>nul || (echo ninja not found: use the project-local w64devkit or install Ninja & exit /b 1)
cmake -S src\microtex -B build\microtex -G Ninja -DCMAKE_BUILD_TYPE=Release ^
 -DCMAKE_CXX_COMPILER=g++ || exit /b 1
cmake --build build\microtex --config Release || exit /b 1
copy /y build\microtex\sniplite_microtex.dll build\sniplite_microtex.dll >nul || exit /b 1
copy /y third_party\microtex\LICENSE build\LICENSE-MicroTeX.txt >nul || exit /b 1
copy /y third_party\tinyxml2\LICENSE.txt build\LICENSE-TinyXML2.txt >nul || exit /b 1
if exist build\microtex-assets.zip del /q build\microtex-assets.zip
"%SystemRoot%\System32\tar.exe" -a -c -f build\microtex-assets.zip -C third_party\microtex res LICENSE || exit /b 1

rem src\start.S is the entry point (start); every program links only what it uses.
set CF=-Isrc -Ires -std=c11 -Os -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers ^
 -D_WIN32_WINNT=0x0A00 -DWINVER=0x0A00 -fno-math-errno -ffunction-sections -fdata-sections -fno-ident
set APP_LDFLAGS=-nostdlib -nostartfiles -e start -s -Wl,--gc-sections -Wl,--nxcompat -Wl,--dynamicbase ^
 -Wl,--high-entropy-va

windres --input-format=rc -O coff -I res res/sniplite.rc build/sniplite.res || exit /b 1
windres --input-format=rc -O coff -I res -DSNIPLITE_CLI res/sniplite.rc build/cli.res || exit /b 1

gcc %CF% -Wl,--subsystem,windows -o build\sniplite.exe src\start.S src\main.c src\ov_base.S src\ov_edit.S ^
 src\ov_paint.S src\ov_show.S src\ov_input.S src\present.S src\shot.S src\draw.S src\pin.c src\image.S ^
 src\scroll_thumb.S src\scroll_hash.S ^
 src\config.c src\settings.c src\update.c src\uninstall.c src\util.c src\ipc.c src\lang.c src\crt.c ^
 src\ocr.c src\ocr_ui.c src\ocr_table.c src\ocr_get.c src\formula.c src\formula_get.c src\formula_ui.c src\formula_preview.c src\scroll.c src\snap.c build\sniplite.res ^
 %APP_LDFLAGS% -lkernel32 -luser32 -lgdi32 -ldwmapi -limm32 -luuid -lole32 -lshell32 -lshlwapi -lgcc || exit /b 1
gcc %CF% -DCONSOLE -Wl,--subsystem,console -o build\sniplite.com src\start.S src\cli.c src\update.c src\util.c ^
 src\ipc.c src\lang.c src\crt.c build\cli.res %APP_LDFLAGS% -lkernel32 -luser32 -lgdi32 -lgcc || exit /b 1

rem Build the separate QR helper and ZXing-C++ core as a QR-only static executable.
cmake -S src\qr -B build\qr -G Ninja -DCMAKE_BUILD_TYPE=Release ^
 -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ || exit /b 1
cmake --build build\qr --config Release || exit /b 1
copy /y build\qr\sniplite_qr.exe build\sniplite_qr.exe >nul || exit /b 1
copy /y third_party\zxing-cpp\LICENSE build\LICENSE-ZXing.txt >nul || exit /b 1

rem The installer carries the screenshot program, CLI, and QR helper as resources, so it is built last.
windres --input-format=rc -O coff -I res -I build res\setup.rc build\setup.res || exit /b 1
gcc %CF% -DSETUP -Wl,--subsystem,windows -o build\sniplite_win_setup_amd64.exe src\start.S src\setup.c ^
 src\util.c src\lang.c src\crt.c build\setup.res %APP_LDFLAGS% -lkernel32 -luser32 -lgdi32 -lole32 -lshell32 -ladvapi32 -luuid -lgcc ^
 || exit /b 1

for %%f in (build\sniplite.exe build\sniplite.com build\sniplite_qr.exe build\sniplite_win_setup_amd64.exe) do echo %%~nxf  %%~zf bytes
