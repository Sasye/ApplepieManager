@echo off
setlocal
cd /d "%~dp0"
where cl >nul 2>nul
if errorlevel 1 for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do call "%%i\VC\Auxiliary\Build\vcvars64.bat" >nul
where cl >nul 2>nul
if errorlevel 1 exit /b 1
if not exist bin\foundation\plugin mkdir bin\foundation\plugin
for %%i in (0 1) do (
    cl /nologo /utf-8 /O2 /MT /LD /EHsc /std:c++17 /DSERVICE_PROVIDER=%%i service_plugin.cpp /Fo:bin\foundation\service%%i.obj /Fe:bin\foundation\plugin\service%%i.dll /link /INCREMENTAL:NO
    if errorlevel 1 exit /b 1
)
for /l %%i in (1,1,11) do (
    cl /nologo /utf-8 /O2 /MT /LD /EHsc /std:c++17 /DFIXTURE=%%i dependency_fixture.cpp /Fo:bin\foundation\fixture%%i.obj /Fe:bin\foundation\plugin\fixture%%i.dll /link /INCREMENTAL:NO
    if errorlevel 1 exit /b 1
)
cl /nologo /utf-8 /O2 /MT /EHsc /std:c++17 /W4 /wd4505 /I..\..\deps\minhook_lib\include ^
    foundation_verify.cpp ..\..\src\hub_core.cpp ..\..\src\hub_shared.cpp ^
    ..\..\src\hub_runtime.cpp ..\..\src\hub_pointers.cpp ..\..\src\hub_windows.cpp ^
    ..\..\src\hub_lifecycle.cpp ..\..\src\manager_loader.cpp ^
    ..\..\deps\minhook_lib\lib\libMinHook.x64.lib user32.lib comctl32.lib ^
    /Fo"bin\foundation\\" /Fe:bin\foundation\foundation_verify.exe /link /INCREMENTAL:NO /OPT:NOICF /LTCG
if errorlevel 1 exit /b 1
pushd bin\foundation
for %%i in (services logs uid uid-stop loader loader-delayed loader-attach-loss) do (
    foundation_verify.exe %%i
    if errorlevel 1 (popd & exit /b 1)
)
popd
exit /b 0
