@echo off
setlocal
cd /d "%~dp0"
set "PYTHON_EXE=%USERPROFILE%\.platformio\penv\Scripts\python.exe"
if exist "%PYTHON_EXE%" goto start_server
where python >nul 2>nul
if not errorlevel 1 (
  set "PYTHON_EXE=python"
  goto start_server
)
echo Python was not found.
echo Install PlatformIO or Python, or publish this folder on an HTTPS static host.
pause
exit /b 1

:start_server
start "Battery Display web server" /min "%PYTHON_EXE%" -m http.server 8000
timeout /t 2 /nobreak >nul
start "" "http://localhost:8000/installer/"
echo Battery Display installer opened at http://localhost:8000/installer/
echo Close the separate web server window when finished.
pause
