@echo off
rem Build the QR + OCR + FormulaNet AVX2 portable ZIP. Run package-all.bat first.
setlocal
cd /d "%~dp0"
if not exist build\sniplite_formula_avx2.dll (echo build\sniplite_formula_avx2.dll is missing: run build.bat first ^& exit /b 1)
for %%f in (sniplite_ocr.dll ggml.dll ggml-base.dll ggml-cpu.dll) do if not exist "build\ocr\cpu-avx2\%%f" (echo build\ocr\cpu-avx2\%%f is missing: run package-all.bat first ^& exit /b 1)
for %%f in (ggml.dll ggml-base.dll ggml-cpu.dll) do if not exist "build\cpu-runtime-avx2\%%f" (echo build\cpu-runtime-avx2\%%f is missing: run package-all.bat first ^& exit /b 1)
for /f "tokens=3" %%v in ('findstr /C:"#define SNIPLITE_VERSION" src\common.h') do set V=%%v
set V=%V:L"=%
set V=%V:"=%
set CPU=build\dist\sniplite_%V%_portable_QR_OCR_FormulaNet_CPU_x64
set D=build\dist\sniplite_%V%_portable_QR_OCR_FormulaNet_AVX2_x64
set STAGE=build\dist\sniplite_%V%_portable_QR_OCR_FormulaNet_AVX2_x64_stage
set ZIP=build\dist\sniplite_%V%_portable_QR_OCR_FormulaNet_AVX2_x64.zip
if not exist "%CPU%\sniplite.exe" (echo FormulaNet CPU package is missing: run package-qr-ocr-formula.bat first ^& exit /b 1)
if exist "%STAGE%" rmdir /s /q "%STAGE%"
xcopy /e /i /q /y "%CPU%\*" "%STAGE%\" >nul
if errorlevel 2 exit /b 1
mkdir "%STAGE%\ocr\cpu-avx2" || exit /b 1
mkdir "%STAGE%\formula\cpu-avx2" || exit /b 1
for %%f in (sniplite_ocr.dll ggml.dll ggml-base.dll ggml-cpu.dll) do copy /y "build\ocr\cpu-avx2\%%f" "%STAGE%\ocr\cpu-avx2\%%f" >nul || exit /b 1
for %%f in (ggml.dll ggml-base.dll ggml-cpu.dll) do copy /y "build\cpu-runtime-avx2\%%f" "%STAGE%\formula\cpu-avx2\%%f" >nul || exit /b 1
for %%f in (crispembed.dll vcruntime140.dll vcruntime140_1.dll msvcp140_1.dll msvcp140.dll) do copy /y "build\ocr\%%f" "%STAGE%\formula\cpu-avx2\%%f" >nul || exit /b 1
copy /y build\sniplite_formula_avx2.dll "%STAGE%\formula\cpu-avx2\sniplite_formula.dll" >nul || exit /b 1
if exist "%ZIP%" del /q "%ZIP%"
"%SystemRoot%\System32\tar.exe" -a -c -f "%ZIP%" -C "%STAGE%" sniplite.exe sniplite-cli.com sniplite_qr.exe LICENSE-ZXing.txt LICENSE-MicroTeX.txt LICENSE-TinyXML2.txt ocr formula || exit /b 1
if exist "%D%" rmdir /s /q "%D%"
move "%STAGE%" "%D%" >nul || exit /b 1
for %%f in ("%ZIP%") do echo %%~nxf  %%~zf bytes
exit /b 0
