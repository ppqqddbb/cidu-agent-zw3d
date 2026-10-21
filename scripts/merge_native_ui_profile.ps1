param(
    [string]$Zw3dUserDir = "$env:APPDATA\ZWSOFT\ZW3D\ZW3D 2026"
)

$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "native_ui_merge_lib.ps1")

$profileRoot = Join-Path $Zw3dUserDir "custom\profiles"
$pluginDir = Join-Path $Zw3dUserDir "custom\apilibs"

Merge-NativeUiProfileRoot -ProfileRoot $profileRoot -PluginDir $pluginDir

Write-Host "Merged Cidu ZW3D native UI profile entries into $profileRoot"
