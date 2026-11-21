@echo off
setlocal
set "R=%~dp0webview2-runtime"
if not exist "%R%\msedgewebview2.exe" (
  echo The bundled WebView2 Fixed Version Runtime is missing.
  exit /b 1
)
"%SystemRoot%\System32\icacls.exe" "%R%" /grant "*S-1-15-2-2:(OI)(CI)(RX)" >nul || exit /b 1
"%SystemRoot%\System32\icacls.exe" "%R%" /grant "*S-1-15-2-1:(OI)(CI)(RX)" >nul || exit /b 1
start "" "%~dp0sniplite.exe"
