@echo off
rem Build the QR + OCR + FormulaNet AVX2 portable ZIP. Run package-all.bat first.
setlocal
cd /d "%~dp0"
if not exist build\sniplite_formula_avx2.dll (echo FormulaNet AVX2 engine missing: run build.bat first ^& exit /b 1)
if not exist build\ocr\cpu-avx2\sniplite_ocr.dll (echo OCR AVX2 engine missing: run package-all.bat first ^& exit /b 1)
if not exist build\ocr\shared-runtime\cpu-avx2\ggml-cpu.dll (echo AVX2 runtime missing: run package-all.bat first ^& exit /b 1)
if not exist build\sniplite_translate_avx2.dll (echo AVX2 translation engine is missing: run build.bat first ^& exit /b 1)
for %%f in (crispembed.dll vcruntime140.dll vcruntime140_1.dll msvcp140_1.dll msvcp140.dll Apache-2.0.txt LICENSE NOTICE.marker NOTICE.sheenbidi NOTICE.teleocr) do if not exist "build\ocr\shared-runtime\cpu-generic\%%f" (echo Shared runtime missing: run package-all.bat first ^& exit /b 1)
for /f "tokens=3" %%v in ('findstr /C:"#define SNIPLITE_VERSION" src\common.h') do set V=%%v
set V=%V:L"=%
set V=%V:"=%
set CPU=build\dist\SnapRail_%V%_portable_QR_OCR_FormulaNet_CPU_x64
set D=build\dist\SnapRail_%V%_portable_QR_OCR_FormulaNet_AVX2_x64
set STAGE=build\dist\SnapRail_%V%_portable_QR_OCR_FormulaNet_AVX2_x64_stage
set ZIP=build\dist\SnapRail_%V%_portable_QR_OCR_FormulaNet_AVX2_x64.zip
if not exist "%CPU%\SnapRail.exe" (echo FormulaNet CPU package is missing: run package-qr-ocr-formula.bat first ^& exit /b 1)
if exist "%STAGE%" rmdir /s /q "%STAGE%"
xcopy /e /i /q /y "%CPU%\*" "%STAGE%\" >nul
if errorlevel 2 exit /b 1
mkdir "%STAGE%\ocr\cpu-avx2" || exit /b 1
mkdir "%STAGE%\shared-runtime\cpu-avx2" || exit /b 1
mkdir "%STAGE%\formula\cpu-avx2" || exit /b 1
copy /y "build\ocr\cpu-avx2\sniplite_ocr.dll" "%STAGE%\ocr\cpu-avx2\sniplite_ocr.dll" >nul || exit /b 1
for %%f in (ggml.dll ggml-base.dll ggml-cpu.dll) do copy /y "build\cpu-runtime-avx2\%%f" "%STAGE%\shared-runtime\cpu-avx2\%%f" >nul || exit /b 1
copy /y build\sniplite_formula_avx2.dll "%STAGE%\formula\cpu-avx2\sniplite_formula.dll" >nul || exit /b 1
mkdir "%STAGE%\translation\cpu-avx2" || exit /b 1
copy /y build\sniplite_translate_avx2.dll "%STAGE%\translation\cpu-avx2\sniplite_translate.dll" >nul || exit /b 1
if exist "%ZIP%" del /q "%ZIP%"
"%SystemRoot%\System32\tar.exe" -a -c -f "%ZIP%" -C "%STAGE%" SnapRail.exe SnapRail-cli.com SnapRail_qr.exe LICENSE-ZXing.txt LICENSE-MicroTeX.txt LICENSE-TinyXML2.txt ocr formula shared-runtime translation || exit /b 1
if exist "%D%" rmdir /s /q "%D%"
move "%STAGE%" "%D%" >nul || exit /b 1
for %%f in ("%ZIP%") do echo %%~nxf  %%~zf bytes
exit /b 0
