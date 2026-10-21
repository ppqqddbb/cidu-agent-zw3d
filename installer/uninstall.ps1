param(
    [string]$InstallDir = ""
)

$ErrorActionPreference = "Stop"

function Get-RegistryValue {
    param([string[]]$Paths, [string]$Name)
    foreach ($path in $Paths) {
        try {
            $item = Get-ItemProperty -Path $path -ErrorAction Stop
            if ($null -ne $item.$Name -and "$($item.$Name)".Length -gt 0) {
                return "$($item.$Name)"
            }
        } catch {}
    }
    return ""
}

function Get-Zw3dInstallYears {
    $roots = @(
        "Registry::HKEY_LOCAL_MACHINE\Software\ZWSOFT\ZW3D",
        "Registry::HKEY_LOCAL_MACHINE\Software\WOW6432Node\ZWSOFT\ZW3D"
    )
    $years = @()
    foreach ($root in $roots) {
        try {
            Get-ChildItem -Path $root -ErrorAction Stop | ForEach-Object {
                if ($_.PSChildName -match '^\d{4}$') {
                    $years += [int]$_.PSChildName
                }
            }
        } catch {}
    }
    return $years | Sort-Object -Descending -Unique
}

function Get-Zw3dYear {
    $currentPaths = @(
        "Registry::HKEY_LOCAL_MACHINE\Software\ZWSOFT\ZW3D\CurrentVersion",
        "Registry::HKEY_LOCAL_MACHINE\Software\WOW6432Node\ZWSOFT\ZW3D\CurrentVersion"
    )
    $year = Get-RegistryValue -Paths $currentPaths -Name "LastInstallVersion"
    if (-not $year) {
        $year = Get-Zw3dInstallYears | Select-Object -First 1
    }
    return $year
}

function Remove-LayoutStrategyInsert {
    param([string]$Path)
    if (-not (Test-Path -LiteralPath $Path)) { return }
    $bytes = [System.IO.File]::ReadAllBytes($Path)
    $hasBom = ($bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF)
    $text = [System.Text.Encoding]::UTF8.GetString($bytes)
    if ($hasBom -and $text.Length -gt 0 -and [int]$text[0] -eq 0xFEFF) {
        $text = $text.Substring(1)
    }
    if (-not $text.Contains('CiduZW3DAgentPage') -and -not $text.Contains('CiduZW3DPage') -and -not $text.Contains('CiduZW3DRibbonMenu')) {
        return
    }
    $pattern = '(?ms)[\t ]*<Insert\b(?=[^>]*\bname="(?:CiduZW3DAgentPage|CiduZW3DPage|CiduZW3DRibbonMenu[^"]*)")[^>]*(?:/>|>.*?</Insert>)\s*\r?\n?'
    $newText = [System.Text.RegularExpressions.Regex]::Replace($text, $pattern, '')
    if ($newText -eq $text) { return }
    $encoding = New-Object System.Text.UTF8Encoding($hasBom)
    [System.IO.File]::WriteAllText($Path, $newText, $encoding)
}

function Remove-UserResourceEntries {
    param([string]$Path, [string]$Marker, [string]$Pattern)
    if (-not (Test-Path -LiteralPath $Path)) { return }
    $bytes = [System.IO.File]::ReadAllBytes($Path)
    $hasBom = ($bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF)
    $text = [System.Text.Encoding]::UTF8.GetString($bytes)
    if ($hasBom -and $text.Length -gt 0 -and [int]$text[0] -eq 0xFEFF) {
        $text = $text.Substring(1)
    }
    if (-not $text.Contains($Marker)) { return }
    $newText = [System.Text.RegularExpressions.Regex]::Replace($text, $Pattern, '')
    if ($newText -eq $text) { return }
    $encoding = New-Object System.Text.UTF8Encoding($hasBom)
    [System.IO.File]::WriteAllText($Path, $newText, $encoding)
}

function Test-CiduProfileRoot {
    param([string]$ProfileRoot)
    if (-not (Test-Path -LiteralPath $ProfileRoot)) { return $false }

    $patterns = @("CiduZW3DAgentPage", "CiduZW3DPage", "ID_~CiduZW3DShowAgentForm", "~CiduZW3DShowAgentForm", "CiduZW3DAgentGroup")
    try {
        return [bool](Get-ChildItem -LiteralPath $ProfileRoot -Recurse -Filter "*.zcui" -File -ErrorAction SilentlyContinue |
            Select-String -Pattern $patterns -SimpleMatch -Quiet -ErrorAction SilentlyContinue)
    } catch {
        return $false
    }
}

function Add-UniquePath {
    param(
        [System.Collections.Generic.List[string]]$Paths,
        [hashtable]$Seen,
        [string]$Path,
        [switch]$RequireExisting
    )
    if (-not $Path) { return }
    if ($RequireExisting -and -not (Test-Path -LiteralPath $Path)) { return }

    try {
        $normalized = [System.IO.Path]::GetFullPath($Path).TrimEnd('\')
    } catch {
        return
    }
    $key = $normalized.ToLowerInvariant()
    if ($Seen.ContainsKey($key)) { return }

    $Seen[$key] = $true
    $Paths.Add($normalized) | Out-Null
}

function Get-ProfileRootFromZcuiPath {
    param([string]$Path)
    if (-not $Path) { return "" }

    $fullPath = [System.IO.Path]::GetFullPath($Path)
    $match = [System.Text.RegularExpressions.Regex]::Match($fullPath, '(?i)^(.+\\custom\\profiles)(?:\\|$)')
    if ($match.Success) {
        return $match.Groups[1].Value
    }
    return ""
}

function Remove-EmptyDirectory {
    param([string]$Path)
    try {
        if ((Test-Path -LiteralPath $Path) -and -not (Get-ChildItem -LiteralPath $Path -Force -ErrorAction SilentlyContinue)) {
            Remove-Item -LiteralPath $Path -Force -ErrorAction SilentlyContinue
        }
    } catch {}
}

function Remove-OwnedFiles {
    param([string]$Root, [string[]]$RelativePaths)
    foreach ($relativePath in $RelativePaths) {
        Remove-Item -LiteralPath (Join-Path $Root $relativePath) -Force -ErrorAction SilentlyContinue
    }
}

function Remove-OwnedFilesByPattern {
    param([string]$Root, [string]$Pattern)
    if (-not (Test-Path -LiteralPath $Root)) { return }
    Get-ChildItem -LiteralPath $Root -File -Filter $Pattern -ErrorAction SilentlyContinue | ForEach-Object {
        Remove-Item -LiteralPath $_.FullName -Force -ErrorAction SilentlyContinue
    }
}

function Clean-UserProfile {
    param([string]$ProfileRoot)
    if (-not (Test-Path -LiteralPath $ProfileRoot)) { return }
    if (-not (Test-CiduProfileRoot -ProfileRoot $ProfileRoot)) { return }

    Get-ChildItem -LiteralPath $ProfileRoot -Recurse -Filter "LayoutStrategy.zcui" -File -ErrorAction SilentlyContinue | ForEach-Object {
        Remove-LayoutStrategyInsert -Path $_.FullName
    }

    $resourcePool = Join-Path $ProfileRoot "ResourcePool"
    Remove-UserResourceEntries `
        -Path (Join-Path $resourcePool "Actions.zcui") `
        -Marker 'ID_~CiduZW3D' `
        -Pattern '(?ms)[\t ]*<Action\b(?=[^>]*\bname="ID_~CiduZW3D[^"]*")[^>]*(?:/>|>.*?</Action>)\s*\r?\n?'
    Remove-UserResourceEntries `
        -Path (Join-Path $resourcePool "RibbonPagesUser.zcui") `
        -Marker 'ID_~CiduZW3DShowAgentForm' `
        -Pattern '(?ms)[\t ]*<RibbonPage\b(?=[^>]*\bname="(?:CiduZW3DAgentPage|CiduZW3DPage)")[^>]*>.*?ID_~CiduZW3DShowAgentForm.*?</RibbonPage>\s*\r?\n?'
}

function Get-AllUserProfileRoots {
    param([string]$Year)

    $roots = New-Object 'System.Collections.Generic.List[string]'
    $seenRoots = @{}
    $zw3dRoots = New-Object 'System.Collections.Generic.List[string]'
    $seenZw3dRoots = @{}

    foreach ($base in @($env:APPDATA, $env:LOCALAPPDATA)) {
        if ($base) {
            Add-UniquePath -Paths $zw3dRoots -Seen $seenZw3dRoots -Path (Join-Path $base "ZWSOFT\ZW3D") -RequireExisting
        }
    }

    foreach ($drive in [System.IO.DriveInfo]::GetDrives()) {
        try {
            if ((-not $drive.IsReady) -or $drive.DriveType -ne [System.IO.DriveType]::Fixed) {
                continue
            }
        } catch {
            continue
        }

        $driveRoot = $drive.RootDirectory.FullName
        Add-UniquePath -Paths $zw3dRoots -Seen $seenZw3dRoots -Path (Join-Path $driveRoot "ZWSOFT\ZW3D") -RequireExisting
        Add-UniquePath -Paths $zw3dRoots -Seen $seenZw3dRoots -Path (Join-Path $driveRoot "ProgramData\ZWSOFT\ZW3D") -RequireExisting

        $usersRoot = Join-Path $driveRoot "Users"
        if (Test-Path -LiteralPath $usersRoot) {
            Get-ChildItem -LiteralPath $usersRoot -Directory -Force -ErrorAction SilentlyContinue | ForEach-Object {
                Add-UniquePath -Paths $zw3dRoots -Seen $seenZw3dRoots -Path (Join-Path $_.FullName "AppData\Roaming\ZWSOFT\ZW3D") -RequireExisting
                Add-UniquePath -Paths $zw3dRoots -Seen $seenZw3dRoots -Path (Join-Path $_.FullName "AppData\Local\ZWSOFT\ZW3D") -RequireExisting
            }
        }
    }

    $patterns = @("CiduZW3DAgentPage", "CiduZW3DPage", "ID_~CiduZW3DShowAgentForm", "~CiduZW3DShowAgentForm", "CiduZW3DAgentGroup")
    foreach ($zw3dRoot in $zw3dRoots) {
        Get-ChildItem -LiteralPath $zw3dRoot -Recurse -Filter "*.zcui" -File -ErrorAction SilentlyContinue | ForEach-Object {
            try {
                if (Select-String -LiteralPath $_.FullName -Pattern $patterns -SimpleMatch -Quiet -ErrorAction SilentlyContinue) {
                    Add-UniquePath -Paths $roots -Seen $seenRoots -Path (Get-ProfileRootFromZcuiPath -Path $_.FullName) -RequireExisting
                }
            } catch {
            }
        }
    }

    return $roots
}

$running = Get-Process -ErrorAction SilentlyContinue | Where-Object {
    $_.ProcessName -like "ZW3D*" -or $_.ProcessName -eq "zw3d"
}
if ($running) {
    throw "请先关闭 ZW3D，再运行卸载。"
}

$year = Get-Zw3dYear

$uninstallRegHklm = "HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\CiduZW3DPlugins"
$uninstallRegHkcu = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\CiduZW3DPlugins"

$installLocation = ""
if ($InstallDir) {
    $installLocation = [Environment]::ExpandEnvironmentVariables($InstallDir).TrimEnd('\','/')
}
if (-not $installLocation) {
    foreach ($reg in @($uninstallRegHklm, $uninstallRegHkcu)) {
        try {
            $entry = Get-ItemProperty -Path $reg -ErrorAction Stop
            if ($entry.InstallLocation) {
                $installLocation = "$($entry.InstallLocation)".TrimEnd('\','/')
                break
            }
        } catch {}
    }
}

$pluginDir = ""
if ($installLocation) {
    $candidateA = Join-Path $installLocation "apilibs"
    if (Test-Path -LiteralPath $candidateA) {
        $pluginDir = $candidateA
    } elseif (Test-Path -LiteralPath $installLocation) {
        $pluginDir = $installLocation
    }
}

if ($pluginDir) {
    Write-Host "插件目录: $pluginDir"

    # Keep cidu_zw3d_config.json so reinstall preserves machine/user settings.
    $rootFiles = @(
        "ZW3DAgentPlugin.dll",
        "ZW3DAgentPlugin.zrc",
        "CiduParametricEngine.dll",
        "WebView2Loader.dll",
        "JiavuKnowledgeSync.exe",
        "cidu_agent_debug.log",
        "cidu_ui_install_report.log",
        "CiduZW3DUninstall.exe"
    )
    Remove-OwnedFiles -Root $pluginDir -RelativePaths $rootFiles

    $iconFiles = @(
        "Icons\CiduAgent.png",
        "Icons\CiduAgent_source_cropped.png",
        "Icons\CiduAgent_source_from_clipboard.png",
        "Icons\CiduInstaller.ico",
        "Icons\CiduInstaller_source.png"
    )
    Remove-OwnedFiles -Root $pluginDir -RelativePaths $iconFiles

    Remove-OwnedFiles -Root $pluginDir -RelativePaths @("forms\CiduZW3DAgentForm.ui")

    $webFiles = @(
        "web\agent_panel.html",
        "web\agent_panel.css",
        "web\agent_panel_bootstrap.js",
        "web\agent_panel_chat.js",
        "web\agent_panel_events.js",
        "web\agent_panel_jiavu.js",
        "web\agent_panel_param_table.js",
        "web\agent_panel_equipment_mapping.js",
        "web\agent_panel_parameter_bridge.js",
        "web\agent_panel_parameters.js",
        "web\vendor\marked.umd.js"
    )
    Remove-OwnedFiles -Root $pluginDir -RelativePaths $webFiles

    Remove-OwnedFiles -Root $pluginDir -RelativePaths @(
        "CiduZW3D\bin\CiduParametricEngine.dll",
        "CiduZW3D\bin\WebView2Loader.dll",
        "CiduZW3D\bin\JiavuKnowledgeSync.exe"
    )

    Remove-OwnedFilesByPattern -Root (Join-Path $pluginDir "knowledge") -Pattern "jiavu_*"

    Remove-Item -LiteralPath (Join-Path $pluginDir "CiduZW3DInstaller\uninstall.ps1") -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath (Join-Path $pluginDir "CiduZW3DInstaller\payload.zip") -Force -ErrorAction SilentlyContinue

    foreach ($d in @("web\vendor", "web", "knowledge", "CiduZW3D\bin", "CiduZW3D", "CiduZW3DInstaller", "forms", "Icons")) {
        Remove-EmptyDirectory -Path (Join-Path $pluginDir $d)
    }
} else {
    Write-Warning "未找到插件目录（HKLM/HKCU Uninstall 注册表均无 InstallLocation）。跳过文件删除。"
}

foreach ($profileRoot in Get-AllUserProfileRoots -Year $year) {
    Write-Host "清理 Cidu 用户配置: $profileRoot"
    Clean-UserProfile -ProfileRoot $profileRoot
}

try {
    Get-ChildItem -Path "HKCU:\Software\ZWSOFT\ZW3D" -Recurse -ErrorAction Stop | ForEach-Object {
        if ($_.PSChildName -eq "Plugin_x64") {
            Remove-Item -Path (Join-Path $_.PSPath "ZW3DAgentPlugin") -Recurse -Force -ErrorAction SilentlyContinue
        }
    }
} catch {}

Remove-Item -Path $uninstallRegHklm -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item -Path $uninstallRegHkcu -Recurse -Force -ErrorAction SilentlyContinue

foreach ($programs in @(
    [Environment]::GetFolderPath("Programs"),
    (Join-Path $env:ProgramData "Microsoft\Windows\Start Menu\Programs")
)) {
    foreach ($folderName in @("Cidu ZW3D Agent", "Cidu ZW3D")) {
        $folder = Join-Path $programs $folderName
        if (Test-Path $folder) {
            Remove-Item -LiteralPath (Join-Path $folder "打开插件目录.lnk") -Force -ErrorAction SilentlyContinue
            Remove-Item -LiteralPath (Join-Path $folder "卸载 Cidu ZW3D 插件.lnk") -Force -ErrorAction SilentlyContinue
            Remove-EmptyDirectory -Path $folder
        }
    }
}

Write-Host "Cidu ZW3D Plugins 已卸载。"
