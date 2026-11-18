@echo off
rem Release files, into build\dist. Run build.bat first.
rem
rem   sniplite_win_setup_amd64.exe                    the installer. Its name never changes: installed
rem                                                   copies ask for it by name when they update
rem   sniplite_<version>_portable_x64.exe             sniplite.exe by itself: nothing to install, it runs
rem                                                   from wherever it is put; keep sniplite_qr.exe beside it
rem   sniplite_qr.exe                                 shared QR reader used by the portable executables
rem   sniplite_<version>_portable_with_OCR_x64.exe    the same program carrying the text recognition
rem                                                   module: a zip of the ocr folder is put after it,
rem                                                   and it unpacks that beside itself when text is
rem                                                   first read (src\ocr_get.c)
rem
rem The module is taken from build\ocr: let build\sniplite.exe download it once (Settings, the OCR page;
rem it checks every file against the hashes in src\ocr_get.c). The module is the work of others, so
rem their licenses go with it, fetched from where they publish them. Nothing here is a program of its
rem own: copy, and the tar and curl that come with Windows.
rem
rem The zip is left in build\sniplite_ocr.zip. Settings downloads such a zip from the project's
rem releases, where one is published under a tag of its own (ocr-1): not with every version, only
rem when the module changes, and then its tag and size are written in src\ocr_get.c.
setlocal
cd /d "%~dp0"
if not exist build\sniplite.exe (echo build\sniplite.exe is missing: run build.bat first & exit /b 1)
if not exist build\sniplite_win_setup_amd64.exe (echo the installer is missing: run build.bat first & exit /b 1)
for %%f in (onnxruntime.dll PP-OCRv6_det_tiny.onnx PP-OCRv6_rec_tiny.onnx) do if not exist build\ocr\%%f (
  echo build\ocr\%%f is missing: start build\sniplite.exe and install the module from its Settings
  exit /b 1
)
for /f "tokens=3" %%v in ('findstr /C:"#define SNIPLITE_VERSION" src\common.h') do set V=%%v
set V=%V:L"=%
set V=%V:"=%
set TAR=%SystemRoot%\System32\tar.exe
set CURL=%SystemRoot%\System32\curl.exe
set D=build\dist
set Z=%D%\zip
if exist %D% rmdir /s /q %D%
mkdir %Z%\ocr || exit /b 1

copy /y build\sniplite_win_setup_amd64.exe %D% >nul || exit /b 1
copy /y build\sniplite.exe %D%\sniplite_%V%_portable_x64.exe >nul || exit /b 1
copy /y build\sniplite_qr.exe %D%\sniplite_qr.exe >nul || exit /b 1
copy /y third_party\quirc\LICENSE %D%\LICENSE-quirc.txt >nul || exit /b 1

for %%f in (onnxruntime.dll PP-OCRv6_det_tiny.onnx PP-OCRv6_rec_tiny.onnx) do copy /y build\ocr\%%f %Z%\ocr >nul || exit /b 1
copy /y res\ocr-notice.txt %Z%\ocr\NOTICE.txt >nul || exit /b 1
set RAW=https://raw.githubusercontent.com
"%CURL%" -sfL -o %Z%\ocr\LICENSE-onnxruntime.txt %RAW%/microsoft/onnxruntime/v1.26.0/LICENSE || goto :nolicense
"%CURL%" -sfL -o %Z%\ocr\ThirdPartyNotices-onnxruntime.txt %RAW%/microsoft/onnxruntime/v1.26.0/ThirdPartyNotices.txt || goto :nolicense
"%CURL%" -sfL -o %Z%\ocr\LICENSE-PaddleOCR.txt %RAW%/PaddlePaddle/PaddleOCR/main/LICENSE || goto :nolicense
"%TAR%" -a -c -f %D%\ocr.zip -C %Z% ocr || exit /b 1
copy /b /y build\sniplite.exe + %D%\ocr.zip %D%\sniplite_%V%_portable_with_OCR_x64.exe >nul || exit /b 1
move /y %D%\ocr.zip build\sniplite_ocr.zip >nul
rmdir /s /q %Z%

for %%f in (%D%\*) do echo %%~nxf  %%~zf bytes
exit /b 0

:nolicense
echo could not fetch the module's licenses: the program with the module was not made
exit /b 1
