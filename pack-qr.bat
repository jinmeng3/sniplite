@echo off
rem Build the QR portable ZIP into build\dist. Run build.bat first.
setlocal
cd /d "%~dp0"
if not exist build\sniplite.exe (echo build\sniplite.exe is missing: run build.bat first & exit /b 1)
if not exist build\sniplite_qr.exe (echo build\sniplite_qr.exe is missing: run build.bat first & exit /b 1)
if not exist build\sniplite.com (echo build\sniplite.com is missing: run build.bat first & exit /b 1)
if not exist third_party\zxing-cpp\LICENSE (echo third_party\zxing-cpp\LICENSE is missing & exit /b 1)
for /f "tokens=3" %%v in ('findstr /C:"#define SNIPLITE_VERSION" src\common.h') do set V=%%v
set V=%V:L"=%
set V=%V:"=%
set D=build\dist\sniplite_%V%_portable_QR_x64
set ZIP=build\dist\sniplite_%V%_portable_QR_x64.zip
if not exist build\dist mkdir build\dist
if exist "%D%" rmdir /s /q "%D%"
mkdir "%D%" || exit /b 1
copy /y build\sniplite.exe "%D%\sniplite.exe" >nul || exit /b 1
copy /y build\sniplite.com "%D%\sniplite-cli.com" >nul || exit /b 1
copy /y build\sniplite_qr.exe "%D%\sniplite_qr.exe" >nul || exit /b 1
copy /y third_party\zxing-cpp\LICENSE "%D%\LICENSE-ZXing.txt" >nul || exit /b 1
if exist "%ZIP%" del /q "%ZIP%"
"%SystemRoot%\System32\tar.exe" -a -c -f "%ZIP%" -C "%D%" sniplite.exe sniplite-cli.com sniplite_qr.exe LICENSE-ZXing.txt || exit /b 1
for %%f in ("%ZIP%") do echo %%~nxf  %%~zf bytes
