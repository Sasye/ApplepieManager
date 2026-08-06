@echo off

setlocal enabledelayedexpansion

echo ==========================================
echo   Applepie Manager Build Script
echo ==========================================
echo.

:: Set up MSVC environment
set "vswhere=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "vcvars="

if exist "%vswhere%" (
    for /f "usebackq tokens=*" %%i in (`"%vswhere%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
        set "install_path=%%i"
    )
)

if defined install_path (
    set "vcvars=!install_path!\VC\Auxiliary\Build\vcvars64.bat"
)

if defined vcvars if exist "!vcvars!" (
    echo [INFO] Found MSVC at: "!vcvars!"
    call "!vcvars!" >nul
)

where cl >nul 2>nul
if %errorlevel% neq 0 (
    echo [WARN] Automatic detection failed.
    set /p "user_path=Please drag and drop vcvars64.bat here and press Enter: "
    if exist "!user_path!" (
        call "!user_path!" >nul
    )
)

where cl >nul 2>nul
if %errorlevel% equ 0 (
    echo [SUCCESS] MSVC Environment Ready.
) else (
    echo [ERROR] Failed to set up MSVC environment.
    pause
    exit /b 1
)

echo [OK] MSVC compiler found
echo.

if not exist bin mkdir bin

:: Build applepie_manager.dll
echo [1/4] Compiling version resource ...
rc /nologo /fo bin\version.res src\version.rc

echo [2/4] Building applepie_manager.dll ...
cl /nologo /utf-8 /O2 /MD /LD /EHsc /std:c++17 ^
    /Ideps\minhook_lib\include ^
    /Ideps\imgui ^
    src\applepie_manager.cpp ^
    deps\imgui\imgui.cpp ^
    deps\imgui\imgui_draw.cpp ^
    deps\imgui\imgui_tables.cpp ^
    deps\imgui\imgui_widgets.cpp ^
    deps\imgui\imgui_impl_win32.cpp ^
    deps\imgui\imgui_impl_dx11.cpp ^
    bin\version.res ^
    deps\minhook_lib\lib\libMinHook.x64.lib ^
    user32.lib ^
    gdi32.lib ^
    d3d11.lib ^
    dxgi.lib ^
    dwmapi.lib ^
    ole32.lib ^
    /Fo"bin\\" ^
    /Fe"bin\applepie_manager.dll" ^
    /link /DLL

if %errorlevel% neq 0 (
    echo [ERROR] applepie_manager.dll build failed!
    pause
    exit /b 1
)
echo [OK] applepie_manager.dll built successfully
echo.

:: Build d3dcompiler_47.dll (proxy loader)
echo [3/4] Building d3dcompiler_47.dll (proxy loader) ...
cl /nologo /O2 /MD /LD /EHsc /std:c++17 ^
    src\proxy_d3dcompiler.cpp ^
    /Fo"bin\\" ^
    /Fe"bin\d3dcompiler_47.dll" ^
    /link /DLL

if %errorlevel% neq 0 (
    echo [ERROR] d3dcompiler_47.dll build failed!
    pause
    exit /b 1
)
echo [OK] d3dcompiler_47.dll built successfully
echo.

:: Build vulkan-1.dll (vulkan proxy loader)
echo [4/4] Building vulkan-1.dll (vulkan proxy loader) ...
cl /nologo /O2 /MD /LD /EHsc /std:c++17 ^
    src\proxy_vulkan_full.cpp ^
    /Fo"bin\\" ^
    /Fe"bin\vulkan-1.dll" ^
    /link /DLL

if %errorlevel% neq 0 (
    echo [ERROR] vulkan-1.dll build failed!
    pause
    exit /b 1
)
echo [OK] vulkan-1.dll built successfully
echo.

:: Clean up intermediate files
del /q bin\applepie_manager.obj 2>nul
del /q bin\applepie_manager.exp 2>nul
del /q bin\applepie_manager.lib 2>nul
del /q bin\imgui.obj 2>nul
del /q bin\imgui_draw.obj 2>nul
del /q bin\imgui_tables.obj 2>nul
del /q bin\imgui_widgets.obj 2>nul
del /q bin\imgui_impl_win32.obj 2>nul
del /q bin\imgui_impl_dx11.obj 2>nul
del /q bin\proxy_d3dcompiler.obj 2>nul
del /q bin\proxy_d3dcompiler.exp 2>nul
del /q bin\proxy_d3dcompiler.lib 2>nul
del /q bin\proxy_vulkan_full.obj 2>nul
del /q bin\proxy_vulkan_full.exp 2>nul
del /q bin\proxy_vulkan_full.lib 2>nul

echo ==========================================
echo   Build Complete!
echo ==========================================
echo.
echo Output files in bin\:
echo   - applepie_manager.dll   (Applepie Manager)
echo   - d3dcompiler_47.dll     (DX proxy loader)
echo   - vulkan-1.dll           (Vulkan proxy loader)
echo.
pause
