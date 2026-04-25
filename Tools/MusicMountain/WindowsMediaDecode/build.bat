@echo off
setlocal

set OUT_DIR=%~dp0..\bin
if not exist "%OUT_DIR%" mkdir "%OUT_DIR%"

cl /nologo /EHsc /std:c++17 "%~dp0MediaFoundationDecode.cpp" /Fe:"%OUT_DIR%\mf_decode.exe" mfplat.lib mfreadwrite.lib mfuuid.lib propsys.lib ole32.lib
exit /b %ERRORLEVEL%
