@echo off
rem Prepare this folder to play.
rem
rem Copies Quake II, whichever expansions you own and the soundtrack out of
rem your own install, then builds the weapon wheel artwork from the same data.
rem Everything it produces is made here, from game data already on this
rem machine - none of it is distributed.
rem
rem Nothing has to be installed first. This runs on the PowerShell that comes
rem with Windows, and reads your Quake II from this machine - there is no
rem download and no other dependency.
rem
rem Set Q2VR_QUAKEDIR first if Quake II is somewhere this cannot guess.

cd /d "%~dp0"

rem -ExecutionPolicy Bypass applies to this one run only. It changes no system
rem setting, and is what lets a downloaded script run without the user having to
rem alter anything.
powershell -NoProfile -ExecutionPolicy Bypass -File "tools\setup.ps1" "."

if errorlevel 1 goto failed
echo.
pause
exit /b 0

:failed
echo.
echo Setup did not finish. The messages above say why.
echo.
pause
exit /b 1
