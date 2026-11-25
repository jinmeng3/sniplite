@echo off
rem Build SnipLite with x86-64 assembly for performance-sensitive paths and C for the rest.
rem The sources are grouped under src\app, src\overlay, src\ocr, src\formula,
rem src\inference, src\scroll and src\pixels. No C runtime: src\app\crt.c has
rem the few library functions the C uses; language data stays in src\lang_text.h.
rem Uses the project-local w64devkit when present, otherwise MinGW-w64 on PATH.
rem
rem Layout, fixed because the packaging scripts read these exact paths:
rem   build\*.exe *.dll *.com      the products
rem   build\obj\                   every intermediate: .o, import libraries, .res, asset zip
rem   build\stage\translation\     the translation model (staged by hand)
rem   build\stage\ocr\             the OCR module package-all.bat assembles
rem   build\stage\formula\         the formula module
rem   build\microtex\ build\qr\    CMake build trees
rem   build\dist\                  the installer and the portable ZIPs
setlocal
cd /d "%~dp0"
if exist "%~dp0w64devkit\bin\gcc.exe" set "PATH=%~dp0w64devkit\bin;%PATH%"
where gcc >nul 2>nul || (echo gcc not found: put w64devkit in the project root or install MinGW-w64 on PATH & exit /b 1)
if not exist build mkdir build
set "OBJ=build\obj"
if not exist "%OBJ%" mkdir "%OBJ%" || exit /b 1
set "STAGE=build\stage"
set "SRC_INCLUDES=-Isrc -Isrc\app -Isrc\overlay -Isrc\ocr -Isrc\formula -Isrc\inference -Isrc\scroll -Isrc\pixels -Ires"
gcc -Isrc -c -o %OBJ%\inference_kernels.o src\inference\inference_kernels.S || exit /b 1
gcc -Isrc -c -o %OBJ%\inference_math.o src\inference\inference_math.S || exit /b 1
gcc -Isrc -c -o %OBJ%\inference_depthwise.o src\inference\inference_depthwise.S || exit /b 1
gcc -Isrc -c -o %OBJ%\inference_convert.o src\inference\inference_convert.S || exit /b 1
gcc -Isrc -c -o %OBJ%\ocr_crop_kernels.o src\ocr\ocr_crop_kernels.S || exit /b 1
gcc -Isrc -c -o %OBJ%\ocr_detect_kernels.o src\ocr\ocr_detect_kernels.S || exit /b 1
set "INFERENCE_KERNELS=%SRC_INCLUDES% -DSNIPLITE_INFERENCE_ASM %OBJ%\inference_kernels.o %OBJ%\inference_math.o %OBJ%\inference_depthwise.o %OBJ%\inference_convert.o %OBJ%\ocr_crop_kernels.o %OBJ%\ocr_detect_kernels.o"
gcc -Isrc -c -o %OBJ%\pixel_ops.o src\pixels\pixel_ops.S || exit /b 1
set "OCR_PIXEL_KERNELS=-DSNIPLITE_OCR_PREPROCESS %OBJ%\pixel_ops.o src\ocr\ocr_preprocess.cpp"
rem Build generic CPU, AVX2 CPU, and Vulkan-first formula inference engines.
for %%n in (ggml ggml-base ggml-cpu) do dlltool -d third_party\formulanet\ggml\%%n.def -l %OBJ%\%%n-import.a || exit /b 1
set "GGML_LIBS=%OBJ%\ggml-import.a %OBJ%\ggml-base-import.a %OBJ%\ggml-cpu-import.a"
rem Build generic CPU, AVX2 CPU, and Vulkan-preferred Marian translation engines.
g++ -std=c++17 -O3 -DNOMINMAX -DWIN32_LEAN_AND_MEAN ^
 -ffunction-sections -fdata-sections -fno-ident -Ithird_party/shared -Ithird_party/formulanet -Ithird_party/formulanet/ggml ^
 -shared -static-libgcc -static-libstdc++ -s -Wl,--gc-sections,--exclude-all-symbols,--nxcompat,--dynamicbase,--high-entropy-va,--no-insert-timestamp ^
 -o build\sniplite_translate.dll third_party\translation\opusmt_translate.cpp third_party\shared\core\gguf_loader.cpp ^
 third_party\translation\opusmt_translate.def %GGML_LIBS% || exit /b 1
g++ -std=c++17 -O3 -mavx2 -mfma -mf16c -DNOMINMAX -DWIN32_LEAN_AND_MEAN ^
 -ffunction-sections -fdata-sections -fno-ident -Ithird_party/shared -Ithird_party/formulanet -Ithird_party/formulanet/ggml ^
 -shared -static-libgcc -static-libstdc++ -s -Wl,--gc-sections,--exclude-all-symbols,--nxcompat,--dynamicbase,--high-entropy-va,--no-insert-timestamp ^
 -o build\sniplite_translate_avx2.dll third_party\translation\opusmt_translate.cpp third_party\shared\core\gguf_loader.cpp ^
 third_party\translation\opusmt_translate.def %GGML_LIBS% || exit /b 1
g++ -std=c++17 -O3 -DNOMINMAX -DWIN32_LEAN_AND_MEAN -DSNIPLITE_TRANSLATION_VULKAN ^
 -ffunction-sections -fdata-sections -fno-ident -Ithird_party/shared -Ithird_party/formulanet -Ithird_party/formulanet/ggml ^
 -shared -static-libgcc -static-libstdc++ -s -Wl,--gc-sections,--exclude-all-symbols,--nxcompat,--dynamicbase,--high-entropy-va,--no-insert-timestamp ^
 -o build\sniplite_translate_vulkan.dll third_party\translation\opusmt_translate.cpp third_party\shared\core\gguf_loader.cpp ^
 third_party\translation\opusmt_translate.def %GGML_LIBS% || exit /b 1
copy /y third_party\translation\LICENSE build\LICENSE-Translation.txt >nul || exit /b 1
g++ -std=c++17 -O3 -DNOMINMAX -DWIN32_LEAN_AND_MEAN %INFERENCE_KERNELS% ^
 -ffunction-sections -fdata-sections -fno-ident -Ithird_party/shared -Ithird_party/formulanet -Ithird_party/formulanet/ggml ^
 -shared -static-libgcc -static-libstdc++ -s -Wl,--gc-sections,--exclude-all-symbols,--nxcompat,--dynamicbase,--high-entropy-va,--no-insert-timestamp ^
 -o build\sniplite_formula.dll third_party\formulanet\ppformulanet_ocr.cpp third_party\shared\core\gguf_loader.cpp ^
 third_party\formulanet\formula.def %GGML_LIBS% || exit /b 1
g++ -std=c++17 -O3 -mavx2 -mfma -mf16c -DNOMINMAX -DWIN32_LEAN_AND_MEAN %INFERENCE_KERNELS% ^
 -ffunction-sections -fdata-sections -fno-ident -Ithird_party/shared -Ithird_party/formulanet -Ithird_party/formulanet/ggml ^
 -shared -static-libgcc -static-libstdc++ -s -Wl,--gc-sections,--exclude-all-symbols,--nxcompat,--dynamicbase,--high-entropy-va,--no-insert-timestamp ^
 -o build\sniplite_formula_avx2.dll third_party\formulanet\ppformulanet_ocr.cpp third_party\shared\core\gguf_loader.cpp ^
 third_party\formulanet\formula.def %GGML_LIBS% || exit /b 1
g++ -std=c++17 -O3 -DNOMINMAX -DWIN32_LEAN_AND_MEAN -DSNIPLITE_VULKAN_PREFERRED %INFERENCE_KERNELS% ^
 -ffunction-sections -fdata-sections -fno-ident -Ithird_party/shared -Ithird_party/formulanet -Ithird_party/formulanet/ggml ^
 -shared -static-libgcc -static-libstdc++ -s -Wl,--gc-sections,--exclude-all-symbols,--nxcompat,--dynamicbase,--high-entropy-va,--no-insert-timestamp ^
 -o build\sniplite_formula_vulkan.dll third_party\formulanet\ppformulanet_ocr.cpp third_party\shared\core\gguf_loader.cpp ^
 third_party\formulanet\formula.def %GGML_LIBS% || exit /b 1
rem Build generic CPU, AVX2 CPU, and Vulkan-first OCR engines with corrected recognition strides.
g++ -std=c++17 -O3 -DNOMINMAX -DWIN32_LEAN_AND_MEAN -DSNIPLITE_OCR_BUILD -DSNIPLITE_CPU_ONLY %INFERENCE_KERNELS% %OCR_PIXEL_KERNELS% ^
 -ffunction-sections -fdata-sections -fno-ident -Ithird_party/ppocrv6 ^
 -Ithird_party/shared -Ithird_party/formulanet -Ithird_party/formulanet/core -Ithird_party/formulanet/ggml ^
 -shared -static-libgcc -static-libstdc++ -s -Wl,--gc-sections,--exclude-all-symbols,--nxcompat,--dynamicbase,--high-entropy-va,--no-insert-timestamp ^
 -o build\sniplite_ocr.dll src\ocr\ocr_engine.cpp third_party\ppocrv6\ppocrv6_ocr.cpp ^
 third_party\ppocrv6\ppocrv6_det.cpp third_party\ppocrv6\ocr_detect.cpp third_party\ppocrv6\ocr_crop.cpp ^
 third_party\ppocrv6\text_angle.cpp third_party\shared\core\gguf_loader.cpp src\ocr\ocr_engine.def ^
 %GGML_LIBS% || exit /b 1
g++ -std=c++17 -O3 -mavx2 -mfma -mf16c -DNOMINMAX -DWIN32_LEAN_AND_MEAN -DSNIPLITE_OCR_BUILD -DSNIPLITE_CPU_ONLY %INFERENCE_KERNELS% %OCR_PIXEL_KERNELS% ^
 -ffunction-sections -fdata-sections -fno-ident -Ithird_party/ppocrv6 ^
 -Ithird_party/shared -Ithird_party/formulanet -Ithird_party/formulanet/core -Ithird_party/formulanet/ggml ^
 -shared -static-libgcc -static-libstdc++ -s -Wl,--gc-sections,--exclude-all-symbols,--nxcompat,--dynamicbase,--high-entropy-va,--no-insert-timestamp ^
 -o build\sniplite_ocr_avx2.dll src\ocr\ocr_engine.cpp third_party\ppocrv6\ppocrv6_ocr.cpp ^
 third_party\ppocrv6\ppocrv6_det.cpp third_party\ppocrv6\ocr_detect.cpp third_party\ppocrv6\ocr_crop.cpp ^
 third_party\ppocrv6\text_angle.cpp third_party\shared\core\gguf_loader.cpp src\ocr\ocr_engine.def ^
 %GGML_LIBS% || exit /b 1
g++ -std=c++17 -O3 -DNOMINMAX -DWIN32_LEAN_AND_MEAN -DSNIPLITE_OCR_BUILD -DSNIPLITE_VULKAN_PREFERRED %INFERENCE_KERNELS% %OCR_PIXEL_KERNELS% ^
 -ffunction-sections -fdata-sections -fno-ident -Ithird_party/ppocrv6 ^
 -Ithird_party/shared -Ithird_party/formulanet -Ithird_party/formulanet/core -Ithird_party/formulanet/ggml ^
 -shared -static-libgcc -static-libstdc++ -s -Wl,--gc-sections,--exclude-all-symbols,--nxcompat,--dynamicbase,--high-entropy-va,--no-insert-timestamp ^
 -o build\sniplite_ocr_vulkan.dll src\ocr\ocr_engine.cpp third_party\ppocrv6\ppocrv6_ocr.cpp ^
 third_party\ppocrv6\ppocrv6_det.cpp third_party\ppocrv6\ocr_detect.cpp third_party\ppocrv6\ocr_crop.cpp ^
 third_party\ppocrv6\text_angle.cpp third_party\shared\core\gguf_loader.cpp src\ocr\ocr_engine.def ^
 %GGML_LIBS% || exit /b 1
rem Build the native formula renderer and embed its resources in the main executable.
where cmake >nul 2>nul || (echo cmake not found: use the project-local w64devkit or install CMake & exit /b 1)
where ninja >nul 2>nul || (echo ninja not found: use the project-local w64devkit or install Ninja & exit /b 1)
cmake -S src\microtex -B build\microtex -G Ninja -DCMAKE_BUILD_TYPE=Release ^
 -DCMAKE_CXX_COMPILER=g++ || exit /b 1
cmake --build build\microtex --config Release || exit /b 1
copy /y build\microtex\sniplite_microtex.dll build\sniplite_microtex.dll >nul || exit /b 1
copy /y third_party\microtex\LICENSE build\LICENSE-MicroTeX.txt >nul || exit /b 1
copy /y third_party\tinyxml2\LICENSE.txt build\LICENSE-TinyXML2.txt >nul || exit /b 1
if exist %OBJ%\microtex-assets.zip del /q %OBJ%\microtex-assets.zip
"%SystemRoot%\System32\tar.exe" -a -c -f %OBJ%\microtex-assets.zip -C third_party\microtex res LICENSE || exit /b 1

rem src\start.S is the entry point (start); every program links only what it uses.
set CF=%SRC_INCLUDES% -std=c11 -Os -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers ^
 -D_WIN32_WINNT=0x0A00 -DWINVER=0x0A00 -fno-math-errno -ffunction-sections -fdata-sections -fno-ident
set APP_LDFLAGS=-nostdlib -nostartfiles -e start -s -Wl,--gc-sections -Wl,--nxcompat -Wl,--dynamicbase ^
 -Wl,--high-entropy-va

windres --input-format=rc -O coff -I res -o %OBJ%\sniplite.res res/sniplite.rc || exit /b 1
windres --input-format=rc -O coff -I res -DSNIPLITE_CLI -o %OBJ%\cli.res res/sniplite.rc || exit /b 1

gcc %CF% -Wl,--subsystem,windows -o build\SnapRail.exe src\start.S src\app\main.c src\overlay\ov_base.S src\overlay\ov_edit.S ^
 src\overlay\ov_paint.S src\overlay\ov_show.S src\overlay\ov_input.S src\overlay\present.S src\overlay\shot.S src\overlay\draw.S src\app\pin.c src\overlay\image.S ^
 src\scroll\scroll_thumb.S src\scroll\scroll_hash.S ^
 src\app\config.c src\app\settings.c src\app\update.c src\app\uninstall.c src\app\util.c src\app\ipc.c src\app\lang.c src\app\crt.c ^
 src\ocr\ocr.c src\ocr\ocr_ui.c src\ocr\ocr_table.c src\ocr\ocr_get.c src\translate\translate.c src\formula\formula.c src\formula\formula_get.c src\formula\formula_ui.c src\formula\formula_preview.c src\scroll\scroll.c src\overlay\snap.c %OBJ%\sniplite.res ^
 %APP_LDFLAGS% -lkernel32 -luser32 -lgdi32 -ldwmapi -limm32 -luuid -lole32 -lshell32 -lshlwapi -lgcc || exit /b 1
gcc %CF% -DCONSOLE -Wl,--subsystem,console -o build\SnapRail.com src\start.S src\app\cli.c src\app\update.c src\app\util.c ^
 src\app\ipc.c src\app\lang.c src\app\crt.c %OBJ%\cli.res %APP_LDFLAGS% -lkernel32 -luser32 -lgdi32 -lgcc || exit /b 1

rem Build the separate QR helper and ZXing-C++ core as a QR-only static executable.
cmake -S src\qr -B build\qr -G Ninja -DCMAKE_BUILD_TYPE=Release ^
 -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ || exit /b 1
cmake --build build\qr --config Release || exit /b 1
copy /y build\qr\SnapRail_qr.exe build\SnapRail_qr.exe >nul || exit /b 1
copy /y third_party\zxing-cpp\LICENSE build\LICENSE-ZXing.txt >nul || exit /b 1

rem A setup.rc left in build\obj by an older build would shadow res\setup.rc for windres.
if exist %OBJ%\setup.rc del /q %OBJ%\setup.rc
rem Staged inputs come first: the installer resource and every package read build\stage only.
rem build\stage\translation is filled by hand; build\ocr and build\formula are moved here.
if not exist "%STAGE%\translation" mkdir "%STAGE%\translation" || exit /b 1
if not exist "%STAGE%\translation\opus-mt-en-zh-q8_0.gguf" if exist assets\opus-mt-en-zh-q8_0.gguf copy /y assets\opus-mt-en-zh-q8_0.gguf "%STAGE%\translation\" >nul || exit /b 1
if exist "build\translation\opus-mt-en-zh-q8_0.gguf" move /y "build\translation\opus-mt-en-zh-q8_0.gguf "%STAGE%\translation\" >nul || exit /b 1
if exist "build\translation" rmdir /s /q "build\translation"
if exist "build\ocr\PP-OCRv6_tiny_rec-f16.gguf" (
 if not exist "%STAGE%\ocr" mkdir "%STAGE%\ocr" || exit /b 1
 move /y "build\ocr\*" "%STAGE%\ocr\" >nul || exit /b 1
 if exist "build\ocr" rmdir /s /q "build\ocr"
)
if not exist "%STAGE%\formula" mkdir "%STAGE%\formula" || exit /b 1
if not exist "%STAGE%\formula\texo-distill-q8_0.gguf" if exist assets\texo-distill-q8_0.gguf copy /y assets\texo-distill-q8_0.gguf "%STAGE%\formula\" >nul || exit /b 1
if exist "build\formula\texo-distill-q8_0.gguf" (
 if not exist "%STAGE%\formula" mkdir "%STAGE%\formula" || exit /b 1
 move /y "build\formula\*" "%STAGE%\formula\" >nul || exit /b 1
 if exist "build\formula" rmdir /s /q "build\formula"
)

rem The installer carries the screenshot program, CLI, QR helper and translation model as
rem resources. The model is optional: without it setup builds without RCDATA 218 and the
rem installer simply does not lay down the translation model.
rem windres ignores -I for RCDATA files, so the model is copied into build\ (or commented
rem out when absent) and removed again by the cleanup step below.
set "SETUP_RC=res\setup.rc"
if not exist "%STAGE%\translation\opus-mt-en-zh-q8_0.gguf" (
 type res\setup.rc > %OBJ%\setup_nomodel.rc || exit /b 1
 echo. >> %OBJ%\setup_nomodel.rc
 echo /* IDR_TRANSLATE_MODEL RCDATA "opus-mt-en-zh-q8_0.gguf" */ >> %OBJ%\setup_nomodel.rc
 set "SETUP_RC=build\obj\setup_nomodel.rc"
) else (
 copy /y "%STAGE%\translation\opus-mt-en-zh-q8_0.gguf" build\opus-mt-en-zh-q8_0.gguf >nul || exit /b 1
)
windres --input-format=rc -O coff -I res -I build -o %OBJ%\setup.res "%SETUP_RC%" || exit /b 1
gcc %CF% -DSETUP -Wl,--subsystem,windows -o build\SnapRail_setup_amd64.exe src\start.S src\app\setup.c ^
 src\app\util.c src\app\lang.c src\app\crt.c %OBJ%\setup.res %APP_LDFLAGS% -lkernel32 -luser32 -lgdi32 -lole32 -lshell32 -ladvapi32 -luuid -lgcc ^
 || exit /b 1

rem Keep build\ to products: intermediates are moved out of the way once every product
rem above is linked. Packaging reads build\stage and build\obj.
for %%f in (build\*.o build\*-import.a build\*.res build\microtex-assets.zip) do if exist "%%~f" move /y "%%~f" "%OBJ%\" >nul || exit /b 1
rem The model copy in build\ existed only for windres; the staged copy under build\stage stays.
if exist "build\opus-mt-en-zh-q8_0.gguf" del /q "build\opus-mt-en-zh-q8_0.gguf" || exit /b 1


rem Remove obsolete product executable names after the new outputs build successfully.
for %%f in (build\sniplite.exe build\sniplite.com build\sniplite_qr.exe build\sniplite_win_setup_amd64.exe build\qr\sniplite_qr.exe) do if exist "%%~f" del /q "%%~f"
for %%f in (build\SnapRail.exe build\SnapRail.com build\SnapRail_qr.exe build\SnapRail_setup_amd64.exe) do echo %%~nxf  %%~zf bytes
