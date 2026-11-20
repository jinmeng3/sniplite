@echo off
rem Build the QR + OCR portable ZIP into build\dist. Run build.bat first and install the OCR module there.
setlocal
cd /d "%~dp0"
if not exist build\sniplite.exe (echo build\sniplite.exe is missing: run build.bat first & exit /b 1)
if not exist build\sniplite_qr.exe (echo build\sniplite_qr.exe is missing: run build.bat first & exit /b 1)
if not exist build\sniplite.com (echo build\sniplite.com is missing: run build.bat first & exit /b 1)
for %%f in (onnxruntime.dll PP-OCRv6_det_tiny.onnx PP-OCRv6_rec_tiny.onnx LICENSE-onnxruntime.txt ThirdPartyNotices-onnxruntime.txt LICENSE-PaddleOCR.txt NOTICE.txt) do if not exist build\ocr\%%f (
  echo build\ocr\%%f is missing: extract the verified OCR module there first
  exit /b 1
)
for /f "tokens=3" %%v in ('findstr /C:"#define SNIPLITE_VERSION" src\common.h') do set V=%%v
set V=%V:L"=%
set V=%V:"=%
set D=build\dist\sniplite_%V%_portable_QR_OCR_x64
set STAGE=build\dist\sniplite_%V%_portable_QR_OCR_x64_stage
set OLD=build\dist\sniplite_%V%_portable_QR_OCR_x64_old
set ZIP=build\dist\sniplite_%V%_portable_QR_OCR_x64.zip
if not exist build\dist mkdir build\dist
if exist "%STAGE%" rmdir /s /q "%STAGE%"
mkdir "%STAGE%\ocr" || exit /b 1
copy /y build\sniplite.exe "%STAGE%\sniplite.exe" >nul || exit /b 1
copy /y build\sniplite.com "%STAGE%\sniplite-cli.com" >nul || exit /b 1
copy /y build\sniplite_qr.exe "%STAGE%\sniplite_qr.exe" >nul || exit /b 1
copy /y third_party\zxing-cpp\LICENSE "%STAGE%\LICENSE-ZXing.txt" >nul || exit /b 1
for %%f in (onnxruntime.dll PP-OCRv6_det_tiny.onnx PP-OCRv6_rec_tiny.onnx LICENSE-onnxruntime.txt ThirdPartyNotices-onnxruntime.txt LICENSE-PaddleOCR.txt NOTICE.txt) do copy /y build\ocr\%%f "%STAGE%\ocr\\" >nul || exit /b 1
if exist "%ZIP%" del /q "%ZIP%"
"%SystemRoot%\System32\tar.exe" -a -c -f "%ZIP%" -C "%STAGE%" sniplite.exe sniplite-cli.com sniplite_qr.exe LICENSE-ZXing.txt ocr || exit /b 1
if exist "%OLD%" rmdir /s /q "%OLD%"
if exist "%D%" move "%D%" "%OLD%" >nul 2>nul
if not exist "%D%" move "%STAGE%" "%D%" >nul
if exist "%STAGE%" rmdir /s /q "%STAGE%"
if exist "%OLD%" rmdir /s /q "%OLD%"
for %%f in ("%ZIP%") do echo %%~nxf  %%~zf bytes
