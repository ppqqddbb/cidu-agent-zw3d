param(
    [string]$PayloadZip = (Join-Path $PSScriptRoot "payload.zip"),
    [string]$TargetUserAppData = "",
    [string]$ShortcutFolder = "Cidu ZW3D",
    [switch]$ShortcutsForAllUsers,
    [string]$LogFile = ""
)

$ErrorActionPreference = "Stop"

if ($LogFile) {
    try {
        $logDir = Split-Path $LogFile -Parent
        if ($logDir) { New-Item -ItemType Directory -Force -Path $logDir | Out-Null }
        $OutputEncoding = New-Object System.Text.UTF8Encoding($false)
        [Console]::OutputEncoding = New-Object System.Text.UTF8Encoding($false)
        Start-Transcript -Path $LogFile -Force | Out-Null
    } catch {
    }
}

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

function Get-Zw3dInfo {
    $currentPaths = @(
        "Registry::HKEY_LOCAL_MACHINE\Software\ZWSOFT\ZW3D\CurrentVersion",
        "Registry::HKEY_LOCAL_MACHINE\Software\WOW6432Node\ZWSOFT\ZW3D\CurrentVersion"
    )
    $year = Get-RegistryValue -Paths $currentPaths -Name "LastInstallVersion"
    if (-not $year) {
        $year = Get-Zw3dInstallYears | Select-Object -First 1
    }
    if (-not $year) {
        throw "未找到 ZW3D 安装信息：HKLM\Software\ZWSOFT\ZW3D。请先安装 ZW3D。"
    }

    $installPaths = @(
        "Registry::HKEY_LOCAL_MACHINE\Software\ZWSOFT\ZW3D\$year\ZW3D $year",
        "Registry::HKEY_LOCAL_MACHINE\Software\WOW6432Node\ZWSOFT\ZW3D\$year\ZW3D $year"
    )
    $productVersion = Get-RegistryValue -Paths $installPaths -Name "ProductVersion"
    $lang = Get-RegistryValue -Paths $installPaths -Name "CurrentLang"
    $installLocation = Get-RegistryValue -Paths $installPaths -Name "InstallLocation"
    if (-not $productVersion) {
        throw "未找到 ZW3D ProductVersion：HKLM\...\ZW3D $year"
    }
    if (-not $installLocation) {
        throw "未找到 ZW3D 安装位置 InstallLocation：HKLM\...\ZW3D $year"
    }
    if (-not $lang) { $lang = "zh_CN" }

    [pscustomobject]@{
        Year = $year
        ProductVersion = $productVersion
        Lang = $lang
        InstallLocation = $installLocation.TrimEnd('\','/')
    }
}

function Assert-Zw3dClosed {
    $running = Get-Process -ErrorAction SilentlyContinue | Where-Object {
        $_.ProcessName -like "ZW3D*" -or $_.ProcessName -eq "zw3d"
    }
    if ($running) {
        throw "请先关闭 ZW3D，再运行安装器。"
    }
}

function Get-WebView2RuntimeVersion {
    $paths = @(
        "HKCU:\Software\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}",
        "HKLM:\Software\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}",
        "HKLM:\Software\WOW6432Node\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}"
    )
    foreach ($path in $paths) {
        try {
            $value = (Get-ItemProperty -Path $path -ErrorAction Stop).pv
            if ($value -and "$value" -ne "0.0.0.0") {
                return "$value"
            }
        } catch {
        }
    }
    return ""
}

function Assert-WebView2RuntimeInstalled {
    $version = Get-WebView2RuntimeVersion
    if ($version) {
        Write-Host "WebView2 Runtime: $version"
        return
    }
    throw "未检测到 Microsoft Edge WebView2 Runtime。Cidu Agent 窗口依赖 WebView2，请先安装 Microsoft Edge WebView2 Runtime 后重新运行安装器。"
}

function Resolve-TargetUserAppData {
    param([string]$Provided)
    if ($Provided) {
        return [Environment]::ExpandEnvironmentVariables($Provided).TrimEnd('\','/')
    }
    return $env:APPDATA.TrimEnd('\','/')
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

function Merge-LayoutStrategy {
    param([string]$Path, [string]$Environment, [string]$Layout, [string]$Role = "4-Expert")
    New-Item -ItemType Directory -Force -Path (Split-Path $Path) | Out-Null

    $insertLine = '<Insert type="RibbonPage" name="CiduZW3DAgentPage" topCollection="' + $Layout + '" leftSibling="#InsertAtLast" posValue="1073741823" />'
    $insertPattern = '(?ms)[\t ]*<Insert\b(?=[^>]*\bname="CiduZW3DAgentPage")[^>]*(?:/>|>.*?</Insert>)\s*\r?\n?'

    if (-not (Test-Path -LiteralPath $Path)) {
        $template = "<?xml version=`"1.0`" encoding=`"UTF-8`"?>`r`n<Strategy profileType=`"UIProfile`" environment=`"$Environment`" role=`"$Role`">`r`n`t<UserCustomizations>`r`n`t`t$insertLine`r`n`t</UserCustomizations>`r`n</Strategy>`r`n"
        $utf8Bom = New-Object System.Text.UTF8Encoding($true)
        [System.IO.File]::WriteAllText($Path, $template, $utf8Bom)
        return
    }

    $bytes = [System.IO.File]::ReadAllBytes($Path)
    $hasBom = ($bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF)
    $text = [System.Text.Encoding]::UTF8.GetString($bytes)
    if ($hasBom -and $text.Length -gt 0 -and [int]$text[0] -eq 0xFEFF) {
        $text = $text.Substring(1)
    }

    $originalText = $text
    if ($text.Contains('name="CiduZW3DAgentPage"')) {
        $text = [System.Text.RegularExpressions.Regex]::Replace($text, $insertPattern, '')
    }

    $stamp = Get-Date -Format "yyyyMMddHHmmss"
    Copy-Item -LiteralPath $Path -Destination "$Path.bak.$stamp" -Force

    $newText = $null
    if ($text.Contains('</UserCustomizations>')) {
        $newText = $text.Replace('</UserCustomizations>', "`t`t$insertLine`r`n`t</UserCustomizations>")
    } elseif ($text.Contains('<UserCustomizations />')) {
        $newText = $text.Replace('<UserCustomizations />', "<UserCustomizations>`r`n`t`t$insertLine`r`n`t</UserCustomizations>")
    } elseif ($text.Contains('<UserCustomizations/>')) {
        $newText = $text.Replace('<UserCustomizations/>', "<UserCustomizations>`r`n`t`t$insertLine`r`n`t</UserCustomizations>")
    } elseif ($text.Contains('</Strategy>')) {
        $newText = $text.Replace('</Strategy>', "`t<UserCustomizations>`r`n`t`t$insertLine`r`n`t</UserCustomizations>`r`n</Strategy>")
    } else {
        Write-Warning "$Path 内未找到 </UserCustomizations> 或 </Strategy>，跳过注入。"
        return
    }

    if ($newText -eq $originalText) {
        return
    }

    $encoding = New-Object System.Text.UTF8Encoding($hasBom)
    [System.IO.File]::WriteAllText($Path, $newText, $encoding)
}

function Remove-LayoutStrategyInsertByName {
    param([string]$Path, [string]$Name)
    if (-not (Test-Path -LiteralPath $Path)) { return }

    $bytes = [System.IO.File]::ReadAllBytes($Path)
    $hasBom = ($bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF)
    $text = [System.Text.Encoding]::UTF8.GetString($bytes)
    if ($hasBom -and $text.Length -gt 0 -and [int]$text[0] -eq 0xFEFF) {
        $text = $text.Substring(1)
    }
    if (-not $text.Contains("name=`"$Name`"")) { return }

    $pattern = '(?ms)[\t ]*<Insert\b(?=[^>]*\bname="' + [System.Text.RegularExpressions.Regex]::Escape($Name) + '")[^>]*(?:/>|>.*?</Insert>)\s*\r?\n?'
    $newText = [System.Text.RegularExpressions.Regex]::Replace($text, $pattern, '')
    if ($newText -eq $text) { return }

    $stamp = Get-Date -Format "yyyyMMddHHmmss"
    Copy-Item -LiteralPath $Path -Destination "$Path.bak.$stamp" -Force

    $encoding = New-Object System.Text.UTF8Encoding($hasBom)
    [System.IO.File]::WriteAllText($Path, $newText, $encoding)
}

function Remove-UserResourceBlock {
    param(
        [string]$Path,
        [string]$Marker,
        [string]$Pattern
    )
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

    $stamp = Get-Date -Format "yyyyMMddHHmmss"
    Copy-Item -LiteralPath $Path -Destination "$Path.bak.$stamp" -Force

    $encoding = New-Object System.Text.UTF8Encoding($hasBom)
    [System.IO.File]::WriteAllText($Path, $newText, $encoding)
}

function Merge-UserResource {
    param(
        [string]$Path,
        [string]$RootOpenTag,
        [string]$RootCloseTag,
        [string]$Marker,
        [string]$InsertBlock,
        [string]$ExistingBlockPattern = ""
    )
    New-Item -ItemType Directory -Force -Path (Split-Path $Path) | Out-Null

    if (-not (Test-Path -LiteralPath $Path)) {
        $template = "<?xml version=`"1.0`" encoding=`"UTF-8`"?>`r`n$RootOpenTag`r`n$InsertBlock`r`n$RootCloseTag`r`n"
        $utf8Bom = New-Object System.Text.UTF8Encoding($true)
        [System.IO.File]::WriteAllText($Path, $template, $utf8Bom)
        return
    }

    $bytes = [System.IO.File]::ReadAllBytes($Path)
    $hasBom = ($bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF)
    $text = [System.Text.Encoding]::UTF8.GetString($bytes)
    if ($hasBom -and $text.Length -gt 0 -and [int]$text[0] -eq 0xFEFF) {
        $text = $text.Substring(1)
    }

    $originalText = $text
    if ($ExistingBlockPattern -and $text.Contains($Marker)) {
        $text = [System.Text.RegularExpressions.Regex]::Replace($text, $ExistingBlockPattern, '')
    }

    $stamp = Get-Date -Format "yyyyMMddHHmmss"
    Copy-Item -LiteralPath $Path -Destination "$Path.bak.$stamp" -Force

    $newText = $null
    if ($text.Contains($RootCloseTag)) {
        $newText = $text.Replace($RootCloseTag, "$InsertBlock`r`n$RootCloseTag")
    } else {
        Write-Warning "$Path 内未找到 $RootCloseTag，跳过注入。"
        return
    }

    if ($newText -eq $originalText) {
        return
    }

    $encoding = New-Object System.Text.UTF8Encoding($hasBom)
    [System.IO.File]::WriteAllText($Path, $newText, $encoding)
}

function Merge-ActionResource {
    param(
        [string]$Path,
        [string]$InsertBlock,
        [string]$Marker,
        [string]$ExistingBlockPattern = ""
    )
    New-Item -ItemType Directory -Force -Path (Split-Path $Path) | Out-Null

    if (-not (Test-Path -LiteralPath $Path)) {
        $template = "<?xml version=`"1.0`" encoding=`"UTF-8`"?>`r`n<Actions system=`"false`">`r`n`t<Action#List>`r`n$InsertBlock`r`n`t</Action#List>`r`n</Actions>`r`n"
        $utf8Bom = New-Object System.Text.UTF8Encoding($true)
        [System.IO.File]::WriteAllText($Path, $template, $utf8Bom)
        return
    }

    $bytes = [System.IO.File]::ReadAllBytes($Path)
    $hasBom = ($bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF)
    $text = [System.Text.Encoding]::UTF8.GetString($bytes)
    if ($hasBom -and $text.Length -gt 0 -and [int]$text[0] -eq 0xFEFF) {
        $text = $text.Substring(1)
    }

    $originalText = $text
    if ($ExistingBlockPattern -and $text.Contains($Marker)) {
        $text = [System.Text.RegularExpressions.Regex]::Replace($text, $ExistingBlockPattern, '')
    }

    $stamp = Get-Date -Format "yyyyMMddHHmmss"
    Copy-Item -LiteralPath $Path -Destination "$Path.bak.$stamp" -Force

    $newText = $null
    # 优先插入到第一个 </Action#List> 之前（ZW3D 能识别）
    if ($text.Contains('</Action#List>')) {
        $pattern = '</Action#List>'
        $index = $text.IndexOf($pattern)
        $newText = $text.Substring(0, $index) + $InsertBlock + "`r`n`t" + $text.Substring($index)
    } elseif ($text.Contains('</Actions>')) {
        # 如果没有 Action#List，创建一个
        $insertBlock = "`t<Action#List>`r`n$InsertBlock`r`n`t</Action#List>`r`n"
        $newText = $text.Replace('</Actions>', "$insertBlock</Actions>")
    } else {
        Write-Warning "$Path 内未找到 </Action#List> 或 </Actions>，跳过注入。"
        return
    }

    if ($newText -eq $originalText) {
        return
    }

    $encoding = New-Object System.Text.UTF8Encoding($hasBom)
    [System.IO.File]::WriteAllText($Path, $newText, $encoding)
}

function Get-CiduActionInsertBlock {
    param([string]$PluginDir)
    $iconPath = Join-Path $PluginDir "Icons\CiduAgent.png"
    return @"
		<Action name="ID_CiduZW3DShowAgentForm" type="button">
			<Form>CiduZW3DAgentForm</Form>
			<Text>
				<Ribbon>Cidu Agent</Ribbon>
				<Menu>Cidu Agent</Menu>
			</Text>
			<Icon>$iconPath</Icon>
			<Hint>Open Cidu Agent.</Hint>
			<Help />
			<Desc />
			<Script>~CiduZW3DShowAgentForm</Script>
		</Action>
"@
}

function Get-CiduRibbonPageInsertBlock {
    return @"
	<RibbonPage name="CiduZW3DAgentPage" text="Cidu Agent" visible="true">
		<RibbonGroup name="CiduZW3DAgentGroup" text="Agent" visible="true">
			<GroupPanel name="CiduZW3DAgentGroupPanel">
				<Control action="ID_CiduZW3DShowAgentForm" visible="true" buttonStyle="3" />
			</GroupPanel>
			<GroupMenu name="CiduZW3DAgentGroupMenu" visible="true" />
		</RibbonGroup>
	</RibbonPage>
"@
}

function Get-Zw3dUiEnvironments {
    return @(
        [pscustomobject]@{ Directory = "Environment-0-Top"; Environment = "0"; Layout = "Layout_0_Top" },
        [pscustomobject]@{ Directory = "Environment-1-Root"; Environment = "1"; Layout = "Layout_1_Root" },
        [pscustomobject]@{ Directory = "Environment-2-Z3"; Environment = "2"; Layout = "Layout_2_Z3" },
        [pscustomobject]@{ Directory = "Environment-3-Sketch"; Environment = "3"; Layout = "Layout_3_Sketch" },
        [pscustomobject]@{ Directory = "Environment-4-Sheet"; Environment = "4"; Layout = "Layout_4_Sheet" },
        [pscustomobject]@{ Directory = "Environment-5-Packet"; Environment = "5"; Layout = "Layout_5_Packet" },
        [pscustomobject]@{ Directory = "Environment-6-Cam"; Environment = "6"; Layout = "Layout_6_CAM" },
        [pscustomobject]@{ Directory = "Environment-7-Animation"; Environment = "7"; Layout = "Layout_7_Animation" },
        [pscustomobject]@{ Directory = "Environment-8-ExplodedView"; Environment = "8"; Layout = "Layout_8_ExpView" },
        [pscustomobject]@{ Directory = "Environment-9-3DSketch"; Environment = "9"; Layout = "Layout_9_Sketch3D" },
        [pscustomobject]@{ Directory = "Environment-10-Part"; Environment = "10"; Layout = "Layout_10_Part" },
        [pscustomobject]@{ Directory = "Environment-11-ECAD Assembly"; Environment = "11"; Layout = "Layout_11_EcadAsm" },
        [pscustomobject]@{ Directory = "Environment-12-ECAD Part"; Environment = "12"; Layout = "Layout_12_EcadPart" },
        [pscustomobject]@{ Directory = "Environment-13-Assembly"; Environment = "13"; Layout = "Layout_13_Asm" },
        [pscustomobject]@{ Directory = "Environment-17-Master Layout"; Environment = "17"; Layout = "Layout_17_MasLayPrt" },
        [pscustomobject]@{ Directory = "Environment-19-Cam"; Environment = "19"; Layout = "Layout_19_CAM2" }
    )
}

function Get-Zw3dUiRoles {
    return @("1-Primary", "2-Intermediate_default", "3-Advanced", "4-Expert")
}

function Copy-Payload {
    param([string]$PayloadDir, [string]$PluginDir)
    New-Item -ItemType Directory -Force -Path $PluginDir | Out-Null
    $supportBin = Join-Path $PluginDir "CiduZW3D\bin"
    New-Item -ItemType Directory -Force -Path $supportBin | Out-Null

    $requiredFiles = @(
        "ZW3DAgentPlugin.dll",
        "ZW3DAgentPlugin.zrc"
    )
    foreach ($file in $requiredFiles) {
        $source = Join-Path $PayloadDir $file
        if (-not (Test-Path $source)) {
            throw "安装包缺少文件：$file"
        }
        Copy-Item -LiteralPath $source -Destination (Join-Path $PluginDir $file) -Force
    }

    foreach ($file in @("CiduParametricEngine.dll", "JiavuKnowledgeSync.exe")) {
        $source = Join-Path $PayloadDir $file
        if (-not (Test-Path $source)) {
            throw "安装包缺少文件：$file"
        }
        Copy-Item -LiteralPath $source -Destination (Join-Path $supportBin $file) -Force
    }

    $configSource = Join-Path $PayloadDir "cidu_zw3d_config.json"
    if (-not (Test-Path $configSource)) {
        throw "安装包缺少文件：cidu_zw3d_config.json"
    }
    $configDest = Join-Path $PluginDir "cidu_zw3d_config.json"
    if (-not (Test-Path $configDest)) {
        Copy-Item -LiteralPath $configSource -Destination $configDest -Force
    } else {
        Merge-CiduConfigDefaults -ConfigPath $configDest -DefaultsPath $configSource
    }
    Ensure-CiduMachineUser -ConfigPath $configDest

    # 把默认值也合并进 Roaming / Local / Profile 路径下的旧配置；
    # 插件读取时优先从这些用户路径读，所以旧空配置会盖掉 PluginDir 里的默认值。
    foreach ($userConfig in @(
        (Join-Path $env:APPDATA "CiduZW3D\cidu_zw3d_config.json"),
        (Join-Path $env:LOCALAPPDATA "CiduZW3D\cidu_zw3d_config.json"),
        (Join-Path $env:USERPROFILE ".CiduZW3D\cidu_zw3d_config.json")
    )) {
        if ($userConfig -and (Test-Path -LiteralPath $userConfig)) {
            Merge-CiduConfigDefaults -ConfigPath $userConfig -DefaultsPath $configSource
        }
    }

    $uninstallerExe = Join-Path $PayloadDir "CiduZW3DUninstall.exe"
    if (Test-Path $uninstallerExe) {
        Copy-Item -LiteralPath $uninstallerExe -Destination (Join-Path $PluginDir "CiduZW3DUninstall.exe") -Force
    }

    foreach ($dir in @("forms", "Icons", "web")) {
        $source = Join-Path $PayloadDir $dir
        if (Test-Path $source) {
            $destination = Join-Path $PluginDir $dir
            New-Item -ItemType Directory -Force -Path $destination | Out-Null
            Copy-Item -Path (Join-Path $source "*") -Destination $destination -Recurse -Force
        }
    }

    $installerDir = Join-Path $PluginDir "CiduZW3DInstaller"
    New-Item -ItemType Directory -Force -Path $installerDir | Out-Null
    $uninstallScript = Join-Path $PayloadDir "uninstall.ps1"
    if (Test-Path $uninstallScript) {
        Copy-Item -LiteralPath $uninstallScript -Destination (Join-Path $installerDir "uninstall.ps1") -Force
    }

    foreach ($legacyRootFile in @("CiduParametricEngine.dll", "WebView2Loader.dll", "JiavuKnowledgeSync.exe")) {
        Remove-LegacyRootPluginFile -PluginDir $PluginDir -FileName $legacyRootFile
    }
}

function Clear-LegacySettingsRibbon {
    param([string]$PluginDir)
    $settings = Join-Path $PluginDir "Settings\Default"
    if (-not (Test-Path $settings)) { return }

    $markers = @(
        "CiduZW3DAgentPage",
        "CiduZW3DPage",
        "ID_CiduZW3DShowAgentForm",
        "CiduZW3DShowAgentForm",
        "CiduZW3DAgentGroup",
        "CiduZW3DRibbonMenu"
    )
    Get-ChildItem -LiteralPath $settings -Recurse -File -Filter "*.zcui" -ErrorAction SilentlyContinue | ForEach-Object {
        try {
            if (Select-String -LiteralPath $_.FullName -Pattern $markers -SimpleMatch -Quiet -ErrorAction SilentlyContinue) {
                Remove-Item -LiteralPath $_.FullName -Force -ErrorAction SilentlyContinue
            }
        } catch {
        }
    }

    foreach ($dir in @(
        "Strategy\CiduZW3D",
        "Strategy\Environment-0-Top",
        "Strategy\Environment-1-Root",
        "Strategy\Environment-2-Z3",
        "Environment-2\Controls",
        "Environment-2",
        "Action",
        "ResourcePool",
        "Strategy"
    )) {
        Remove-EmptyDirectory -Path (Join-Path $settings $dir)
    }
    Remove-EmptyDirectory -Path $settings
}

function Clear-LegacyUserApilibs {
    param([string]$UserDir)
    $legacy = Join-Path $UserDir "custom\apilibs"
    if (-not (Test-Path $legacy)) { return }

    $files = @(
        "ZW3DAgentPlugin.dll",
        "ZW3DAgentPlugin.zrc",
        "CiduParametricEngine.dll",
        "WebView2Loader.dll",
        "JiavuKnowledgeSync.exe",
        "CiduZW3DUninstall.exe",
        "cidu_agent_debug.log"
    )
    foreach ($f in $files) {
        Remove-Item -LiteralPath (Join-Path $legacy $f) -Force -ErrorAction SilentlyContinue
    }
    Get-ChildItem -LiteralPath $legacy -Filter "*.bak-*" -File -ErrorAction SilentlyContinue | ForEach-Object {
        Remove-Item -LiteralPath $_.FullName -Force -ErrorAction SilentlyContinue
    }
    Get-ChildItem -LiteralPath $legacy -Filter "*.bak.*" -File -ErrorAction SilentlyContinue | ForEach-Object {
        Remove-Item -LiteralPath $_.FullName -Force -ErrorAction SilentlyContinue
    }

    Remove-OwnedFiles -Root $legacy -RelativePaths @(
        "forms\CiduZW3DAgentForm.ui",
        "Icons\CiduAgent.png",
        "Icons\CiduAgent_source_cropped.png",
        "Icons\CiduAgent_source_from_clipboard.png",
        "Icons\CiduInstaller.ico",
        "Icons\CiduInstaller_source.png",
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
        "web\vendor\marked.umd.js",
        "CiduZW3DInstaller\uninstall.ps1",
        "CiduZW3DInstaller\payload.zip"
    )

    foreach ($d in @("web\vendor", "web", "CiduZW3DInstaller", "forms", "Icons")) {
        Remove-EmptyDirectory -Path (Join-Path $legacy $d)
    }
}

function Remove-OwnedFilesByPattern {
    param(
        [string]$Root,
        [string]$Pattern
    )
    if (-not (Test-Path -LiteralPath $Root)) { return }

    Get-ChildItem -LiteralPath $Root -File -Filter $Pattern -ErrorAction SilentlyContinue | ForEach-Object {
        Remove-Item -LiteralPath $_.FullName -Force -ErrorAction SilentlyContinue
    }
}

function Remove-LegacyRootPluginFile {
    param(
        [string]$PluginDir,
        [string]$FileName
    )
    $path = Join-Path $PluginDir $FileName
    if (-not (Test-Path -LiteralPath $path)) { return }

    try {
        Remove-Item -LiteralPath $path -Force -ErrorAction Stop
    } catch {
        throw "旧版 Cidu 文件无法删除：$path。请先关闭 ZW3D 后重新安装。错误：$($_.Exception.Message)"
    }
    if (Test-Path -LiteralPath $path) {
        throw "旧版 Cidu 文件仍然存在：$path。请先关闭 ZW3D 后重新安装。"
    }
}

function Clear-PreviousCiduInstall {
    param([string]$PluginDir)
    if (-not (Test-Path -LiteralPath $PluginDir)) { return }

    # Keep cidu_zw3d_config.json: it stores this machine's user id and endpoint settings.
    $rootFiles = @(
        "ZW3DAgentPlugin.dll",
        "ZW3DAgentPlugin.zrc",
        "CiduParametricEngine.dll",
        "WebView2Loader.dll",
        "JiavuKnowledgeSync.exe",
        "CiduZW3DUninstall.exe",
        "cidu_agent_debug.log",
        "cidu_ui_install_report.log"
    )
    foreach ($file in $rootFiles) {
        Remove-LegacyRootPluginFile -PluginDir $PluginDir -FileName $file
    }

    Remove-OwnedFiles -Root $PluginDir -RelativePaths @(
        "forms\CiduZW3DAgentForm.ui",
        "Icons\CiduAgent.png",
        "Icons\CiduAgent_source_cropped.png",
        "Icons\CiduAgent_source_from_clipboard.png",
        "Icons\CiduInstaller.ico",
        "Icons\CiduInstaller_source.png",
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
        "web\vendor\marked.umd.js",
        "CiduZW3DInstaller\uninstall.ps1",
        "CiduZW3DInstaller\payload.zip",
        "CiduZW3D\bin\CiduParametricEngine.dll",
        "CiduZW3D\bin\WebView2Loader.dll",
        "CiduZW3D\bin\JiavuKnowledgeSync.exe"
    )

    Remove-OwnedFilesByPattern -Root (Join-Path $PluginDir "knowledge") -Pattern "jiavu_*"

    foreach ($d in @(
        "web\vendor",
        "web",
        "knowledge",
        "CiduZW3D\bin",
        "CiduZW3D",
        "CiduZW3DInstaller",
        "forms",
        "Icons"
    )) {
        Remove-EmptyDirectory -Path (Join-Path $PluginDir $d)
    }
}

function Clear-CiduRegistryInstallRecords {
    foreach ($uninstallKey in @(
        "HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\CiduZW3DAgent",
        "HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\CiduZW3DAgent",
        "HKLM:\Software\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\CiduZW3DAgent"
    )) {
        Remove-Item -Path $uninstallKey -Recurse -Force -ErrorAction SilentlyContinue
    }
}

function Clear-CiduZw3dPluginRegistry {
    foreach ($root in @("HKCU:\Software\ZWSOFT\ZW3D", "HKLM:\Software\ZWSOFT\ZW3D")) {
        if (-not (Test-Path -Path $root)) { continue }
        try {
            Get-ChildItem -Path $root -Recurse -ErrorAction Stop | ForEach-Object {
                if ($_.PSChildName -eq "Plugin_x64") {
                    Remove-Item -Path (Join-Path $_.PSPath "ZW3DAgentPlugin") -Recurse -Force -ErrorAction SilentlyContinue
                    Remove-Item -Path (Join-Path $_.PSPath "CiduZW3D") -Recurse -Force -ErrorAction SilentlyContinue
                    Remove-Item -Path (Join-Path $_.PSPath "CiduZW3DAgent") -Recurse -Force -ErrorAction SilentlyContinue
                }
            }
        } catch {
        }
    }
}

function Merge-CiduConfigDefaults {
    param([string]$ConfigPath, [string]$DefaultsPath)
    if (-not (Test-Path -LiteralPath $DefaultsPath)) { return }
    if (-not (Test-Path -LiteralPath $ConfigPath)) { return }

    try {
        $defaultsRaw = Get-Content -LiteralPath $DefaultsPath -Raw -Encoding UTF8
        $defaults = $defaultsRaw | ConvertFrom-Json
        $existingRaw = Get-Content -LiteralPath $ConfigPath -Raw -Encoding UTF8
        $existing = $existingRaw | ConvertFrom-Json

        $changed = $false
        foreach ($prop in $defaults.PSObject.Properties) {
            $name = $prop.Name
            $defValue = "$($prop.Value)"
            if ([string]::IsNullOrEmpty($defValue)) { continue }
            $currentValue = ""
            if ($existing.PSObject.Properties.Name -contains $name) {
                $currentValue = "$($existing.$name)"
            }
            if ([string]::IsNullOrEmpty($currentValue)) {
                $existing | Add-Member -NotePropertyName $name -NotePropertyValue $defValue -Force
                $changed = $true
            }
        }

        if ($changed) {
            $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
            [System.IO.File]::WriteAllText($ConfigPath, ($existing | ConvertTo-Json -Depth 8), $utf8NoBom)
        }
    } catch {
        Write-Warning "无法合并默认配置：$ConfigPath；错误：$($_.Exception.Message)"
    }
}

function Ensure-CiduMachineUser {
    param([string]$ConfigPath)
    if (-not (Test-Path -LiteralPath $ConfigPath)) { return }
    try {
        $raw = Get-Content -LiteralPath $ConfigPath -Raw -Encoding UTF8
        $json = $raw | ConvertFrom-Json
        $current = ""
        if ($json.PSObject.Properties.Name -contains "user") {
            $current = "$($json.user)"
        }
        if ($current -and $current -ne "zw3d-user") {
            return
        }
        $json | Add-Member -NotePropertyName "user" -NotePropertyValue ("cidu-zw3d:" + [guid]::NewGuid().ToString()) -Force
        $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
        [System.IO.File]::WriteAllText($ConfigPath, ($json | ConvertTo-Json -Depth 8), $utf8NoBom)
    } catch {
        Write-Warning "无法写入 Cidu 用户标识：$ConfigPath"
    }
}

function Add-UniqueProfileRoot {
    param(
        [System.Collections.Generic.List[string]]$Roots,
        [hashtable]$Seen,
        [string]$ProfileRoot
    )
    if (-not $ProfileRoot) { return }

    $normalized = [System.IO.Path]::GetFullPath($ProfileRoot).TrimEnd('\')
    $key = $normalized.ToLowerInvariant()
    if ($Seen.ContainsKey($key)) { return }

    $Seen[$key] = $true
    $Roots.Add($normalized) | Out-Null
}

function Add-UniqueExistingPath {
    param(
        [System.Collections.Generic.List[string]]$Paths,
        [hashtable]$Seen,
        [string]$Path
    )
    if (-not $Path) { return }
    if (-not (Test-Path -LiteralPath $Path)) { return }

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

function Get-Zw3dRootCandidates {
    param([string]$UserAppData, [string]$Year)

    $paths = New-Object 'System.Collections.Generic.List[string]'
    $seen = @{}

    foreach ($base in @($UserAppData, $env:APPDATA, $env:LOCALAPPDATA)) {
        if ($base) {
            Add-UniqueExistingPath -Paths $paths -Seen $seen -Path (Join-Path $base "ZWSOFT\ZW3D")
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
        Add-UniqueExistingPath -Paths $paths -Seen $seen -Path (Join-Path $driveRoot "ZWSOFT\ZW3D")
        Add-UniqueExistingPath -Paths $paths -Seen $seen -Path (Join-Path $driveRoot "ProgramData\ZWSOFT\ZW3D")

        $usersRoot = Join-Path $driveRoot "Users"
        if (Test-Path -LiteralPath $usersRoot) {
            Get-ChildItem -LiteralPath $usersRoot -Directory -ErrorAction SilentlyContinue | ForEach-Object {
                Add-UniqueExistingPath -Paths $paths -Seen $seen -Path (Join-Path $_.FullName "AppData\Roaming\ZWSOFT\ZW3D")
                Add-UniqueExistingPath -Paths $paths -Seen $seen -Path (Join-Path $_.FullName "AppData\Local\ZWSOFT\ZW3D")
            }
        }
    }

    return $paths
}

function Add-CiduMarkedProfileRootsFromZw3dRoot {
    param(
        [System.Collections.Generic.List[string]]$Roots,
        [hashtable]$Seen,
        [string]$Zw3dRoot
    )
    if (-not (Test-Path -LiteralPath $Zw3dRoot)) { return }

    $patterns = @("CiduZW3DAgentPage", "CiduZW3DPage", "ID_CiduZW3DShowAgentForm", "CiduZW3DShowAgentForm", "CiduZW3DAgentGroup")
    Get-ChildItem -LiteralPath $Zw3dRoot -Recurse -Filter "*.zcui" -File -ErrorAction SilentlyContinue | ForEach-Object {
        try {
            if (Select-String -LiteralPath $_.FullName -Pattern $patterns -SimpleMatch -Quiet -ErrorAction SilentlyContinue) {
                Add-UniqueProfileRoot -Roots $Roots -Seen $Seen -ProfileRoot (Get-ProfileRootFromZcuiPath -Path $_.FullName)
            }
        } catch {
        }
    }
}

function Add-Zw3dExistingProfileRootsFromZw3dRoot {
    param(
        [System.Collections.Generic.List[string]]$Roots,
        [hashtable]$Seen,
        [string]$Zw3dRoot
    )
    if (-not (Test-Path -LiteralPath $Zw3dRoot)) { return }

    Get-ChildItem -LiteralPath $Zw3dRoot -Directory -Recurse -ErrorAction SilentlyContinue | ForEach-Object {
        if ($_.Name -ieq "profiles" -and (Split-Path $_.FullName -Parent | Split-Path -Leaf) -ieq "custom") {
            Add-UniqueProfileRoot -Roots $Roots -Seen $Seen -ProfileRoot $_.FullName
        }
    }

    Get-ChildItem -LiteralPath $Zw3dRoot -Directory -Filter "ZW3D *" -ErrorAction SilentlyContinue | ForEach-Object {
        Add-UniqueProfileRoot -Roots $Roots -Seen $Seen -ProfileRoot (Join-Path $_.FullName "custom\profiles")
        Get-ChildItem -LiteralPath $_.FullName -Directory -Filter "instance-*" -ErrorAction SilentlyContinue | ForEach-Object {
            Add-UniqueProfileRoot -Roots $Roots -Seen $Seen -ProfileRoot (Join-Path $_.FullName "custom\profiles")
        }
    }
}

function Get-CiduNativeUiProfileRoots {
    param([string]$UserAppData, [string]$Year, [string]$InstallLocation = "")

    $roots = New-Object 'System.Collections.Generic.List[string]'
    $seen = @{}
    $zw3dRoot = Join-Path $UserAppData "ZWSOFT\ZW3D"
    $userDir = Join-Path $zw3dRoot "ZW3D $Year"

    Add-UniqueProfileRoot -Roots $roots -Seen $seen -ProfileRoot (Join-Path $userDir "custom\profiles")
    Add-Zw3dExistingProfileRootsFromZw3dRoot -Roots $roots -Seen $seen -Zw3dRoot $zw3dRoot

    foreach ($candidate in Get-Zw3dRootCandidates -UserAppData $UserAppData -Year $Year) {
        Add-Zw3dExistingProfileRootsFromZw3dRoot -Roots $roots -Seen $seen -Zw3dRoot $candidate
        Add-CiduMarkedProfileRootsFromZw3dRoot -Roots $roots -Seen $seen -Zw3dRoot $candidate
    }

    if ($InstallLocation) {
        Add-UniqueProfileRoot -Roots $roots -Seen $seen -ProfileRoot (Join-Path $InstallLocation "custom\profiles")
        Add-UniqueProfileRoot -Roots $roots -Seen $seen -ProfileRoot (Join-Path $InstallLocation "profiles")
        Add-Zw3dExistingProfileRootsFromZw3dRoot -Roots $roots -Seen $seen -Zw3dRoot $InstallLocation
    }

    return $roots
}

function Merge-NativeUiProfile {
    param([string]$UserAppData, [string]$Year, [string]$PluginDir, [string]$InstallLocation = "")

    foreach ($profileRoot in Get-CiduNativeUiProfileRoots -UserAppData $UserAppData -Year $Year -InstallLocation $InstallLocation) {
        Merge-NativeUiProfileRoot -ProfileRoot $profileRoot -PluginDir $PluginDir
    }
}

function Get-CiduMarkerCount {
    param(
        [string]$Path,
        [string]$Pattern
    )
    if (-not (Test-Path -LiteralPath $Path)) { return 0 }
    try {
        return @(Select-String -LiteralPath $Path -Pattern $Pattern -SimpleMatch -ErrorAction SilentlyContinue).Count
    } catch {
        return 0
    }
}

function Add-CiduFileReport {
    param(
        [System.Collections.Generic.List[string]]$Lines,
        [string]$Label,
        [string]$Path
    )
    if (Test-Path -LiteralPath $Path) {
        try {
            $item = Get-Item -LiteralPath $Path -ErrorAction Stop
            $Lines.Add("$Label=$Path; exists=true; length=$($item.Length); lastWrite=$($item.LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss'))") | Out-Null
        } catch {
            $Lines.Add("$Label=$Path; exists=true; statError=$($_.Exception.Message)") | Out-Null
        }
    } else {
        $Lines.Add("$Label=$Path; exists=false") | Out-Null
    }
}

function Add-CiduPathReport {
    param(
        [System.Collections.Generic.List[string]]$Lines,
        [string]$Label,
        [string]$Path
    )
    if (Test-Path -LiteralPath $Path) {
        try {
            $item = Get-Item -LiteralPath $Path -ErrorAction Stop
            $kind = if ($item.PSIsContainer) { "directory" } else { "file" }
            $Lines.Add("$Label=$Path; exists=true; type=$kind; lastWrite=$($item.LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss'))") | Out-Null
        } catch {
            $Lines.Add("$Label=$Path; exists=true; statError=$($_.Exception.Message)") | Out-Null
        }
    } else {
        $Lines.Add("$Label=$Path; exists=false") | Out-Null
    }
}

function Add-CiduMatchedLinesReport {
    param(
        [System.Collections.Generic.List[string]]$Lines,
        [string]$Label,
        [string]$Path,
        [string[]]$Patterns,
        [int]$Limit = 30
    )
    if (-not (Test-Path -LiteralPath $Path)) { return }
    try {
        $matches = @(Select-String -LiteralPath $Path -Pattern $Patterns -SimpleMatch -ErrorAction SilentlyContinue | Select-Object -First $Limit)
        foreach ($match in $matches) {
            $line = ($match.Line -replace "\s+", " ").Trim()
            $Lines.Add("$Label line=$($match.LineNumber): $line") | Out-Null
        }
        if ($matches.Count -ge $Limit) {
            $Lines.Add("$Label matchedLineLimit=$Limit") | Out-Null
        }
    } catch {
        $Lines.Add("$Label readError=$($_.Exception.Message)") | Out-Null
    }
}

function Add-CiduRegistryValueReport {
    param(
        [System.Collections.Generic.List[string]]$Lines,
        [string]$Label,
        [string]$Path,
        [string]$Name
    )
    try {
        $props = Get-ItemProperty -Path $Path -ErrorAction Stop
        $value = $props.$Name
        if ($null -eq $value) {
            $Lines.Add("$Label=$Path; $Name=<null>") | Out-Null
        } else {
            $Lines.Add("$Label=$Path; $Name=$value") | Out-Null
        }
    } catch {
        $Lines.Add("$Label=$Path; $Name=<missing>; error=$($_.Exception.Message)") | Out-Null
    }
}

function Write-CiduUiInstallReport {
    param([string]$UserAppData, [string]$Year, [string]$PluginDir, [string]$InstallLocation = "")

    $reportPath = Join-Path $PluginDir "cidu_ui_install_report.log"
    $lines = New-Object 'System.Collections.Generic.List[string]'
    $lines.Add("time=$(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')") | Out-Null
    $lines.Add("computer=$env:COMPUTERNAME") | Out-Null
    $lines.Add("user=$env:USERNAME") | Out-Null
    $lines.Add("psVersion=$($PSVersionTable.PSVersion)") | Out-Null
    $lines.Add("userAppData=$UserAppData") | Out-Null
    $lines.Add("year=$Year") | Out-Null
    $lines.Add("pluginDir=$PluginDir") | Out-Null
    $lines.Add("installLocation=$InstallLocation") | Out-Null
    $lines.Add("envAppData=$env:APPDATA") | Out-Null
    $lines.Add("envLocalAppData=$env:LOCALAPPDATA") | Out-Null
    Add-CiduRegistryValueReport -Lines $lines -Label "zw3dCurrentHklm" -Path "HKLM:\Software\ZWSOFT\ZW3D\CurrentVersion" -Name "LastInstallVersion"
    Add-CiduRegistryValueReport -Lines $lines -Label "zw3dCurrentHklmWow6432" -Path "HKLM:\Software\WOW6432Node\ZWSOFT\ZW3D\CurrentVersion" -Name "LastInstallVersion"
    Add-CiduRegistryValueReport -Lines $lines -Label "zw3dInstallHklmProduct" -Path "HKLM:\Software\ZWSOFT\ZW3D\$Year\ZW3D $Year" -Name "ProductVersion"
    Add-CiduRegistryValueReport -Lines $lines -Label "zw3dInstallHklmPath" -Path "HKLM:\Software\ZWSOFT\ZW3D\$Year\ZW3D $Year" -Name "InstallLocation"
    Add-CiduRegistryValueReport -Lines $lines -Label "zw3dInstallHklmLang" -Path "HKLM:\Software\ZWSOFT\ZW3D\$Year\ZW3D $Year" -Name "CurrentLang"
    Add-CiduRegistryValueReport -Lines $lines -Label "zw3dInstallHklmWow6432Product" -Path "HKLM:\Software\WOW6432Node\ZWSOFT\ZW3D\$Year\ZW3D $Year" -Name "ProductVersion"
    Add-CiduRegistryValueReport -Lines $lines -Label "zw3dInstallHklmWow6432Path" -Path "HKLM:\Software\WOW6432Node\ZWSOFT\ZW3D\$Year\ZW3D $Year" -Name "InstallLocation"
    Add-CiduRegistryValueReport -Lines $lines -Label "zw3dInstallHklmWow6432Lang" -Path "HKLM:\Software\WOW6432Node\ZWSOFT\ZW3D\$Year\ZW3D $Year" -Name "CurrentLang"
    Add-CiduPathReport -Lines $lines -Label "candidateUserZw3dRoot" -Path (Join-Path $UserAppData "ZWSOFT\ZW3D")
    if ($env:APPDATA) {
        Add-CiduPathReport -Lines $lines -Label "candidateEnvAppDataZw3dRoot" -Path (Join-Path $env:APPDATA "ZWSOFT\ZW3D")
    }
    if ($env:LOCALAPPDATA) {
        Add-CiduPathReport -Lines $lines -Label "candidateEnvLocalAppDataZw3dRoot" -Path (Join-Path $env:LOCALAPPDATA "ZWSOFT\ZW3D")
    }
    if ($InstallLocation) {
        Add-CiduPathReport -Lines $lines -Label "candidateInstallCustomProfiles" -Path (Join-Path $InstallLocation "custom\profiles")
        Add-CiduPathReport -Lines $lines -Label "candidateInstallProfiles" -Path (Join-Path $InstallLocation "profiles")
    }
    foreach ($candidate in Get-Zw3dRootCandidates -UserAppData $UserAppData -Year $Year) {
        Add-CiduPathReport -Lines $lines -Label "candidateScannedZw3dRoot" -Path $candidate
    }
    Add-CiduFileReport -Lines $lines -Label "pluginDll" -Path (Join-Path $PluginDir "ZW3DAgentPlugin.dll")
    Add-CiduFileReport -Lines $lines -Label "pluginZrc" -Path (Join-Path $PluginDir "ZW3DAgentPlugin.zrc")
    Add-CiduFileReport -Lines $lines -Label "agentForm" -Path (Join-Path $PluginDir "forms\CiduZW3DAgentForm.ui")
    Add-CiduFileReport -Lines $lines -Label "agentIcon" -Path (Join-Path $PluginDir "Icons\CiduAgent.png")
    Add-CiduFileReport -Lines $lines -Label "agentHtml" -Path (Join-Path $PluginDir "web\agent_panel.html")
    Add-CiduFileReport -Lines $lines -Label "agentCss" -Path (Join-Path $PluginDir "web\agent_panel.css")
    Add-CiduFileReport -Lines $lines -Label "agentBootstrapJs" -Path (Join-Path $PluginDir "web\agent_panel_bootstrap.js")
    Add-CiduFileReport -Lines $lines -Label "markedJs" -Path (Join-Path $PluginDir "web\vendor\marked.umd.js")
    Add-CiduFileReport -Lines $lines -Label "config" -Path (Join-Path $PluginDir "cidu_zw3d_config.json")
    Add-CiduRegistryValueReport -Lines $lines -Label "webview2Hkcu" -Path "HKCU:\Software\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}" -Name "pv"
    Add-CiduRegistryValueReport -Lines $lines -Label "webview2Hklm" -Path "HKLM:\Software\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}" -Name "pv"
    Add-CiduRegistryValueReport -Lines $lines -Label "webview2HklmWow6432" -Path "HKLM:\Software\WOW6432Node\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}" -Name "pv"

    $profileRoots = @(Get-CiduNativeUiProfileRoots -UserAppData $UserAppData -Year $Year -InstallLocation $InstallLocation)
    $lines.Add("profileRootCount=$($profileRoots.Count)") | Out-Null
    foreach ($profileRoot in $profileRoots) {
        $actions = Join-Path $profileRoot "ResourcePool\Actions.zcui"
        $ribbon = Join-Path $profileRoot "ResourcePool\RibbonPagesUser.zcui"
        $layouts = @()
        if (Test-Path -LiteralPath $profileRoot) {
            $layouts = @(Get-ChildItem -LiteralPath $profileRoot -Recurse -Filter "LayoutStrategy.zcui" -File -ErrorAction SilentlyContinue)
        }
        $actionOk = (Test-Path -LiteralPath $actions) -and [bool](Select-String -LiteralPath $actions -Pattern "ID_CiduZW3DShowAgentForm" -SimpleMatch -Quiet -ErrorAction SilentlyContinue)
        $pageOk = (Test-Path -LiteralPath $ribbon) -and [bool](Select-String -LiteralPath $ribbon -Pattern "CiduZW3DAgentPage" -SimpleMatch -Quiet -ErrorAction SilentlyContinue)
        $controlOk = (Test-Path -LiteralPath $ribbon) -and [bool](Select-String -LiteralPath $ribbon -Pattern "ID_CiduZW3DShowAgentForm" -SimpleMatch -Quiet -ErrorAction SilentlyContinue)
        $layoutHitCount = @($layouts | Select-String -Pattern "CiduZW3DAgentPage" -SimpleMatch -List -ErrorAction SilentlyContinue).Count
        $lines.Add("profile=$profileRoot; exists=$(Test-Path -LiteralPath $profileRoot); action=$actionOk; page=$pageOk; control=$controlOk; layoutFiles=$($layouts.Count); layoutHits=$layoutHitCount") | Out-Null
        Add-CiduFileReport -Lines $lines -Label "actionsFile" -Path $actions
        $lines.Add("actionsMarkers actionId=$(Get-CiduMarkerCount -Path $actions -Pattern 'ID_CiduZW3DShowAgentForm'); form=$(Get-CiduMarkerCount -Path $actions -Pattern 'CiduZW3DAgentForm'); icon=$(Get-CiduMarkerCount -Path $actions -Pattern 'CiduAgent.png')") | Out-Null
        Add-CiduMatchedLinesReport -Lines $lines -Label "actions" -Path $actions -Patterns @("ID_CiduZW3DShowAgentForm", "CiduZW3DAgentForm", "Cidu Agent", "CiduAgent.png")
        Add-CiduFileReport -Lines $lines -Label "ribbonFile" -Path $ribbon
        $lines.Add("ribbonMarkers agentPage=$(Get-CiduMarkerCount -Path $ribbon -Pattern 'CiduZW3DAgentPage'); oldPage=$(Get-CiduMarkerCount -Path $ribbon -Pattern 'CiduZW3DPage'); group=$(Get-CiduMarkerCount -Path $ribbon -Pattern 'CiduZW3DAgentGroup'); control=$(Get-CiduMarkerCount -Path $ribbon -Pattern 'ID_CiduZW3DShowAgentForm'); style3=$(Get-CiduMarkerCount -Path $ribbon -Pattern 'buttonStyle=""3""')") | Out-Null
        Add-CiduMatchedLinesReport -Lines $lines -Label "ribbon" -Path $ribbon -Patterns @("CiduZW3DAgentPage", "CiduZW3DPage", "CiduZW3DAgentGroup", "CiduZW3DAgentGroupPanel", "ID_CiduZW3DShowAgentForm", "buttonStyle")
        foreach ($layout in $layouts) {
            $hasPage = [bool](Select-String -LiteralPath $layout.FullName -Pattern "CiduZW3DAgentPage" -SimpleMatch -Quiet -ErrorAction SilentlyContinue)
            Add-CiduFileReport -Lines $lines -Label "layoutFile hasCiduPage=$hasPage" -Path $layout.FullName
            if ($hasPage) {
                Add-CiduMatchedLinesReport -Lines $lines -Label "layout" -Path $layout.FullName -Patterns @("CiduZW3DAgentPage", "CiduZW3DLayoutTab") -Limit 12
            }
        }
    }

    $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllLines($reportPath, $lines, $utf8NoBom)
}

function Merge-NativeUiProfileRoot {
    param([string]$ProfileRoot, [string]$PluginDir)

    $profileRoot = $ProfileRoot
    $resourcePool = Join-Path $profileRoot "ResourcePool"
    New-Item -ItemType Directory -Force -Path $resourcePool | Out-Null
    Remove-UserResourceBlock `
        -Path (Join-Path $resourcePool "RibbonPagesUser.zcui") `
        -Marker 'ID_CiduZW3DShowAgentForm' `
        -Pattern '(?ms)[\t ]*<RibbonPage\b(?=[^>]*\bname="CiduZW3DPage")[^>]*>.*?ID_CiduZW3DShowAgentForm.*?</RibbonPage>\s*\r?\n?'
    foreach ($role in Get-Zw3dUiRoles) {
        foreach ($uiEnvironment in Get-Zw3dUiEnvironments) {
            Remove-LayoutStrategyInsertByName `
                -Path (Join-Path $profileRoot "$role\$($uiEnvironment.Directory)\LayoutStrategy.zcui") `
                -Name "CiduZW3DPage"
        }
    }

    Merge-ActionResource `
        -Path (Join-Path $resourcePool "Actions.zcui") `
        -InsertBlock (Get-CiduActionInsertBlock -PluginDir $PluginDir) `
        -Marker 'name="ID_CiduZW3DShowAgentForm"' `
        -ExistingBlockPattern '(?ms)[\t ]*<Action\b(?=[^>]*\bname="ID_~?CiduZW3DShowAgentForm")[^>]*(?:/>|>.*?</Action>)\s*\r?\n?'
    Merge-UserResource `
        -Path (Join-Path $resourcePool "RibbonPagesUser.zcui") `
        -RootOpenTag '<RibbonPages>' `
        -RootCloseTag '</RibbonPages>' `
        -Marker 'name="CiduZW3DAgentPage"' `
        -InsertBlock (Get-CiduRibbonPageInsertBlock) `
        -ExistingBlockPattern '(?ms)[\t ]*<RibbonPage\b(?=[^>]*\bname="CiduZW3DAgentPage")[^>]*(?:/>|>.*?</RibbonPage>)\s*\r?\n?'
    foreach ($role in Get-Zw3dUiRoles) {
        foreach ($uiEnvironment in Get-Zw3dUiEnvironments) {
            Merge-LayoutStrategy `
                -Path (Join-Path $profileRoot "$role\$($uiEnvironment.Directory)\LayoutStrategy.zcui") `
                -Environment $uiEnvironment.Environment `
                -Layout $uiEnvironment.Layout `
                -Role $role
        }
    }
}

function Register-UninstallEntry {
    param([string]$PluginDir, [string]$InstallLocation)
    $uninstallExe = Join-Path $PluginDir "CiduZW3DUninstall.exe"
    $uninstallPath = Join-Path $PluginDir "CiduZW3DInstaller\uninstall.ps1"
    $uninstallCommand = "powershell -NoProfile -ExecutionPolicy Bypass -File `"$uninstallPath`""
    if (Test-Path $uninstallExe) {
        $uninstallCommand = "`"$uninstallExe`""
    }
    $uninstallReg = "HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\CiduZW3DPlugins"
    New-Item -Path $uninstallReg -Force | Out-Null
    New-ItemProperty -Path $uninstallReg -Name "DisplayName" -Value "Cidu ZW3D Plugins" -PropertyType String -Force | Out-Null
    New-ItemProperty -Path $uninstallReg -Name "DisplayVersion" -Value "1.0.0" -PropertyType String -Force | Out-Null
    New-ItemProperty -Path $uninstallReg -Name "Publisher" -Value "Cidu" -PropertyType String -Force | Out-Null
    New-ItemProperty -Path $uninstallReg -Name "InstallLocation" -Value $InstallLocation -PropertyType String -Force | Out-Null
    New-ItemProperty -Path $uninstallReg -Name "UninstallString" -Value $uninstallCommand -PropertyType String -Force | Out-Null
    New-ItemProperty -Path $uninstallReg -Name "NoModify" -Value 1 -PropertyType DWord -Force | Out-Null
    New-ItemProperty -Path $uninstallReg -Name "NoRepair" -Value 1 -PropertyType DWord -Force | Out-Null
}

function New-InstallerShortcut {
    param([string]$Path, [string]$TargetPath, [string]$Arguments = "", [string]$WorkingDirectory = "")
    $shell = New-Object -ComObject WScript.Shell
    $shortcut = $shell.CreateShortcut($Path)
    $shortcut.TargetPath = $TargetPath
    if ($Arguments) { $shortcut.Arguments = $Arguments }
    if ($WorkingDirectory) { $shortcut.WorkingDirectory = $WorkingDirectory }
    $shortcut.Save()
}

function New-InstallerShortcuts {
    param([string]$PluginDir, [string]$FolderName, [switch]$AllUsers)
    if (-not $FolderName) { return }
    $programs = if ($AllUsers) {
        Join-Path $env:ProgramData "Microsoft\Windows\Start Menu\Programs"
    } else {
        [Environment]::GetFolderPath("Programs")
    }
    $folder = Join-Path $programs $FolderName
    try {
        New-Item -ItemType Directory -Force -Path $folder | Out-Null
    } catch {
        $programs = [Environment]::GetFolderPath("Programs")
        $folder = Join-Path $programs $FolderName
        New-Item -ItemType Directory -Force -Path $folder | Out-Null
    }

    $uninstallExe = Join-Path $PluginDir "CiduZW3DUninstall.exe"
    $uninstallPath = Join-Path $PluginDir "CiduZW3DInstaller\uninstall.ps1"
    $powershell = Join-Path $env:WINDIR "System32\WindowsPowerShell\v1.0\powershell.exe"
    New-InstallerShortcut `
        -Path (Join-Path $folder "打开插件目录.lnk") `
        -TargetPath (Join-Path $env:WINDIR "explorer.exe") `
        -Arguments "`"$PluginDir`"" `
        -WorkingDirectory $PluginDir
    if (Test-Path $uninstallExe) {
        New-InstallerShortcut `
            -Path (Join-Path $folder "卸载 Cidu ZW3D 插件.lnk") `
            -TargetPath $uninstallExe `
            -WorkingDirectory $PluginDir
    } elseif (Test-Path $uninstallPath) {
        New-InstallerShortcut `
            -Path (Join-Path $folder "卸载 Cidu ZW3D 插件.lnk") `
            -TargetPath $powershell `
            -Arguments "-NoProfile -ExecutionPolicy Bypass -File `"$uninstallPath`"" `
            -WorkingDirectory $PluginDir
    }
}

$temp = $null
try {
    Assert-Zw3dClosed
    $info = Get-Zw3dInfo
    Assert-WebView2RuntimeInstalled
    $pluginDir = Join-Path $info.InstallLocation "apilibs"
    $userAppData = Resolve-TargetUserAppData -Provided $TargetUserAppData
    $userDir = Join-Path $userAppData "ZWSOFT\ZW3D\ZW3D $($info.Year)"

    Write-Host "ZW3D: $($info.ProductVersion) $($info.Lang)"
    Write-Host "InstallLocation: $($info.InstallLocation)"
    Write-Host "插件目录 (admin): $pluginDir"
    Write-Host "用户配置目录: $userDir"

    if (-not (Test-Path $PayloadZip)) {
        throw "安装包缺少 payload.zip: $PayloadZip"
    }
    $temp = Join-Path $env:TEMP ("CiduZW3DInstaller-" + [guid]::NewGuid().ToString("N"))
    New-Item -ItemType Directory -Force -Path $temp | Out-Null
    Expand-Archive -LiteralPath $PayloadZip -DestinationPath $temp -Force

    Clear-CiduRegistryInstallRecords
    Clear-CiduZw3dPluginRegistry
    Clear-PreviousCiduInstall -PluginDir $pluginDir
    Copy-Payload -PayloadDir $temp -PluginDir $pluginDir
    Clear-LegacySettingsRibbon -PluginDir $pluginDir
    Clear-LegacyUserApilibs -UserDir $userDir
    Merge-NativeUiProfile -UserAppData $userAppData -Year $info.Year -PluginDir $pluginDir -InstallLocation $info.InstallLocation
    Write-CiduUiInstallReport -UserAppData $userAppData -Year $info.Year -PluginDir $pluginDir -InstallLocation $info.InstallLocation
    Register-UninstallEntry -PluginDir $pluginDir -InstallLocation $info.InstallLocation
    New-InstallerShortcuts -PluginDir $pluginDir -FolderName $ShortcutFolder -AllUsers:$ShortcutsForAllUsers

    Write-Host "Cidu ZW3D Plugins 安装完成。"
} finally {
    if ($temp -and (Test-Path $temp)) {
        Remove-Item -LiteralPath $temp -Recurse -Force -ErrorAction SilentlyContinue
    }
    if ($LogFile) {
        try { Stop-Transcript | Out-Null } catch {}
    }
}
