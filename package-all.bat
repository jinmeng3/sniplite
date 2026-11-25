@echo off
setlocal
chcp 65001 >nul
cd /d "%~dp0"
rem Build the installer and portable ZIPs into build\dist. Run build.bat first.
if not exist build\SnapRail.com (echo build\SnapRail.com is missing: run build.bat first ^& exit /b 1)
if not exist build\SnapRail_setup_amd64.exe (echo the installer is missing: run build.bat first ^& exit /b 1)
if not exist build\sniplite_ocr.dll (echo build\sniplite_ocr.dll is missing: run build.bat first & exit /b 1)
if not exist build\sniplite_ocr_avx2.dll (echo build\sniplite_ocr_avx2.dll is missing: run build.bat first & exit /b 1)
if not exist build\sniplite_formula_avx2.dll (echo build\sniplite_formula_avx2.dll is missing: run build.bat first ^& exit /b 1)
if not exist third_party\ppocrv6\LICENSE (echo third_party\ppocrv6\LICENSE is missing & exit /b 1)
set MODEL_DIR=
for /d %%D in (E:\*) do if not defined MODEL_DIR if exist "%%D\PP-OCRv6_tiny_det-f16.gguf" if exist "%%D\PP-OCRv6_tiny_rec-f16.gguf" set "MODEL_DIR=%%D"
if not defined MODEL_DIR (echo PP-OCRv6 GGUF files are missing from the root of drive E: & exit /b 1)
set "CRISP_DIR=%MODEL_DIR%\crispembed-windows-x86_64-vulkan"
if not exist "%CRISP_DIR%\crispembed.dll" (echo CrispEmbed runtime is missing beside the GGUF files & exit /b 1)
if not exist "%CRISP_DIR%\ggml-vulkan.dll" (echo Vulkan GGML runtime is missing from %CRISP_DIR% & exit /b 1)
call build-cpu-runtime.bat || exit /b 1
if not exist build\stage\cpu-runtime\ggml.dll (echo build\stage\cpu-runtime is missing: build the CPU GGML runtime first ^& exit /b 1)
if not exist build\stage\cpu-runtime\ggml-base.dll exit /b 1
if not exist build\stage\cpu-runtime\ggml-cpu.dll exit /b 1
set "OCR_DIR=build\stage\ocr"
set "OCR_ZIP=build\stage\sniplite_ocr.zip"
if exist "%OCR_DIR%" rmdir /s /q "%OCR_DIR%"
mkdir "%OCR_DIR%" || exit /b 1
copy /y build\sniplite_ocr.dll "%OCR_DIR%\sniplite_ocr.dll" >nul || exit /b 1
copy /y third_party\ppocrv6\LICENSE "%OCR_DIR%\LICENSE-OCREngine.txt" >nul || exit /b 1
for %%f in (PP-OCRv6_tiny_det-f16.gguf PP-OCRv6_tiny_rec-f16.gguf) do copy /y "%MODEL_DIR%\%%f" "%OCR_DIR%\%%f" >nul || exit /b 1
set OCR_GENERIC=%OCR_DIR%\shared-runtime\cpu-generic
mkdir "%OCR_GENERIC%" || exit /b 1
for %%f in (crispembed.dll Apache-2.0.txt LICENSE NOTICE.marker NOTICE.sheenbidi NOTICE.teleocr) do (
  copy /y "%CRISP_DIR%\%%f" "%OCR_GENERIC%\%%f"
  if errorlevel 1 (echo Could not stage %%f from the CrispEmbed package & exit /b 1)
)
for %%f in (ggml.dll ggml-base.dll ggml-cpu.dll) do copy /y "build\stage\cpu-runtime\%%f" "%OCR_GENERIC%\%%f" >nul || exit /b 1
mkdir "%OCR_DIR%\cpu-avx2" || exit /b 1
copy /y build\sniplite_ocr_avx2.dll "%OCR_DIR%\cpu-avx2\sniplite_ocr.dll" >nul || exit /b 1
mkdir "%OCR_DIR%\shared-runtime\cpu-avx2" || exit /b 1
for %%f in (ggml.dll ggml-base.dll) do copy /y "build\stage\cpu-runtime-avx2\%%f" "%OCR_DIR%\shared-runtime\cpu-avx2\%%f" >nul || exit /b 1
copy /y "build\stage\cpu-runtime-avx2\ggml-cpu.dll" "%OCR_DIR%\shared-runtime\cpu-avx2\ggml-cpu.dll" >nul || exit /b 1
for %%f in (vcruntime140.dll vcruntime140_1.dll msvcp140_1.dll msvcp140.dll) do copy /y "%SystemRoot%\System32\%%f" "%OCR_GENERIC%\%%f" >nul || exit /b 1
set OCR_VULKAN_RUNTIME=build\stage\ocr-vulkan-runtime
if exist "%OCR_VULKAN_RUNTIME%" rmdir /s /q "%OCR_VULKAN_RUNTIME%"
mkdir "%OCR_VULKAN_RUNTIME%" || exit /b 1
for %%f in (ggml.dll ggml-base.dll ggml-cpu.dll ggml-vulkan.dll) do copy /y "%CRISP_DIR%\%%f" "%OCR_VULKAN_RUNTIME%\%%f" >nul || exit /b 1
copy /y res\ocr-notice.txt "%OCR_DIR%\NOTICE.txt" >nul || exit /b 1
set "OCR_ZIP_TMP=%OCR_ZIP%.tmp.zip"
if exist "%OCR_ZIP_TMP%" del /q "%OCR_ZIP_TMP%"
"%SystemRoot%\System32\tar.exe" -a -c -f "%OCR_ZIP_TMP%" -C build\stage ocr || (if exist "%OCR_ZIP_TMP%" del /q "%OCR_ZIP_TMP%" & exit /b 1)
move /y "%OCR_ZIP_TMP%" "%OCR_ZIP%" >nul || (if exist "%OCR_ZIP_TMP%" del /q "%OCR_ZIP_TMP%" & exit /b 1)
for %%f in ("%OCR_ZIP%") do echo %%~nxf  %%~zf bytes
for /f "tokens=3" %%v in ('findstr /C:"#define SNIPLITE_VERSION" src\common.h') do set V=%%v
set V=%V:L"=%
set V=%V:"=%
set D=build\dist
if not exist "%D%" mkdir "%D%" || exit /b 1
call package-qr.bat || exit /b 1
call package-qr-ocr.bat || exit /b 1
call package-qr-ocr-avx2.bat || exit /b 1
call package-qr-ocr-vulkan.bat || exit /b 1
call package-qr-ocr-formula.bat || exit /b 1
call package-qr-ocr-formula-avx2.bat || exit /b 1
call package-qr-ocr-formula-vulkan.bat || exit /b 1
rem Keep published files untouched until every package has been created successfully.
rem Remove legacy single-file portable executables after all new packages succeed.
set "SETUP_TMP=%D%\SnapRail_setup_amd64.exe.tmp"
if exist "%SETUP_TMP%" del /q "%SETUP_TMP%"
copy /y build\SnapRail_setup_amd64.exe "%SETUP_TMP%" >nul || (if exist "%SETUP_TMP%" del /q "%SETUP_TMP%" & exit /b 1)
move /y "%SETUP_TMP%" "%D%\SnapRail_setup_amd64.exe" >nul || (if exist "%SETUP_TMP%" del /q "%SETUP_TMP%" & exit /b 1)
copy /y third_party\zxing-cpp\LICENSE "%D%\LICENSE-ZXing.txt" >nul || exit /b 1
copy /y third_party\microtex\LICENSE "%D%\LICENSE-MicroTeX.txt" >nul || exit /b 1
copy /y third_party\tinyxml2\LICENSE.txt "%D%\LICENSE-TinyXML2.txt" >nul || exit /b 1
for %%f in (%D%\*) do echo %%~nxf  %%~zf bytes
for %%f in (LICENSE-ZXing.txt LICENSE-WebView2.txt NOTICE-WebView2.txt) do if exist "%D%\%%f" del /q "%D%\%%f"
for %%f in ("%D%\sniplite_*_portable_*.zip" "%D%\sniplite_win_setup_amd64.exe" "%D%\sniplite_%V%_portable_x64.exe" "%D%\sniplite_%V%_portable_with_OCR_x64.exe") do if exist "%%~f" del /q "%%~f"
for /d %%f in ("%D%\sniplite_*_portable_*") do if exist "%%~f" rmdir /s /q "%%~f"
for %%f in ("%D%\SnapRail_%V%_portable_x64.exe" "%D%\SnapRail_%V%_portable_with_OCR_x64.exe") do if exist "%%~f" del /q "%%~f"
for %%f in (%D%\*) do echo %%~nxf  %%~zf bytes
exit /b 0
