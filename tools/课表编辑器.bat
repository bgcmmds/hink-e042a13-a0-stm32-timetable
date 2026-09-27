@echo off
REM ---------------------------------------------------------------
REM  Timetable editor launcher
REM  Starts the local server and opens the browser.
REM  Closing this window stops the server.
REM ---------------------------------------------------------------

REM Go to the repository root (this .bat lives in tools/).
cd /d "%~dp0.."
if errorlevel 1 (
    echo.
    echo Failed to enter the project root folder.
    echo.
    pause
    exit /b 1
)

REM Prefer the "py" launcher; fall back to "python" from PATH.
where py >nul 2>nul
if %errorlevel%==0 (
    py "tools\server.py"
    goto :end
)

where python >nul 2>nul
if %errorlevel%==0 (
    python "tools\server.py"
    goto :end
)

echo.
echo Python not found.
echo Please install Python 3 and make sure it is on your PATH.
echo (Or edit this .bat and replace "python" with the full path
echo  to your python.exe, for example C:\Python312\python.exe)
echo.
pause

:end
