@echo off
cd /d %~dp0
set PYTHONPATH=%PYTHONPATH%;%~dp0\restapi\thirdpart
start python upgrade/py/gc.py
