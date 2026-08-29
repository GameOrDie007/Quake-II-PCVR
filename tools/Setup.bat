@echo off
rem Prepare this folder to play.
rem
rem Copies Quake II, whichever expansions you own and the soundtrack out of
rem your own install, then builds the weapon wheel artwork from the same data.
rem Everything it produces is made here, from game data already on this
rem machine - none of it is distributed.
rem
rem Set Q2VR_QUAKEDIR first if Quake II is somewhere this cannot guess.

cd /d "%~dp0"

set Q2PY=
where python >nul 2>&1 && set Q2PY=python
if not defined Q2PY (
	where py >nul 2>&1 && set Q2PY=py
)

if not defined Q2PY (
	echo.
	echo Python 3 is needed to prepare the install, and was not found.
	echo Get it from https://www.python.org/downloads/ - tick
	echo "Add python.exe to PATH" while installing - then run this again.
	echo.
	pause
	exit /b 1
)

%Q2PY% tools\setup.py .

echo.
pause
