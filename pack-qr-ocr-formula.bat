@echo off
rem Build the QR + OCR + FormulaNet portable ZIP into build\dist. Run build.bat first.
setlocal
cd /d "%~dp0"
if not exist build\sniplite.exe (echo build\sniplite.exe is missing: run build.bat first & exit /b 1)
if not exist build\sniplite_qr.exe (echo build\sniplite_qr.exe is missing: run build.bat first & exit /b 1)
if not exist build\sniplite.com (echo build\sniplite.com is missing: run build.bat first & exit /b 1)
for %%f in (onnxruntime.dll PP-OCRv6_det_tiny.onnx PP-OCRv6_rec_tiny.onnx LICENSE-onnxruntime.txt ThirdPartyNotices-onnxruntime.txt LICENSE-PaddleOCR.txt NOTICE.txt) do if not exist "build\ocr\%%f" (
  echo build\ocr\%%f is missing: run pack.bat to prepare the OCR module
  exit /b 1
)
for %%f in (onnxruntime.dll config.json generation_config.json tokenizer.json tokenizer_config.json LICENSE.txt LICENSE-AGPL-3.0.txt LICENSE-onnxruntime.txt ThirdPartyNotices-onnxruntime.txt onnx\encoder_model.onnx onnx\decoder_model_merged.onnx) do if not exist "build\formula\%%f" (
  echo build\formula\%%f is missing: install FormulaNet from Preferences in build\sniplite.exe
  exit /b 1
)
for /f "tokens=3" %%v in ('findstr /C:"#define SNIPLITE_VERSION" src\common.h') do set V=%%v
set V=%V:L"=%
set V=%V:"=%
set D=build\dist\sniplite_%V%_portable_QR_OCR_FormulaNet_x64
set STAGE=build\dist\sniplite_%V%_portable_QR_OCR_FormulaNet_x64_stage
set OLD=build\dist\sniplite_%V%_portable_QR_OCR_FormulaNet_x64_old
set ZIP=build\dist\sniplite_%V%_portable_QR_OCR_FormulaNet_x64.zip
if not exist build\dist mkdir build\dist
if exist "%STAGE%" rmdir /s /q "%STAGE%"
mkdir "%STAGE%\ocr" || exit /b 1
copy /y build\sniplite.exe "%STAGE%\sniplite.exe" >nul || exit /b 1
copy /y build\sniplite.com "%STAGE%\sniplite-cli.com" >nul || exit /b 1
copy /y build\sniplite_qr.exe "%STAGE%\sniplite_qr.exe" >nul || exit /b 1
copy /y third_party\zxing-cpp\LICENSE "%STAGE%\LICENSE-ZXing.txt" >nul || exit /b 1
copy /y third_party\webview2\LICENSE.txt "%STAGE%\LICENSE-WebView2.txt" >nul || exit /b 1
copy /y third_party\webview2\NOTICE.txt "%STAGE%\NOTICE-WebView2.txt" >nul || exit /b 1
for %%f in (onnxruntime.dll PP-OCRv6_det_tiny.onnx PP-OCRv6_rec_tiny.onnx LICENSE-onnxruntime.txt ThirdPartyNotices-onnxruntime.txt LICENSE-PaddleOCR.txt NOTICE.txt) do copy /y build\ocr\%%f "%STAGE%\ocr\\" >nul || exit /b 1
xcopy /e /i /y build\formula "%STAGE%\formula" >nul || exit /b 1
if exist "%ZIP%" del /q "%ZIP%"
"%SystemRoot%\System32\tar.exe" -a -c -f "%ZIP%" -C "%STAGE%" sniplite.exe sniplite-cli.com sniplite_qr.exe LICENSE-ZXing.txt LICENSE-WebView2.txt NOTICE-WebView2.txt ocr formula || exit /b 1
if exist "%OLD%" rmdir /s /q "%OLD%"
if exist "%D%" (
  move "%D%" "%OLD%" >nul 2>nul
  if exist "%D%" echo Existing package directory could not be replaced; leaving it unchanged. The ZIP is current.
)
if not exist "%D%" move "%STAGE%" "%D%" >nul
if exist "%STAGE%" rmdir /s /q "%STAGE%"
if exist "%OLD%" rmdir /s /q "%OLD%"
for %%f in ("%ZIP%") do echo %%~nxf  %%~zf bytes
exit /b 0
