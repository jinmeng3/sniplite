@echo off
rem Build the QR portable ZIP into build\dist. Run build.bat first.
setlocal
cd /d "%~dp0"
if not exist build\SnapRail.exe (echo build\SnapRail.exe is missing: run build.bat first & exit /b 1)
if not exist build\SnapRail_qr.exe (echo build\SnapRail_qr.exe is missing: run build.bat first & exit /b 1)
if not exist build\SnapRail.com (echo build\SnapRail.com is missing: run build.bat first & exit /b 1)
if not exist third_party\zxing-cpp\LICENSE (echo third_party\zxing-cpp\LICENSE is missing & exit /b 1)
for /f "tokens=3" %%v in ('findstr /C:"#define SNIPLITE_VERSION" src\common.h') do set V=%%v
set V=%V:L"=%
set V=%V:"=%
set D=build\dist\SnapRail_%V%_portable_QR_x64
set ZIP=build\dist\SnapRail_%V%_portable_QR_x64.zip
if not exist build\dist mkdir build\dist
if exist "%D%" rmdir /s /q "%D%"
mkdir "%D%" || exit /b 1
copy /y build\SnapRail.exe "%D%\SnapRail.exe" >nul || exit /b 1
copy /y build\SnapRail.com "%D%\SnapRail-cli.com" >nul || exit /b 1
copy /y build\SnapRail_qr.exe "%D%\SnapRail_qr.exe" >nul || exit /b 1
copy /y third_party\zxing-cpp\LICENSE "%D%\LICENSE-ZXing.txt" >nul || exit /b 1
copy /y third_party\microtex\LICENSE "%D%\LICENSE-MicroTeX.txt" >nul || exit /b 1
copy /y third_party\tinyxml2\LICENSE.txt "%D%\LICENSE-TinyXML2.txt" >nul || exit /b 1
if exist "%ZIP%" del /q "%ZIP%"
"%SystemRoot%\System32\tar.exe" -a -c -f "%ZIP%" -C "%D%" SnapRail.exe SnapRail-cli.com SnapRail_qr.exe LICENSE-ZXing.txt LICENSE-MicroTeX.txt LICENSE-TinyXML2.txt || exit /b 1
for %%f in ("%ZIP%") do echo %%~nxf  %%~zf bytes
