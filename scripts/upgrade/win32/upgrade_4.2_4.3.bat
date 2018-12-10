@echo off
cd /d %~dp0
set PYTHONPATH=%PYTHONPATH%;%~dp0\..\restapi\thirdpart
start python py/upgrade_4.2_4.3.py
