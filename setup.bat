@echo off

rem RELEASE

if not exist venv python -m virtualenv venv

rem Downloading the default VTK wheel version. If you wish to use a custom wheel,
rem please follow the same steps using the wheel you prefer.
if not exist VTK-8.1.2-cp37-cp37m-win_amd64.whl powershell -Command "Invoke-WebRequest -UseBasicParsing https://download.lfd.uci.edu/pythonlibs/y2rycu7g/VTK-8.1.2-cp37-cp37m-win_amd64.whl -OutFile VTK-8.1.2-cp37-cp37m-win_amd64.whl"
venv\Scripts\Activate & pip install VTK-8.1.2-cp37-cp37m-win_amd64.whl & Deactivate

rem DEBUG

if not exist venv-dbg python -m virtualenv venv-dbg
