@echo off
rem Build the QR + OCR + formula Vulkan-first portable ZIP. Run build.bat first.
setlocal
cd /d "%~dp0"
if not exist build\sniplite_ocr_vulkan.dll (echo build\sniplite_ocr_vulkan.dll is missing: run build.bat first ^& exit /b 1)
if not exist build\sniplite_formula_vulkan.dll (echo build\sniplite_formula_vulkan.dll is missing: run build.bat first ^& exit /b 1)
for %%f in (ggml.dll ggml-base.dll ggml-cpu.dll ggml-vulkan.dll) do if not exist "build\ocr-vulkan-runtime\%%f" (echo build\ocr-vulkan-runtime\%%f is missing: run package-all.bat first ^& exit /b 1)
if not exist build\sniplite_translate_vulkan.dll (echo Vulkan translation engine is missing: run build.bat first ^& exit /b 1)
for /f "tokens=3" %%v in ('findstr /C:"#define SNIPLITE_VERSION" src\common.h') do set V=%%v
set V=%V:L"=%
set V=%V:"=%
set AVX2=build\dist\SnapRail_%V%_portable_QR_OCR_FormulaNet_AVX2_x64
set D=build\dist\SnapRail_%V%_portable_QR_OCR_FormulaNet_Vulkan_x64
set STAGE=build\dist\SnapRail_%V%_portable_QR_OCR_FormulaNet_Vulkan_x64_stage
set "ZIP_TMP=%ZIP%.tmp.zip"
if not exist "%AVX2%\SnapRail.exe" (echo AVX2 FormulaNet package is missing: package-qr-ocr-formula-avx2.bat failed ^& exit /b 1)
if exist "%STAGE%" rmdir /s /q "%STAGE%"
xcopy /e /i /q /y "%AVX2%\*" "%STAGE%\" >nul
if errorlevel 2 exit /b 1
copy /y build\SnapRail.exe "%STAGE%\SnapRail.exe" >nul || exit /b 1
copy /y build\SnapRail.com "%STAGE%\SnapRail-cli.com" >nul || exit /b 1
copy /y build\SnapRail_qr.exe "%STAGE%\SnapRail_qr.exe" >nul || exit /b 1
mkdir "%STAGE%\shared-runtime\vulkan" || exit /b 1
copy /y build\sniplite_formula.dll "%STAGE%\formula\sniplite_formula.dll" >nul || exit /b 1
copy /y build\sniplite_formula_vulkan.dll "%STAGE%\formula\sniplite_formula_vulkan.dll" >nul || exit /b 1
for %%f in (ggml.dll ggml-base.dll ggml-cpu.dll ggml-vulkan.dll) do copy /y "build\ocr-vulkan-runtime\%%f" "%STAGE%\shared-runtime\vulkan\%%f" >nul || exit /b 1
mkdir "%STAGE%\translation\vulkan" || exit /b 1
copy /y build\sniplite_translate_vulkan.dll "%STAGE%\translation\vulkan\sniplite_translate.dll" >nul || exit /b 1
copy /y build\sniplite_ocr_vulkan.dll "%STAGE%\ocr\sniplite_ocr_vulkan.dll" >nul || exit /b 1
if exist "%ZIP_TMP%" del /q "%ZIP_TMP%"
"%SystemRoot%\System32\tar.exe" -a -c -f "%ZIP_TMP%" -C "%STAGE%" SnapRail.exe SnapRail-cli.com SnapRail_qr.exe LICENSE-ZXing.txt LICENSE-MicroTeX.txt LICENSE-TinyXML2.txt ocr formula shared-runtime translation || (if exist "%ZIP_TMP%" del /q "%ZIP_TMP%" & exit /b 1)
move /y "%ZIP_TMP%" "%ZIP%" >nul || (if exist "%ZIP_TMP%" del /q "%ZIP_TMP%" & exit /b 1)
if exist "%D%" rmdir /s /q "%D%"
move "%STAGE%" "%D%" >nul || exit /b 1
for %%f in ("%ZIP%") do echo %%~nxf  %%~zf bytes
exit /b 0
