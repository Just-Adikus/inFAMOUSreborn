@echo off
rem Менеджер сохранений inFamous: слоты для сохранений и загрузок
cd /d "%~dp0"
set "PYW=%LOCALAPPDATA%\Programs\Python\Python313\pythonw.exe"
if exist "%PYW%" (start "" "%PYW%" "infamous-recomp\tools\save_manager.py" & exit /b)
where pyw >/dev/null 2>/dev/null && (start "" pyw -3 "infamous-recomp\tools\save_manager.py" & exit /b)
start "" pythonw "infamous-recomp\tools\save_manager.py"
