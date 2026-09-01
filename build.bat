@echo off
setlocal EnableDelayedExpansion
title NetTool - Flash ESP32

cd /d "%~dp0"

echo ==========================================
echo        NETTOOL - FLASH ESP32
echo ==========================================
echo.

echo Chargement ESP-IDF 6.0.2...
call C:\esp\v6.0.2\esp-idf\export.bat

if errorlevel 1 goto ERROR

echo.
echo Ports COM disponibles :
echo ------------------------------------------

set count=0

for /f "usebackq tokens=1,* delims=|" %%A in (`powershell -NoProfile -Command ^
  "Get-PnpDevice -Class Ports -PresentOnly | Where-Object { $_.FriendlyName -match '\(COM\d+\)' } | ForEach-Object { if ($_.FriendlyName -match '\((COM\d+)\)') { Write-Output ($matches[1] + '|' + $_.FriendlyName) } }"`) do (
    set /a count+=1
    set "port!count!=%%A"
    set "name!count!=%%B"
    echo [!count!] %%A - %%B
)

echo ------------------------------------------

if !count! EQU 0 (
    echo.
    echo Aucun port COM detecte.
    echo.
    echo Verification manuelle :
    powershell -NoProfile -Command "Get-PnpDevice -Class Ports -PresentOnly"
    goto ERROR
)

echo.
set /p choice="Selectionne le port COM : "

if not defined port%choice% (
    echo.
    echo Choix invalide.
    goto ERROR
)

set "COMPORT=!port%choice%!"

echo.
echo Port selectionne : !COMPORT!
echo.

echo Compilation...
call idf.py build

if errorlevel 1 goto ERROR

echo.
echo Flash sur !COMPORT!...
call idf.py -p !COMPORT! flash

if errorlevel 1 goto ERROR

echo.
echo ==========================================
echo             FLASH OK
echo ==========================================
echo.
echo Ouverture du moniteur serie...
echo Pour quitter : CTRL + ]
echo.

call idf.py -p !COMPORT! monitor

exit /b 0

:ERROR
echo.
echo ==========================================
echo              ERREUR
echo ==========================================
echo.
pause
exit /b 1