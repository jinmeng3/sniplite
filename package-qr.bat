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
set STAGE=build\dist\SnapRail_%V%_portable_QR_x64_stage
set OLD=build\dist\SnapRail_%V%_portable_QR_x64_old
set ZIP=build\dist\SnapRail_%V%_portable_QR_x64.zip
set ZIPTMP=build\dist\SnapRail_%V%_portable_QR_x64_tmp.zip
if not exist build\dist mkdir build\dist || exit /b 1
if exist "%STAGE%" rmdir /s /q "%STAGE%"
mkdir "%STAGE%" || exit /b 1
copy /y build\SnapRail.exe "%STAGE%\SnapRail.exe" >nul || exit /b 1
copy /y build\SnapRail.com "%STAGE%\SnapRail-cli.com" >nul || exit /b 1
copy /y build\SnapRail_qr.exe "%STAGE%\SnapRail_qr.exe" >nul || exit /b 1
copy /y third_party\zxing-cpp\LICENSE "%STAGE%\LICENSE-ZXing.txt" >nul || exit /b 1
copy /y third_party\microtex\LICENSE "%STAGE%\LICENSE-MicroTeX.txt" >nul || exit /b 1
copy /y third_party\tinyxml2\LICENSE.txt "%STAGE%\LICENSE-TinyXML2.txt" >nul || exit /b 1
if exist "%ZIPTMP%" del /q "%ZIPTMP%"
"%SystemRoot%\System32\tar.exe" -a -c -f "%ZIPTMP%" -C "%STAGE%" SnapRail.exe SnapRail-cli.com SnapRail_qr.exe LICENSE-ZXing.txt LICENSE-MicroTeX.txt LICENSE-TinyXML2.txt || exit /b 1
if exist "%ZIP%" move /y "%ZIP%" "%ZIP%.old" >nul || exit /b 1
move /y "%ZIPTMP%" "%ZIP%" >nul || (if exist "%ZIP%.old" move /y "%ZIP%.old" "%ZIP%" >nul & exit /b 1)
if exist "%ZIP%.old" del /q "%ZIP%.old"
if exist "%OLD%" rmdir /s /q "%OLD%"
if exist "%D%" move "%D%" "%OLD%" >nul || exit /b 1
move "%STAGE%" "%D%" >nul || (if exist "%OLD%" move "%OLD%" "%D%" >nul & exit /b 1)
if exist "%OLD%" rmdir /s /q "%OLD%"
for %%f in ("%ZIP%") do echo %%~nxf  %%~zf bytes
