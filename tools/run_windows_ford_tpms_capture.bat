@echo off
setlocal
cd /d "%~dp0\.."

where py >nul 2>nul
if errorlevel 1 (
    echo Python launcher was not found. Install Python 3 from python.org and enable the launcher.
    pause
    exit /b 2
)

py -m pip show pyserial >nul 2>nul
if errorlevel 1 (
    echo Installing pyserial...
    py -m pip install pyserial
    if errorlevel 1 (
        echo Failed to install pyserial. Check network access and try again.
        pause
        exit /b 2
    )
)

py tools\windows_ford_tpms_capture.py %*
echo.
echo Capture finished. See the ford_tpms_capture folder for TXT, JSON, and UART logs.
pause
