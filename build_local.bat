@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cd /d "C:\Users\Adige\Downloads\Win10-Kernel32-Patcher"
if exist build rmdir /s /q build
cmake -S Win10CompatRuntime -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Release 2>&1
if errorlevel 1 exit /b 1
cmake --build build --config Release 2>&1
