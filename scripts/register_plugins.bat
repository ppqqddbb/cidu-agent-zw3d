@echo off
setlocal EnableExtensions

if not defined ZW3D_USER_DIR set "ZW3D_USER_DIR=%APPDATA%\ZWSOFT\ZW3D\ZW3D 2026"

rem Resolve the real ZW3D runtime apilibs directory before choosing an install target.
rem ZW3D auto-loads plugins from <InstallLocation>\apilibs; a separate staging folder
rem such as D:\File\3DPlugin is not necessarily used by the running process.
if not defined ZW3D_YEAR (
  for /f "tokens=2,*" %%A in ('REG QUERY "HKLM\Software\ZWSOFT\ZW3D\CurrentVersion" /v LastInstallVersion 2^>nul ^| find "LastInstallVersion"') do set "ZW3D_YEAR=%%B"
)
if defined ZW3D_YEAR set "ZW3D_INSTALL_KEY=HKLM\Software\ZWSOFT\ZW3D\%ZW3D_YEAR%\ZW3D %ZW3D_YEAR%"
if not defined ZW3D_INSTALL_LOCATION if defined ZW3D_INSTALL_KEY (
  for /f "tokens=2,*" %%A in ('REG QUERY "%ZW3D_INSTALL_KEY%" /v InstallLocation 2^>nul ^| find "InstallLocation"') do set "ZW3D_INSTALL_LOCATION=%%B"
)

if defined CIDU_PLUGIN_DIR (
  set "PLUGIN_DIR=%CIDU_PLUGIN_DIR%"
) else if defined ZW3D_INSTALL_LOCATION (
  set "PLUGIN_DIR=%ZW3D_INSTALL_LOCATION%\apilibs"
) else if exist "D:\File\3DPlugin\" (
  rem Compatibility fallback only when the ZW3D installation cannot be detected.
  set "PLUGIN_DIR=D:\File\3DPlugin"
) else (
  set "PLUGIN_DIR=%ZW3D_USER_DIR%\custom\apilibs"
)
for %%I in ("%PLUGIN_DIR%") do set "PLUGIN_DIR=%%~fI"
if not exist "%PLUGIN_DIR%" mkdir "%PLUGIN_DIR%"
echo Installing Cidu plugins to: %PLUGIN_DIR%

copy /Y "%~dp0..\build\Release\ZW3DAgentPlugin.dll" "%PLUGIN_DIR%\" >nul
if errorlevel 1 (
  echo Failed to copy ZW3DAgentPlugin.dll. Close ZW3D and run this script again.
  exit /b 1
)
copy /Y "%~dp0..\build\Release\CiduParametricEngine.dll" "%PLUGIN_DIR%\" >nul
if errorlevel 1 (
  echo Failed to copy CiduParametricEngine.dll. Close ZW3D and run this script again.
  exit /b 1
)
if exist "%~dp0..\build\Release\ZW3DAgentPlugin.zrc" (
  copy /Y "%~dp0..\build\Release\ZW3DAgentPlugin.zrc" "%PLUGIN_DIR%\" >nul
  if errorlevel 1 (
    echo Failed to copy ZW3DAgentPlugin.zrc. Close ZW3D and run this script again.
    exit /b 1
  )
)

if exist "%PLUGIN_DIR%\Settings\Default" (
  powershell -NoProfile -ExecutionPolicy Bypass -Command "$root=(Resolve-Path '%PLUGIN_DIR%').Path; $settings=Join-Path $root 'Settings\Default'; $targets=@('Action\CiduZW3D.zcui','Environment-2\Controls\CiduZW3D.zcui','ResourcePool\CiduZW3D.zcui','ResourcePool\RibbonPages.zcui','ResourcePool\RibbonPagesUser.zcui','ResourcePool\RibbonQuickAccessMenus.zcui','Strategy\Environment-0-Top\LayoutStrategy.zcui','Strategy\Environment-1-Root\LayoutStrategy.zcui','Strategy\Environment-2-Z3\LayoutStrategy.zcui','Strategy\CiduZW3D'); foreach($target in $targets){ $path=Join-Path $settings $target; if(Test-Path -LiteralPath $path){ $resolved=(Resolve-Path -LiteralPath $path).Path; if($resolved.StartsWith($settings,[System.StringComparison]::OrdinalIgnoreCase)){ Remove-Item -LiteralPath $resolved -Recurse -Force } } }"
  if errorlevel 1 (
    echo Failed to clean legacy Settings native UI resources.
    exit /b 1
  )
)

if exist "%~dp0..\resources\agent_plugin\Icons" (
  xcopy "%~dp0..\resources\agent_plugin\Icons" "%PLUGIN_DIR%\Icons\" /E /I /Y >nul
  if errorlevel 1 (
    echo Failed to copy Icon resources. Close ZW3D and run this script again.
    exit /b 1
  )
)

if exist "%~dp0..\resources\agent_plugin\forms" (
  xcopy "%~dp0..\resources\agent_plugin\forms" "%PLUGIN_DIR%\forms\" /E /I /Y >nul
  if errorlevel 1 (
    echo Failed to copy form resources. Close ZW3D and run this script again.
    exit /b 1
  )
)
if exist "%~dp0..\config\cidu_zw3d_config.json" (
  copy /Y "%~dp0..\config\cidu_zw3d_config.json" "%PLUGIN_DIR%\" >nul
  if errorlevel 1 (
    echo Failed to copy cidu_zw3d_config.json. Close ZW3D and run this script again.
    exit /b 1
  )
)
if exist "%~dp0..\build\Release\cidu_zw3d_config.json" (
  copy /Y "%~dp0..\build\Release\cidu_zw3d_config.json" "%PLUGIN_DIR%\" >nul
  if errorlevel 1 (
    echo Failed to copy cidu_zw3d_config.json. Close ZW3D and run this script again.
    exit /b 1
  )
)

if exist "%~dp0..\build\Release\WebView2Loader.dll" (
  copy /Y "%~dp0..\build\Release\WebView2Loader.dll" "%PLUGIN_DIR%\" >nul
  if errorlevel 1 (
    echo Failed to copy WebView2Loader.dll. Close ZW3D and run this script again.
    exit /b 1
  )
)

if exist "%~dp0..\build\Release\web" (
  if not exist "%PLUGIN_DIR%\web" mkdir "%PLUGIN_DIR%\web"
  xcopy /E /I /Y "%~dp0..\build\Release\web" "%PLUGIN_DIR%\web" >nul
  if errorlevel 1 (
    echo Failed to copy web resources. Close ZW3D and run this script again.
    exit /b 1
  )
)

if not exist "%PLUGIN_DIR%\ZW3DAgentPlugin.dll" (
  echo Missing ZW3DAgentPlugin.dll in %PLUGIN_DIR%.
  echo Run: cmake --build build --config Release
  exit /b 1
)

if not exist "%PLUGIN_DIR%\CiduParametricEngine.dll" (
  echo Missing CiduParametricEngine.dll in %PLUGIN_DIR%.
  echo Run: cmake --build build --config Release
  exit /b 1
)

if not defined ZW3D_YEAR (
  for /f "tokens=2,*" %%A in ('REG QUERY "HKLM\Software\ZWSOFT\ZW3D\CurrentVersion" /v LastInstallVersion 2^>nul ^| find "LastInstallVersion"') do set "ZW3D_YEAR=%%B"
)

if not defined ZW3D_YEAR (
  echo Could not detect installed ZW3D from HKLM\Software\ZWSOFT\ZW3D.
  exit /b 1
)

set "ZW3D_INSTALL_KEY=HKLM\Software\ZWSOFT\ZW3D\%ZW3D_YEAR%\ZW3D %ZW3D_YEAR%"

if not defined ZW3D_PRODUCT_VERSION (
  for /f "tokens=2,*" %%A in ('REG QUERY "%ZW3D_INSTALL_KEY%" /v ProductVersion 2^>nul ^| find "ProductVersion"') do set "ZW3D_PRODUCT_VERSION=%%B"
)

if not defined ZW3D_LANG (
  for /f "tokens=2,*" %%A in ('REG QUERY "%ZW3D_INSTALL_KEY%" /v CurrentLang 2^>nul ^| find "CurrentLang"') do set "ZW3D_LANG=%%B"
)

if not defined ZW3D_PRODUCT_VERSION (
  echo Could not detect ProductVersion from %ZW3D_INSTALL_KEY%.
  exit /b 1
)

if not defined ZW3D_LANG set "ZW3D_LANG=zh_CN"

set "REG_ROOT=HKCU\Software\ZWSOFT\ZW3D\%ZW3D_PRODUCT_VERSION%\%ZW3D_LANG%\Plugin_x64"

REG ADD "%REG_ROOT%" /f >nul
if errorlevel 1 (
  echo Failed to create ZW3D plugin registry root: %REG_ROOT%
  exit /b 1
)

REG ADD "%REG_ROOT%\ZW3DAgentPlugin" /v "Application" /t REG_SZ /d "ZW3DAgentPlugin.dll" /f
REG ADD "%REG_ROOT%\ZW3DAgentPlugin" /v "Description" /t REG_SZ /d "Cidu ZW3D agent plugin" /f
REG ADD "%REG_ROOT%\ZW3DAgentPlugin" /v "Load" /t REG_SZ /d "7" /f
REG ADD "%REG_ROOT%\ZW3DAgentPlugin" /v "Location" /t REG_SZ /d "%PLUGIN_DIR%" /f
REG ADD "%REG_ROOT%\ZW3DAgentPlugin" /v "Resource" /t REG_SZ /d "" /f

echo Registered Cidu ZW3D plugins for %ZW3D_PRODUCT_VERSION% %ZW3D_LANG%.
echo Registry root: %REG_ROOT%

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0merge_native_ui_profile.ps1" -Zw3dUserDir "%ZW3D_USER_DIR%"
if errorlevel 1 (
  echo Failed to merge native ZW3D UI profile entries.
  exit /b 1
)
