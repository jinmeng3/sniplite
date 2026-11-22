@echo off
rem Build the QR + OCR + FormulaNet + Fixed Version WebView2 portable ZIP.
rem Extract Microsoft's x64 Fixed Version Runtime into build\webview2-runtime first.
setlocal
cd /d "%~dp0"
for %%f in (msedgewebview2.exe msedge.dll icudtl.dat resources.pak msedge_100_percent.pak) do if not exist "build\webview2-runtime\%%f" (
  echo build\webview2-runtime\%%f is missing: extract the complete x64 Fixed Version Runtime
  exit /b 1
)
call pack-qr-ocr-formula.bat || exit /b 1
for /f "tokens=3" %%v in ('findstr /C:"#define SNIPLITE_VERSION" src\common.h') do set V=%%v
set V=%V:L"=%
set V=%V:"=%
set BASE_ZIP=build\dist\sniplite_%V%_portable_QR_OCR_FormulaNet_x64.zip
set STAGE=build\dist\sniplite_%V%_portable_QR_OCR_FormulaNet_WebView2_stage
set ZIP=build\dist\sniplite_%V%_portable_QR_OCR_FormulaNet_WebView2_x64.zip
if not exist "%BASE_ZIP%" (echo the FormulaNet portable ZIP is missing & exit /b 1)
if exist "%STAGE%" rmdir /s /q "%STAGE%"
mkdir "%STAGE%" || exit /b 1
"%SystemRoot%\System32\tar.exe" -xf "%BASE_ZIP%" -C "%STAGE%" || exit /b 1
xcopy /e /i /h /y "build\webview2-runtime" "%STAGE%\webview2-runtime" >nul || exit /b 1
copy /y start-sniplite-webview2.bat "%STAGE%\start-sniplite.bat" >nul || exit /b 1
if exist "%ZIP%" del /q "%ZIP%"
"%SystemRoot%\System32\tar.exe" -a -c -f "%ZIP%" -C "%STAGE%" sniplite.exe sniplite-cli.com sniplite_qr.exe LICENSE-ZXing.txt LICENSE-WebView2.txt NOTICE-WebView2.txt ocr formula webview2-runtime start-sniplite.bat || exit /b 1
rmdir /s /q "%STAGE%"
for %%f in ("%ZIP%") do echo %%~nxf  %%~zf bytes
