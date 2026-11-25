@echo off
rem Build the QR + OCR portable ZIP into build\dist. Run build.bat and package-all.bat first.
setlocal
cd /d "%~dp0"
if not exist build\sniplite.exe (echo build\sniplite.exe is missing: run build.bat first & exit /b 1)
if not exist build\sniplite_qr.exe (echo build\sniplite_qr.exe is missing: run build.bat first & exit /b 1)
if not exist build\sniplite.com (echo build\sniplite.com is missing: run build.bat first & exit /b 1)
if not exist build\sniplite_ocr.dll (echo build\sniplite_ocr.dll is missing: run build.bat first & exit /b 1)
if not exist build\sniplite_translate.dll (echo Translation engine is missing: run build.bat first ^& exit /b 1)
if not exist build\LICENSE-Translation.txt (echo Translation license is missing: run build.bat first ^& exit /b 1)
if not exist third_party\ppocrv6\LICENSE (echo third_party\ppocrv6\LICENSE is missing & exit /b 1)
for %%f in (PP-OCRv6_tiny_det-f16.gguf PP-OCRv6_tiny_rec-f16.gguf NOTICE.txt) do if not exist build\ocr\%%f (
  echo build\ocr\%%f is missing: run package-all.bat to prepare the GGUF OCR module
  exit /b 1
)
for %%f in (sniplite_ocr.dll) do if not exist build\ocr\%%f (echo build\ocr\%%f is missing: run package-all.bat first ^& exit /b 1)
for %%f in (crispembed.dll ggml.dll ggml-base.dll ggml-cpu.dll vcruntime140.dll vcruntime140_1.dll msvcp140_1.dll msvcp140.dll Apache-2.0.txt LICENSE NOTICE.marker NOTICE.sheenbidi NOTICE.teleocr) do if not exist build\ocr\shared-runtime\cpu-generic\%%f (
  echo build\ocr\shared-runtime\cpu-generic\%%f is missing: run package-all.bat first
  exit /b 1
)
if not exist build\ocr\LICENSE-OCREngine.txt (echo OCR engine license is missing: run package-all.bat first ^& exit /b 1)
for /f "tokens=3" %%v in ('findstr /C:"#define SNIPLITE_VERSION" src\common.h') do set V=%%v
set V=%V:L"=%
set V=%V:"=%
set D=build\dist\sniplite_%V%_portable_QR_OCR_CPU_x64
set STAGE=build\dist\sniplite_%V%_portable_QR_OCR_CPU_x64_stage
set OLD=build\dist\sniplite_%V%_portable_QR_OCR_CPU_x64_old
set ZIP=build\dist\sniplite_%V%_portable_QR_OCR_CPU_x64.zip
if not exist build\dist mkdir build\dist
if exist "%STAGE%" rmdir /s /q "%STAGE%"
mkdir "%STAGE%\ocr" || exit /b 1
mkdir "%STAGE%\shared-runtime\cpu-generic" || exit /b 1
mkdir "%STAGE%\translation" || exit /b 1
copy /y build\sniplite.exe "%STAGE%\sniplite.exe" >nul || exit /b 1
copy /y build\sniplite.com "%STAGE%\sniplite-cli.com" >nul || exit /b 1
copy /y build\sniplite_qr.exe "%STAGE%\sniplite_qr.exe" >nul || exit /b 1
copy /y third_party\zxing-cpp\LICENSE "%STAGE%\LICENSE-ZXing.txt" >nul || exit /b 1
copy /y third_party\microtex\LICENSE "%STAGE%\LICENSE-MicroTeX.txt" >nul || exit /b 1
copy /y third_party\tinyxml2\LICENSE.txt "%STAGE%\LICENSE-TinyXML2.txt" >nul || exit /b 1
copy /y build\sniplite_translate.dll "%STAGE%\translation\sniplite_translate.dll" >nul || exit /b 1
copy /y build\LICENSE-Translation.txt "%STAGE%\translation\LICENSE-Translation.txt" >nul || exit /b 1
for %%f in (PP-OCRv6_tiny_det-f16.gguf PP-OCRv6_tiny_rec-f16.gguf NOTICE.txt) do copy /y build\ocr\%%f "%STAGE%\ocr\" >nul || exit /b 1
copy /y build\ocr\sniplite_ocr.dll "%STAGE%\ocr\sniplite_ocr.dll" >nul || exit /b 1
copy /y build\ocr\LICENSE-OCREngine.txt "%STAGE%\ocr\LICENSE-OCREngine.txt" >nul || exit /b 1
for %%f in (crispembed.dll ggml.dll ggml-base.dll ggml-cpu.dll vcruntime140.dll vcruntime140_1.dll msvcp140_1.dll msvcp140.dll Apache-2.0.txt LICENSE NOTICE.marker NOTICE.sheenbidi NOTICE.teleocr) do copy /y build\ocr\shared-runtime\cpu-generic\%%f "%STAGE%\shared-runtime\cpu-generic\%%f" >nul || exit /b 1
if exist "%ZIP%" del /q "%ZIP%"
"%SystemRoot%\System32\tar.exe" -a -c -f "%ZIP%" -C "%STAGE%" sniplite.exe sniplite-cli.com sniplite_qr.exe LICENSE-ZXing.txt LICENSE-MicroTeX.txt LICENSE-TinyXML2.txt ocr shared-runtime translation || exit /b 1
if exist "%OLD%" rmdir /s /q "%OLD%"
if exist "%D%" move "%D%" "%OLD%" >nul 2>nul
if not exist "%D%" move "%STAGE%" "%D%" >nul
if exist "%STAGE%" rmdir /s /q "%STAGE%"
if exist "%OLD%" rmdir /s /q "%OLD%"
for %%f in ("%ZIP%") do echo %%~nxf  %%~zf bytes
