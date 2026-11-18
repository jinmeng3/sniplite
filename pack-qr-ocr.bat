@echo off
rem Build the QR + OCR portable ZIP into build\dist. Run build.bat first and install the OCR module there.
setlocal
cd /d "%~dp0"
if not exist build\sniplite.exe (echo build\sniplite.exe is missing: run build.bat first & exit /b 1)
if not exist build\sniplite_qr.exe (echo build\sniplite_qr.exe is missing: run build.bat first & exit /b 1)
for %%f in (onnxruntime.dll PP-OCRv6_det_tiny.onnx PP-OCRv6_rec_tiny.onnx LICENSE-onnxruntime.txt ThirdPartyNotices-onnxruntime.txt LICENSE-PaddleOCR.txt NOTICE.txt) do if not exist build\ocr\%%f (
  echo build\ocr\%%f is missing: extract the verified OCR module there first
  exit /b 1
)
for /f "tokens=3" %%v in ('findstr /C:"#define SNIPLITE_VERSION" src\common.h') do set V=%%v
set V=%V:L"=%
set V=%V:"=%
set D=build\dist\sniplite_%V%_portable_QR_OCR_x64
set ZIP=build\dist\sniplite_%V%_portable_QR_OCR_x64.zip
if not exist build\dist mkdir build\dist
if exist "%D%" rmdir /s /q "%D%"
mkdir "%D%\ocr" || exit /b 1
copy /y build\sniplite.exe "%D%\sniplite.exe" >nul || exit /b 1
copy /y build\sniplite_qr.exe "%D%\sniplite_qr.exe" >nul || exit /b 1
copy /y third_party\quirc\LICENSE "%D%\LICENSE-quirc.txt" >nul || exit /b 1
for %%f in (onnxruntime.dll PP-OCRv6_det_tiny.onnx PP-OCRv6_rec_tiny.onnx LICENSE-onnxruntime.txt ThirdPartyNotices-onnxruntime.txt LICENSE-PaddleOCR.txt NOTICE.txt) do copy /y build\ocr\%%f "%D%\ocr\" >nul || exit /b 1
if exist "%ZIP%" del /q "%ZIP%"
"%SystemRoot%\System32\tar.exe" -a -c -f "%ZIP%" -C "%D%" sniplite.exe sniplite_qr.exe LICENSE-quirc.txt ocr || exit /b 1
for %%f in ("%ZIP%") do echo %%~nxf  %%~zf bytes
