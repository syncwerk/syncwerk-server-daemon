@echo off
cd /d %~dp0
set PYTHONPATH=%PYTHONPATH%;%~dp0\..\restapi\thirdpart
start python py/upgrade_4.0_4.1.py
