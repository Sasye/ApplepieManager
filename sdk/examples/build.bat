@echo off
setlocal
cd /d "%~dp0"

if /i "%~1"=="/verify" goto args_ok
if not "%~1"=="" (
    echo Usage: build.bat [/verify]
    exit /b 2
)
:args_ok
where cl >nul 2>nul
if errorlevel 1 (
    if not exist "%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" (
        echo [ERROR] Use an x64 Developer Command Prompt or install MSVC.
        exit /b 1
    )
    for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do call "%%i\VC\Auxiliary\Build\vcvars64.bat" >nul
)
where cl >nul 2>nul
if errorlevel 1 exit /b 1
if not exist bin mkdir bin
cl /nologo /utf-8 /O2 /MT /LD /EHsc /std:c++17 /W4 /wd4505 minimal_plugin.cpp ^
    /Fo:bin\minimal_plugin.obj /Fe:bin\applepie_example.dll /link /INCREMENTAL:NO /OPT:NOICF
if errorlevel 1 exit /b 1
echo [OK] %CD%\bin\applepie_example.dll
cl /nologo /utf-8 /O2 /MT /LD /EHsc /std:c++17 /W4 /wd4505 config_plugin.cpp ^
    /Fo:bin\config_plugin.obj /Fe:bin\applepie_config_example.dll /link /INCREMENTAL:NO
if errorlevel 1 exit /b 1
for %%i in (0 1) do (
    cl /nologo /utf-8 /O2 /MT /LD /EHsc /std:c++17 /W4 /wd4505 /DSERVICE_PROVIDER=%%i service_plugin.cpp ^
        /Fo:bin\service%%i.obj /Fe:bin\applepie_service%%i.dll /link /INCREMENTAL:NO
    if errorlevel 1 exit /b 1
)
if /i not "%~1"=="/verify" exit /b 0

rem Optional offline verification against this repository's REAL Core backend.
rem No game process, deployment, injection or private plugin hook library.
cl /nologo /utf-8 /TC /std:c11 /W4 /c abi_verify.c /Fo:bin\abi_verify.obj
if errorlevel 1 exit /b 1
ml64 /nologo /c /Fo bin\hook_thunks.obj hook_thunks.asm
if errorlevel 1 exit /b 1
cl /nologo /utf-8 /O2 /MT /EHsc /std:c++17 /W4 /wd4505 /I..\..\deps\minhook_lib\include ^
    verify.cpp bin\hook_thunks.obj ..\..\src\hub_core.cpp ..\..\src\hub_shared.cpp ^
    ..\..\src\hub_runtime.cpp ..\..\src\hub_pointers.cpp ^
    ..\..\src\hub_windows.cpp ..\..\src\hub_lifecycle.cpp ..\..\src\hub_config.cpp ^
    ..\..\deps\minhook_lib\lib\libMinHook.x64.lib user32.lib comctl32.lib ^
    /Fo"bin\\" /Fe:bin\sdk_verify.exe /link /INCREMENTAL:NO /OPT:NOICF /LTCG
if errorlevel 1 exit /b 1
bin\sdk_verify.exe
if errorlevel 1 exit /b 1
bin\sdk_verify.exe entry-changed
if errorlevel 1 exit /b 1
cl /nologo /utf-8 /O2 /MT /EHsc /std:c++17 /W4 /wd4505 /I..\..\deps\minhook_lib\include ^
    config_verify.cpp bin\hub_core.obj bin\hub_shared.obj bin\hub_runtime.obj ^
    bin\hub_pointers.obj bin\hub_windows.obj bin\hub_lifecycle.obj bin\hub_config.obj ^
    ..\..\deps\minhook_lib\lib\libMinHook.x64.lib user32.lib comctl32.lib ^
    /Fo:bin\config_verify.obj /Fe:bin\config_verify.exe /link /INCREMENTAL:NO /LTCG
if errorlevel 1 exit /b 1
for %%i in (behavior persistence corrupt plugin notifications hotkeys hotkeys-reload) do (
    bin\config_verify.exe %%i
    if errorlevel 1 exit /b 1
)
exit /b 0
