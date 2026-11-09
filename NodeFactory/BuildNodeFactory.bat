@echo off
rem ---------------------------------------------------------------------------
rem NodeFactory: builds the editor binaries of the plugin without rebuilding the
rem project. The editor cannot rebuild projects whose game sources are not
rem shipped (for example the Hello Neighbor Mod Kit), so the plugin is compiled
rem here in a temporary host project and the binaries are copied next to this
rem file.
rem
rem Usage: keep this file next to NodeFactory.uplugin and run it.
rem Optional argument: root folder of the engine used for building (the folder
rem that contains Engine\Build\BatchFiles\RunUAT.bat).
rem
rem Requirements: Visual Studio 2015 or 2017 with C++ tools, and an engine
rem 4.20 that can compile C++. When the engine of the
rem project cannot compile C++, Unreal Engine 4.20 from the Epic Games Launcher
rem is used and the BuildId of the binaries is set to the one of the project
rem engine.
rem ---------------------------------------------------------------------------
setlocal EnableExtensions DisableDelayedExpansion

set "PLUGIN_DIR=%~dp0"
set "PLUGIN_DIR=%PLUGIN_DIR:~0,-1%"
set "UPLUGIN=%PLUGIN_DIR%\NodeFactory.uplugin"
if not exist "%UPLUGIN%" goto :no_uplugin

rem Output folder: short path to stay below the Windows path limit.
set "OUT_ROOT=%SystemDrive%\NFBuild"
if not exist "%OUT_ROOT%\" mkdir "%OUT_ROOT%" 2>nul
if not exist "%OUT_ROOT%\" set "OUT_ROOT=%LOCALAPPDATA%\NFBuild"
if not exist "%OUT_ROOT%\" mkdir "%OUT_ROOT%" 2>nul
if not exist "%OUT_ROOT%\" goto :no_output
set "PACKAGE_DIR=%OUT_ROOT%\NodeFactory"
set "LOG=%OUT_ROOT%\BuildLog.txt"

rem Engine the project runs on: the closest parent folder with an engine.
set "TARGET_ENGINE="
call :find_engine_up "%PLUGIN_DIR%\.."

rem Engine used for building: argument, else the project engine, else UE 4.20 from the launcher.
set "BUILD_ENGINE="
set "CAN_BUILD="
if not "%~1"=="" set "BUILD_ENGINE=%~f1"
if defined BUILD_ENGINE goto :engine_chosen
if defined TARGET_ENGINE call :can_build "%TARGET_ENGINE%"
if defined CAN_BUILD set "BUILD_ENGINE=%TARGET_ENGINE%"
if defined BUILD_ENGINE goto :engine_chosen
call :find_launcher_engine
if defined BUILD_ENGINE goto :engine_chosen
goto :no_engine

:engine_chosen
call :can_build "%BUILD_ENGINE%"
if not defined CAN_BUILD goto :engine_cannot_build
if not defined TARGET_ENGINE set "TARGET_ENGINE=%BUILD_ENGINE%"

rem Visual Studio 2015 or 2017 are the compilers of UE 4.20.
set "VSFOUND="
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VS2015=%ProgramFiles(x86)%\Microsoft Visual Studio 14.0"
if exist "%OUT_ROOT%\vs2017.txt" del "%OUT_ROOT%\vs2017.txt"
if exist "%VSWHERE%" "%VSWHERE%" -version "[15.0,16.0)" -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath > "%OUT_ROOT%\vs2017.txt" 2>nul
if exist "%OUT_ROOT%\vs2017.txt" set /p VSFOUND=<"%OUT_ROOT%\vs2017.txt"
if not defined VSFOUND if exist "%VS2015%\VC\bin\amd64\cl.exe" set "VSFOUND=%VS2015%"
if not defined VSFOUND if exist "%VS2015%\VC\bin\x86_amd64\cl.exe" set "VSFOUND=%VS2015%"

> "%LOG%" echo NodeFactory build log
>>"%LOG%" echo Plugin:             %UPLUGIN%
>>"%LOG%" echo Build engine:       %BUILD_ENGINE%
>>"%LOG%" echo Project engine:     %TARGET_ENGINE%
>>"%LOG%" echo Visual Studio:      %VSFOUND%
>>"%LOG%" echo.

echo.
echo [NodeFactory] Plugin:         %UPLUGIN%
echo [NodeFactory] Build engine:   %BUILD_ENGINE%
echo [NodeFactory] Project engine: %TARGET_ENGINE%
if not defined VSFOUND goto :vs_warning
echo [NodeFactory] Visual Studio:  %VSFOUND%
goto :build

:vs_warning
echo.
echo [NodeFactory] WARNING: Visual Studio 2015 or 2017 with C++ tools was not found.
echo   Unreal Engine 4.20 compiles with Visual Studio 2015 or 2017; 2019 and 2022 do not fit.
echo   Installer of Visual Studio 2017: https://aka.ms/vs/15/release/vs_community.exe
echo   Workload:  "Game development with C++".
echo   The build is started anyway, the log shows the exact reason if it fails.

:build
echo.
echo [NodeFactory] Building. This takes 5-20 minutes, the window stays quiet meanwhile.
echo [NodeFactory] Full log: %LOG%
call "%BUILD_ENGINE%\Engine\Build\BatchFiles\RunUAT.bat" BuildPlugin -Plugin="%UPLUGIN%" -Package="%PACKAGE_DIR%" -NoTargetPlatforms -TargetPlatforms=Win64 >> "%LOG%" 2>&1
set "UAT_EXIT=%ERRORLEVEL%"
>>"%LOG%" echo.
>>"%LOG%" echo RunUAT exit code: %UAT_EXIT%

rem Packaged binaries when everything succeeded; otherwise the editor binaries left in the host project.
set "PARTIAL="
set "BIN_SRC=%PACKAGE_DIR%\Binaries"
if "%UAT_EXIT%"=="0" if exist "%BIN_SRC%\Win64\UE4Editor-NodeFactoryEditor.dll" goto :have_binaries
set "PARTIAL=1"
set "BIN_SRC=%PACKAGE_DIR%\HostProject\Plugins\NodeFactory\Binaries"
if exist "%BIN_SRC%\Win64\UE4Editor-NodeFactoryEditor.dll" if exist "%BIN_SRC%\Win64\UE4Editor-NodeFactory.dll" if exist "%BIN_SRC%\Win64\UE4Editor.modules" goto :have_binaries
goto :build_failed

:have_binaries
rem Binaries built by another engine get the BuildId of the project engine, otherwise it rejects them.
if /i "%BUILD_ENGINE%"=="%TARGET_ENGINE%" goto :copy_binaries
set "NF_TARGET_MODULES=%TARGET_ENGINE%\Engine\Binaries\Win64\UE4Editor.modules"
set "NF_PLUGIN_MODULES=%BIN_SRC%\Win64\UE4Editor.modules"
powershell -NoProfile -ExecutionPolicy Bypass -Command "$ErrorActionPreference = 'Stop'; $t = Get-Content -Raw -LiteralPath $env:NF_TARGET_MODULES | ConvertFrom-Json; $m = Get-Content -Raw -LiteralPath $env:NF_PLUGIN_MODULES | ConvertFrom-Json; Write-Output ('BuildId of the build engine:   ' + $m.BuildId); $m.BuildId = $t.BuildId; [IO.File]::WriteAllText($env:NF_PLUGIN_MODULES, ($m | ConvertTo-Json -Depth 5)); Write-Output ('BuildId of the project engine: ' + $t.BuildId)" >> "%LOG%" 2>&1
if errorlevel 1 goto :buildid_failed

:copy_binaries
xcopy "%BIN_SRC%" "%PLUGIN_DIR%\Binaries" /E /I /Y /Q >> "%LOG%" 2>&1
if errorlevel 1 goto :copy_failed
if defined PARTIAL goto :partial_done
echo.
echo [NodeFactory] Done. The editor binaries are in:
echo   %PLUGIN_DIR%\Binaries\Win64
echo Start the editor as usual: the "Missing Modules" dialog should not appear any more.
goto :end

:partial_done
echo.
echo [NodeFactory] The editor binaries were built and copied, the editor can be started.
echo   Another part of the build failed, send this file anyway: %LOG%
start "" notepad "%LOG%"
goto :end

:no_uplugin
echo [NodeFactory] NodeFactory.uplugin was not found next to this file.
goto :end

:no_output
echo [NodeFactory] Cannot create the output folder %OUT_ROOT%.
goto :end

:no_engine
echo [NodeFactory] No engine that can compile C++ was found.
echo   Install Unreal Engine 4.20.3 from the Epic Games Launcher
echo   (Unreal Engine - Library - Engine Versions - "+" - 4.20.3)
echo   or pass the engine folder as the argument of this file.
goto :end

:engine_cannot_build
echo [NodeFactory] The engine %BUILD_ENGINE% cannot compile C++:
echo   Engine\Binaries\DotNET\UnrealBuildTool.exe or the engine headers are missing.
echo   Install Unreal Engine 4.20.3 from the Epic Games Launcher and run this file again,
echo   or pass the folder of another engine 4.20 as the argument.
goto :end

:build_failed
echo.
echo [NodeFactory] The build failed. Send this file: %LOG%
start "" notepad "%LOG%"
goto :end

:buildid_failed
echo.
echo [NodeFactory] The BuildId could not be set. Send this file: %LOG%
start "" notepad "%LOG%"
goto :end

:copy_failed
echo.
echo [NodeFactory] The binaries were built but could not be copied into the plugin folder.
echo   Copy %BIN_SRC% into %PLUGIN_DIR% manually,
echo   or run this file as administrator.
goto :end

:end
echo.
pause
endlocal
exit /b

rem --- Subroutines -------------------------------------------------------------

:find_engine_up
set "NF_DIR=%~f1"
:find_engine_up_loop
if exist "%NF_DIR%\Engine\Binaries\Win64\UE4Editor.modules" goto :find_engine_up_found
for %%I in ("%NF_DIR%\..") do set "NF_PARENT=%%~fI"
if /i "%NF_PARENT%"=="%NF_DIR%" goto :eof
set "NF_DIR=%NF_PARENT%"
goto :find_engine_up_loop
:find_engine_up_found
set "TARGET_ENGINE=%NF_DIR%"
goto :eof

:can_build
set "CAN_BUILD="
if exist "%~1\Engine\Build\BatchFiles\RunUAT.bat" if exist "%~1\Engine\Binaries\DotNET\UnrealBuildTool.exe" if exist "%~1\Engine\Source\Runtime\Core\Public\CoreMinimal.h" set "CAN_BUILD=1"
goto :eof

:find_launcher_engine
for /f "tokens=2,*" %%A in ('reg query "HKLM\SOFTWARE\EpicGames\Unreal Engine\4.20" /v InstalledDirectory /reg:64 2^>nul ^| find /i "InstalledDirectory"') do set "BUILD_ENGINE=%%B"
if defined BUILD_ENGINE goto :eof
for /f "tokens=2,*" %%A in ('reg query "HKLM\SOFTWARE\EpicGames\Unreal Engine\4.20" /v InstalledDirectory /reg:32 2^>nul ^| find /i "InstalledDirectory"') do set "BUILD_ENGINE=%%B"
if defined BUILD_ENGINE goto :eof
if exist "%ProgramFiles%\Epic Games\UE_4.20\Engine\Build\BatchFiles\RunUAT.bat" set "BUILD_ENGINE=%ProgramFiles%\Epic Games\UE_4.20"
goto :eof
