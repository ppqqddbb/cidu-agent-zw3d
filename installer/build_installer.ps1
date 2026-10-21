param(
    [string]$Configuration = "Release",
    [string]$OutputDir = (Join-Path (Split-Path $PSScriptRoot -Parent) "dist"),
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.IO.Compression.FileSystem

$repoRoot = Split-Path $PSScriptRoot -Parent
$buildDir = Join-Path $repoRoot "build\$Configuration"
$stageRoot = Join-Path $repoRoot ("build\installer-" + [System.Diagnostics.Process]::GetCurrentProcess().Id)
$payloadDir = Join-Path $stageRoot "payload"
$packageDir = Join-Path $stageRoot "package"
$payloadZip = Join-Path $packageDir "payload.zip"
$installerExe = Join-Path $OutputDir "CiduZW3DPluginInstaller.exe"
$scriptPackageZip = Join-Path $OutputDir "CiduZW3DPluginInstaller-script.zip"
$installerIcon = Join-Path $repoRoot "resources\agent_plugin\Icons\CiduInstaller.ico"

if (-not (Test-Path $installerIcon)) {
    throw "Missing installer icon: $installerIcon"
}

function Copy-TextFileWithUtf8Bom {
    param(
        [string]$Source,
        [string]$Destination
    )
    $text = [System.IO.File]::ReadAllText($Source)
    $utf8Bom = New-Object System.Text.UTF8Encoding($true)
    [System.IO.File]::WriteAllText($Destination, $text, $utf8Bom)
}

if (-not $SkipBuild) {
    cmake --build (Join-Path $repoRoot "build") --config $Configuration --target ZW3DAgentPlugin CiduParametricEngine
}

Remove-Item -LiteralPath $stageRoot -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $payloadDir | Out-Null
New-Item -ItemType Directory -Force -Path $packageDir | Out-Null
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

$agentDll = Join-Path $buildDir "ZW3DAgentPlugin.dll"
if (-not (Test-Path $agentDll)) {
    throw "Missing build output: $agentDll"
}
Copy-Item -LiteralPath $agentDll -Destination $payloadDir -Force

$agentZrc = Join-Path $buildDir "ZW3DAgentPlugin.zrc"
if (-not (Test-Path $agentZrc)) {
    throw "Missing build output: $agentZrc"
}
Copy-Item -LiteralPath $agentZrc -Destination $payloadDir -Force

$engineDll = Join-Path $buildDir "CiduParametricEngine.dll"
if (-not (Test-Path $engineDll)) {
    throw "Missing build output: $engineDll"
}
Copy-Item -LiteralPath $engineDll -Destination (Join-Path $payloadDir "CiduParametricEngine.dll") -Force

Copy-Item -LiteralPath (Join-Path $repoRoot "config\cidu_zw3d_config.json") -Destination $payloadDir -Force
Copy-TextFileWithUtf8Bom -Source (Join-Path $repoRoot "installer\uninstall.ps1") -Destination (Join-Path $payloadDir "uninstall.ps1")
Copy-Item -LiteralPath (Join-Path $repoRoot "resources\agent_plugin\forms") -Destination (Join-Path $payloadDir "forms") -Recurse -Force
Copy-Item -LiteralPath (Join-Path $repoRoot "resources\agent_plugin\Icons") -Destination (Join-Path $payloadDir "Icons") -Recurse -Force
New-Item -ItemType Directory -Force -Path (Join-Path $payloadDir "web") | Out-Null
Copy-Item -Path (Join-Path $repoRoot "resources\agent_plugin\web\*") -Destination (Join-Path $payloadDir "web") -Recurse -Force
Copy-Item -LiteralPath (Join-Path $repoRoot "resources\agent_plugin\Settings") -Destination (Join-Path $payloadDir "Settings") -Recurse -Force

$cscCandidates = @(
    (Join-Path $env:WINDIR "Microsoft.NET\Framework64\v4.0.30319\csc.exe"),
    (Join-Path $env:WINDIR "Microsoft.NET\Framework\v4.0.30319\csc.exe"),
    "C:\Program Files\Microsoft Visual Studio\18\Insiders\MSBuild\Current\Bin\Roslyn\csc.exe"
)
$csc = $cscCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $csc) {
    throw "C# compiler was not found."
}

$uninstaller = Join-Path $repoRoot "installer\uninstaller.cs"
$uninstallerExe = Join-Path $payloadDir "CiduZW3DUninstall.exe"
& $csc `
    /nologo `
    /target:winexe `
    /platform:anycpu `
    "/win32icon:$installerIcon" `
    /reference:System.Windows.Forms.dll `
    /reference:System.Drawing.dll `
    /out:$uninstallerExe `
    $uninstaller
if ($LASTEXITCODE -ne 0) {
    throw "C# uninstaller build failed. Exit code: $LASTEXITCODE"
}

$knowledgeSync = Join-Path $repoRoot "tools\jiavu_knowledge_sync\JiavuKnowledgeSync.cs"
$knowledgeSyncExe = Join-Path $payloadDir "JiavuKnowledgeSync.exe"
& $csc `
    /nologo `
    /target:exe `
    /platform:x64 `
    /reference:System.Web.Extensions.dll `
    /out:$knowledgeSyncExe `
    $knowledgeSync
if ($LASTEXITCODE -ne 0) {
    throw "C# Jiavu knowledge sync build failed. Exit code: $LASTEXITCODE"
}

if (Test-Path -LiteralPath $payloadZip) {
    Remove-Item -LiteralPath $payloadZip -Force
}
[System.IO.Compression.ZipFile]::CreateFromDirectory($payloadDir, $payloadZip)
Copy-TextFileWithUtf8Bom -Source (Join-Path $repoRoot "installer\install.ps1") -Destination (Join-Path $packageDir "install.ps1")

$bootstrapper = Join-Path $repoRoot "installer\bootstrapper.cs"
& $csc `
    /nologo `
    /target:winexe `
    /platform:anycpu `
    "/win32icon:$installerIcon" `
    /reference:System.Windows.Forms.dll `
    /reference:System.Drawing.dll `
    /out:$installerExe `
    /resource:"$($packageDir)\install.ps1,CiduZW3D.install.ps1" `
    /resource:"$payloadZip,CiduZW3D.payload.zip" `
    $bootstrapper
if ($LASTEXITCODE -ne 0) {
    throw "C# installer build failed. Exit code: $LASTEXITCODE"
}

if (Test-Path -LiteralPath $scriptPackageZip) {
    Remove-Item -LiteralPath $scriptPackageZip -Force
}
[System.IO.Compression.ZipFile]::CreateFromDirectory($packageDir, $scriptPackageZip)

Write-Host "Installer generated: $installerExe"
Write-Host "Script package generated: $scriptPackageZip"
