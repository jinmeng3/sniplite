@echo off
rem Build the QR + OCR AVX2 portable ZIP. Run package-all.bat first.
setlocal
cd /d "%~dp0"
for %%f in (sniplite_ocr.dll) do if not exist "build\stage\ocr\cpu-avx2\%%f" (echo build\stage\ocr\cpu-avx2\%%f is missing: run package-all.bat first ^& exit /b 1)
if not exist build\stage\ocr\shared-runtime\cpu-avx2\ggml-cpu.dll (echo AVX2 runtime is missing: run package-all.bat first ^& exit /b 1)
for %%f in (ggml.dll ggml-base.dll ggml-cpu.dll) do if not exist "build\stage\ocr\shared-runtime\cpu-generic\%%f" (echo generic runtime is missing: run package-all.bat first ^& exit /b 1)
if not exist build\sniplite_translate_avx2.dll (echo AVX2 translation engine is missing: run build.bat first ^& exit /b 1)
for /f "tokens=3" %%v in ('findstr /C:"#define SNIPLITE_VERSION" src\common.h') do set V=%%v
set V=%V:L"=%
set V=%V:"=%
set CPU=build\dist\SnapRail_%V%_portable_QR_OCR_CPU_x64
set D=build\dist\SnapRail_%V%_portable_QR_OCR_AVX2_x64
set STAGE=build\dist\SnapRail_%V%_portable_QR_OCR_AVX2_x64_stage
set "ZIP=build\dist\SnapRail_%V%_portable_QR_OCR_AVX2_x64.zip"
set "ZIP_TMP=%ZIP%.tmp.zip"
if not exist "%CPU%\SnapRail.exe" (echo CPU OCR package is missing: run package-qr-ocr.bat first ^& exit /b 1)
if exist "%STAGE%" rmdir /s /q "%STAGE%"
xcopy /e /i /q /y "%CPU%\*" "%STAGE%\" >nul
if errorlevel 2 exit /b 1
mkdir "%STAGE%\ocr\cpu-avx2" || exit /b 1
for %%f in (sniplite_ocr.dll) do copy /y "build\stage\ocr\cpu-avx2\%%f" "%STAGE%\ocr\cpu-avx2\%%f" >nul || exit /b 1
mkdir "%STAGE%\translation\cpu-avx2" || exit /b 1
copy /y build\sniplite_translate_avx2.dll "%STAGE%\translation\cpu-avx2\sniplite_translate.dll" >nul || exit /b 1
mkdir "%STAGE%\shared-runtime\cpu-avx2" || exit /b 1
for %%f in (ggml.dll ggml-base.dll) do copy /y "build\stage\cpu-runtime-avx2\%%f" "%STAGE%\shared-runtime\cpu-avx2\%%f" >nul || exit /b 1
copy /y build\stage\ocr\shared-runtime\cpu-avx2\ggml-cpu.dll "%STAGE%\shared-runtime\cpu-avx2\ggml-cpu.dll" >nul || exit /b 1
if exist "%ZIP_TMP%" del /q "%ZIP_TMP%"
"%SystemRoot%\System32\tar.exe" -a -c -f "%ZIP_TMP%" -C "%STAGE%" SnapRail.exe SnapRail-cli.com SnapRail_qr.exe LICENSE-ZXing.txt LICENSE-MicroTeX.txt LICENSE-TinyXML2.txt ocr shared-runtime translation || (if exist "%ZIP_TMP%" del /q "%ZIP_TMP%" & exit /b 1)
move /y "%ZIP_TMP%" "%ZIP%" >nul || (if exist "%ZIP_TMP%" del /q "%ZIP_TMP%" & exit /b 1)
if exist "%D%" rmdir /s /q "%D%"
move "%STAGE%" "%D%" >nul || exit /b 1
for %%f in ("%ZIP%") do echo %%~nxf  %%~zf bytes
exit /b 0
