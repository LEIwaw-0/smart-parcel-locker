@echo off
cd /d %~dp0
echo ============================================
echo   Smart Locker Cloud Platform
echo   Browser: http://127.0.0.1:8000
echo   Press Ctrl+C to stop
echo ============================================
REM Auto-install dependencies if missing (fast when already installed)
E:\66\Scripts\python.exe -m pip install -r requirements.txt --quiet --disable-pip-version-check
E:\66\Scripts\python.exe server.py
pause
