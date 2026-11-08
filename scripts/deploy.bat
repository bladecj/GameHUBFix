@echo off
setlocal
cd /d "%~dp0.."

set "TARGET=%APPDATA%\Rainmeter\Plugins"

echo Target: "%TARGET%"
echo.
echo IMPORTANT: quit Rainmeter before continuing, otherwise the DLL is locked.
echo Press Ctrl+C to abort, or
pause

if not exist "%TARGET%" mkdir "%TARGET%"

if not exist "dist\GameHUBFix.dll" (
  echo dist\GameHUBFix.dll not found. Run scripts\build.bat first.
  exit /b 1
)

copy /Y "dist\GameHUBFix.dll" "%TARGET%\GameHUBFix.dll"
if errorlevel 1 (
  echo COPY FAILED - is Rainmeter still running?
  exit /b 1
)
copy /Y "config\GameHUBFix.ini" "%TARGET%\GameHUBFix.ini"

echo.
echo Deployed. Start Rainmeter again, then refresh "Another dock\PadListener".
endlocal
