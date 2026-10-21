@echo off
rem Distort Me - link a folder with DistortMe.8bf into Photoshop's Plug-ins
rem folder, so a new build only has to be copied into that folder.
rem
rem Usage:  link-plugin-windows.bat [folder with DistortMe.8bf] [Photoshop Plug-ins folder]
rem Both are optional: the first defaults to the folder of this script, the
rem second to Photoshop 2026's Plug-ins folder.
rem
rem It creates a directory junction "Plug-ins\Distort Me". Writing into
rem Program Files needs admin rights, so the script asks for them.
setlocal

set "SRC=%~1"
if "%SRC%"=="" set "SRC=%~dp0"
if "%SRC:~-1%"=="\" set "SRC=%SRC:~0,-1%"
set "PLUGINS=%~2"
if "%PLUGINS%"=="" set "PLUGINS=C:\Program Files\Adobe\Adobe Photoshop 2026\Plug-ins"
set "LINK=%PLUGINS%\Distort Me"

rem restart with admin rights if needed
net session >nul 2>&1
if errorlevel 1 (
    echo Asking for admin rights...
    powershell -NoProfile -Command "Start-Process -FilePath '%~f0' -ArgumentList '\"%SRC%\" \"%PLUGINS%\"' -Verb RunAs"
    exit /b
)

if not exist "%SRC%\DistortMe.8bf" (
    echo No DistortMe.8bf in "%SRC%".
    goto done
)
if not exist "%PLUGINS%" (
    echo Photoshop's Plug-ins folder was not found: "%PLUGINS%"
    echo Pass it as the second argument.
    goto done
)
if exist "%PLUGINS%\DistortMe.8bf" (
    echo Removing the old copy "%PLUGINS%\DistortMe.8bf" so Photoshop doesn't load it twice.
    del "%PLUGINS%\DistortMe.8bf"
)
if exist "%LINK%" (
    echo Replacing the existing "%LINK%".
    rmdir "%LINK%"
)
mklink /J "%LINK%" "%SRC%"
if errorlevel 1 (
    echo Could not create the link.
    goto done
)
echo.
echo Done. Photoshop now loads DistortMe.8bf from "%SRC%".
echo Restart Photoshop. To update, close Photoshop and replace the .8bf in that folder.

:done
echo.
pause
