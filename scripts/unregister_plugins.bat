@echo off
setlocal EnableExtensions

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

REG DELETE "%REG_ROOT%\ZW3DAgentPlugin" /f >nul 2>&1

echo Unregistered Cidu ZW3D plugins for %ZW3D_PRODUCT_VERSION% %ZW3D_LANG%.
echo Registry root: %REG_ROOT%
