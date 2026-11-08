@echo off
setlocal
cd /d "%~dp0.."

set "CXX=x86_64-w64-mingw32-g++"
if not "%~1"=="" set "CXX=%~1"

echo compiler : %CXX%
"%CXX%" -O2 -std=c++11 -shared -static-libgcc -static-libstdc++ -o GameHUBFix.dll src\GameHUBFix.cpp -lpsapi
if errorlevel 1 (
  echo BUILD FAILED
  exit /b 1
)

echo.
echo --- exported symbols ^(expect 6^) ---
objdump -p GameHUBFix.dll | findstr /R "Initialize Reload Update GetString ExecuteBang Finalize"
echo.
echo Built successfully: %CD%\GameHUBFix.dll
echo Deploy with scripts\deploy.bat ^(quit Rainmeter first^).
endlocal
