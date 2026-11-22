@echo off
rem Release the installer and portable ZIPs into build\dist. Run build.bat first.
if not exist build\sniplite.com (echo build\sniplite.com is missing: run build.bat first & exit /b 1)
if not exist build\sniplite_win_setup_amd64.exe (echo the installer is missing: run build.bat first & exit /b 1)
for /f "tokens=3" %%v in ('findstr /C:"#define SNIPLITE_VERSION" src\common.h') do set V=%%v
set V=%V:L"=%
set V=%V:"=%
set RUNTIME=ocr/vcruntime140.dll ocr/vcruntime140_1.dll ocr/msvcp140_1.dll ocr/msvcp140.dll
set MODULE=ocr/PP-OCRv6_det_tiny.onnx ocr/PP-OCRv6_rec_tiny.onnx ocr/onnxruntime.dll %RUNTIME%
set LICENSES=ocr/LICENSE-onnxruntime.txt ocr/LICENSE-PaddleOCR.txt ocr/ThirdPartyNotices-onnxruntime.txt
set ZIP=build\sniplite_ocr.zip
set RUNTIME_ZIP=build\sniplite_ocr_runtime.zip
set TAR=%SystemRoot%\System32\tar.exe
set D=build\dist
if not exist "%D%" mkdir "%D%" || exit /b 1
for %%f in (onnxruntime.dll PP-OCRv6_det_tiny.onnx PP-OCRv6_rec_tiny.onnx LICENSE-onnxruntime.txt ThirdPartyNotices-onnxruntime.txt LICENSE-PaddleOCR.txt NOTICE.txt) do if not exist "build\ocr\%%f" set NEED_OCR=1
if defined NEED_OCR (
  if not exist "%ZIP%" "%SystemRoot%\System32\curl.exe" -sfL -o "%ZIP%" "%URL%"
  if not exist "%ZIP%" (echo could not fetch the OCR module: %URL% & exit /b 1)
  for %%f in ("%ZIP%") do if not "%%~zf"=="%BYTES%" (
    echo %ZIP% is %%~zf bytes, src\ocr_get.c names %BYTES%: delete it to fetch the published one again
    exit /b 1
  )
  "%SystemRoot%\System32\tar.exe" -xf "%ZIP%" -C build || exit /b 1
)
for %%f in (LICENSE-ZXing.txt LICENSE-WebView2.txt NOTICE-WebView2.txt) do if exist "%D%\%%f" del /q "%D%\%%f"
for %%f in ("%D%\sniplite_%V%_portable_x64.exe" "%D%\sniplite_%V%_portable_with_OCR_x64.exe") do if exist "%%~f" del /q "%%~f"
copy /y build\sniplite_win_setup_amd64.exe "%D%\sniplite_win_setup_amd64.exe" >nul || exit /b 1
copy /y third_party\zxing-cpp\LICENSE "%D%\LICENSE-ZXing.txt" >nul || exit /b 1
copy /y third_party\webview2\LICENSE.txt "%D%\LICENSE-WebView2.txt" >nul || exit /b 1
copy /y third_party\webview2\NOTICE.txt "%D%\NOTICE-WebView2.txt" >nul || exit /b 1
call pack-qr.bat || exit /b 1
call pack-qr-ocr.bat || exit /b 1
if exist "build\webview2-runtime\msedgewebview2.exe" (
  call pack-qr-ocr-formula-webview2.bat || exit /b 1
) else (
  call pack-qr-ocr-formula.bat || exit /b 1
  if exist "%D%\sniplite_%V%_portable_QR_OCR_CrispEmbed_WebView2_x64.zip" del /q "%D%\sniplite_%V%_portable_QR_OCR_CrispEmbed_WebView2_x64.zip"
  echo Fixed WebView2 package skipped: extract the x64 Fixed Version runtime to build\webview2-runtime
)
for %%f in (%D%\*) do echo %%~nxf  %%~zf bytes
exit /b 0
