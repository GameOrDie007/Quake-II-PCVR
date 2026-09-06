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

rem Ask each candidate to run something trivial rather than trusting "where".
rem On a Windows machine with no Python, "where python" still succeeds: it finds
rem the Microsoft Store app execution alias in WindowsApps, which opens the Store
rem and exits 0 without running anything. Setup would then appear to do nothing
rem at all, which is the worst failure this script can have.
set Q2PY=
for %%P in (python py python3) do (
	if not defined Q2PY (
		%%P -c "import sys; assert sys.version_info[0]==3" >nul 2>&1 && set Q2PY=%%P
	)
)

if not defined Q2PY (
	echo.
	echo Python 3 is needed to prepare the install, and no working one was
	echo found. Get it from https://www.python.org/downloads/ - tick
	echo "Add python.exe to PATH" while installing - then run this again.
	echo.
	echo If typing "python" opens the Microsoft Store, that is a placeholder
	echo rather than Python. Installing from the link above replaces it.
	echo.
	pause
	exit /b 1
)

%Q2PY% tools\setup.py .

echo.
pause
