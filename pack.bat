@echo off
rem Release files, into build\dist. Run build.bat first.
rem
rem   sniplite_win_setup_amd64.exe                    the installer. Its name never changes: installed
rem                                                   copies ask for it by name when they update
rem   sniplite_<version>_portable_x64.exe             sniplite.exe by itself: nothing to install, it runs
rem                                                   from wherever it is put; keep sniplite_qr.exe beside it
rem   sniplite_qr.exe                                 shared QR reader used by the portable executables
rem   sniplite_<version>_portable_with_OCR_x64.exe    the same program carrying the text recognition
rem                                                   module: the module's zip is put after it, and it
rem                                                   unpacks that beside itself when text is first
rem                                                   read (src\ocr_get.c)
rem
rem The module's zip is the one this project publishes under a tag of its own (ocr-1), the very file
rem Settings downloads: kept in build\sniplite_ocr.zip, fetched once with the curl that comes with
rem Windows. Its size must be the one src\ocr_get.c names, so the two cannot drift apart.
rem
rem A changed module is a new zip, made by hand: the ocr folder (onnxruntime.dll, the two models,
rem res\ocr-notice.txt as NOTICE.txt, the licenses of its parts), published under a new tag; the tag
rem goes here, the tag, the size and the hashes into src\ocr_get.c.
setlocal
cd /d "%~dp0"
if not exist build\sniplite.exe (echo build\sniplite.exe is missing: run build.bat first & exit /b 1)
if not exist build\sniplite_qr.exe (echo build\sniplite_qr.exe is missing: run build.bat first & exit /b 1)
if not exist build\sniplite_win_setup_amd64.exe (echo the installer is missing: run build.bat first & exit /b 1)
for /f "tokens=3" %%v in ('findstr /C:"#define SNIPLITE_VERSION" src\common.h') do set V=%%v
set V=%V:L"=%
set V=%V:"=%
for /f "tokens=3" %%v in ('findstr /C:"#define MODULE_BYTES" src\ocr_get.c') do set BYTES=%%v
set BYTES=%BYTES:u=%
set ZIP=build\sniplite_ocr.zip
set URL=https://github.com/MrTangLuyao/sniplite/releases/download/ocr-1/sniplite_ocr.zip
set D=build\dist
if exist %D% rmdir /s /q %D%
mkdir %D% || exit /b 1

if not exist %ZIP% "%SystemRoot%\System32\curl.exe" -sfL -o %ZIP% %URL%
if not exist %ZIP% (echo could not fetch the module's zip: %URL% & exit /b 1)
for %%f in (%ZIP%) do if not "%%~zf"=="%BYTES%" (
  echo %ZIP% is %%~zf bytes, src\ocr_get.c names %BYTES%: delete it to fetch the published one again
  exit /b 1
)

copy /y build\sniplite_win_setup_amd64.exe %D% >nul || exit /b 1
copy /y build\sniplite.exe %D%\sniplite_%V%_portable_x64.exe >nul || exit /b 1
copy /y build\sniplite_qr.exe %D%\sniplite_qr.exe >nul || exit /b 1
copy /y third_party\quirc\LICENSE %D%\LICENSE-quirc.txt >nul || exit /b 1
copy /b /y build\sniplite.exe + %ZIP% %D%\sniplite_%V%_portable_with_OCR_x64.exe >nul || exit /b 1

for %%f in (%D%\*) do echo %%~nxf  %%~zf bytes
exit /b 0
