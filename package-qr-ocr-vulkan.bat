@echo off
rem Build the QR + OCR Vulkan-first portable ZIP. Run package-all.bat first.
setlocal
cd /d "%~dp0"
if not exist build\sniplite_ocr_vulkan.dll (echo build\sniplite_ocr_vulkan.dll is missing: run build.bat first ^& exit /b 1)
if not exist build\ocr-vulkan-runtime\ggml-vulkan.dll (echo OCR Vulkan GGML runtime is missing: run package-all.bat first ^& exit /b 1)
if not exist build\sniplite_translate_vulkan.dll (echo Vulkan translation engine is missing: run build.bat first ^& exit /b 1)
for /f "tokens=3" %%v in ('findstr /C:"#define SNIPLITE_VERSION" src\common.h') do set V=%%v
set V=%V:L"=%
set V=%V:"=%
set AVX2=build\dist\sniplite_%V%_portable_QR_OCR_AVX2_x64
set D=build\dist\sniplite_%V%_portable_QR_OCR_Vulkan_x64
set STAGE=build\dist\sniplite_%V%_portable_QR_OCR_Vulkan_x64_stage
set ZIP=build\dist\sniplite_%V%_portable_QR_OCR_Vulkan_x64.zip
if not exist "%AVX2%\sniplite.exe" (echo AVX2 OCR package is missing: run package-qr-ocr-avx2.bat first ^& exit /b 1)
if exist "%STAGE%" rmdir /s /q "%STAGE%"
xcopy /e /i /q /y "%AVX2%\*" "%STAGE%\" >nul
if errorlevel 2 exit /b 1
copy /y build\sniplite.exe "%STAGE%\sniplite.exe" >nul || exit /b 1
copy /y build\sniplite.com "%STAGE%\sniplite-cli.com" >nul || exit /b 1
copy /y build\sniplite_qr.exe "%STAGE%\sniplite_qr.exe" >nul || exit /b 1
copy /y build\sniplite_ocr_vulkan.dll "%STAGE%\ocr\sniplite_ocr_vulkan.dll" >nul || exit /b 1
mkdir "%STAGE%\shared-runtime\vulkan" || exit /b 1
for %%f in (ggml.dll ggml-base.dll ggml-cpu.dll ggml-vulkan.dll) do copy /y "build\ocr-vulkan-runtime\%%f" "%STAGE%\shared-runtime\vulkan\%%f" >nul || exit /b 1
mkdir "%STAGE%\translation\vulkan" || exit /b 1
copy /y build\sniplite_translate_vulkan.dll "%STAGE%\translation\vulkan\sniplite_translate.dll" >nul || exit /b 1
for %%f in (ggml.dll ggml-base.dll) do copy /y "build\cpu-runtime-avx2\%%f" "%STAGE%\shared-runtime\cpu-avx2\%%f" >nul || exit /b 1
if exist "%ZIP%" del /q "%ZIP%"
"%SystemRoot%\System32\tar.exe" -a -c -f "%ZIP%" -C "%STAGE%" sniplite.exe sniplite-cli.com sniplite_qr.exe LICENSE-ZXing.txt LICENSE-MicroTeX.txt LICENSE-TinyXML2.txt ocr shared-runtime translation || exit /b 1
if exist "%D%" rmdir /s /q "%D%"
move "%STAGE%" "%D%" >nul || exit /b 1
for %%f in ("%ZIP%") do echo %%~nxf  %%~zf bytes
exit /b 0
