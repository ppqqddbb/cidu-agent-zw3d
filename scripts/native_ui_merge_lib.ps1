function Merge-LayoutStrategy {
    param([string]$Path, [string]$Environment, [string]$Layout, [string]$Role = "4-Expert")
    $parentPath = Split-Path $Path
    if ($parentPath) {
        New-Item -ItemType Directory -Force -Path $parentPath | Out-Null
    }

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
    $parentPath = Split-Path $Path
    if ($parentPath) {
        New-Item -ItemType Directory -Force -Path $parentPath | Out-Null
    }

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
    $parentPath = Split-Path $Path
    if ($parentPath) {
        New-Item -ItemType Directory -Force -Path $parentPath | Out-Null
    }

    if (-not (Test-Path -LiteralPath $Path)) {
        $actionListOpen = '<Action#List>'
        $actionListClose = '</Action#List>'
        $template = "<?xml version=`"1.0`" encoding=`"UTF-8`"?>`r`n<Actions system=`"false`">`r`n`t$actionListOpen`r`n$InsertBlock`r`n`t$actionListClose`r`n</Actions>`r`n"
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
    if ($text.Contains('</Action#List>')) {
        $pattern = '</Action#List>'
        $index = $text.IndexOf($pattern)
        $newText = $text.Substring(0, $index) + $InsertBlock + "`r`n`t" + $text.Substring($index)
    } elseif ($text.Contains('</Actions>')) {
        $actionListOpen = '<Action#List>'
        $actionListClose = '</Action#List>'
        $wrapped = "`t$actionListOpen`r`n$InsertBlock`r`n`t$actionListClose`r`n"
        $newText = $text.Replace('</Actions>', "$wrapped</Actions>")
    } else {
        Write-Warning ($Path + ' 内未找到 Action#List 或 Actions 结束标签，跳过注入。')
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

function Merge-NativeUiProfileRoot {
    param([string]$ProfileRoot, [string]$PluginDir)

    $resourcePool = Join-Path $ProfileRoot "ResourcePool"
    New-Item -ItemType Directory -Force -Path $resourcePool | Out-Null

    $removeUserParams = @{
        Path = Join-Path $resourcePool "RibbonPagesUser.zcui"
        Marker = 'ID_CiduZW3DShowAgentForm'
        Pattern = '(?ms)[\t ]*<RibbonPage\b(?=[^>]*\bname="CiduZW3DPage")[^>]*>.*?ID_CiduZW3DShowAgentForm.*?</RibbonPage>\s*\r?\n?'
    }
    Remove-UserResourceBlock @removeUserParams

    foreach ($role in Get-Zw3dUiRoles) {
        foreach ($uiEnvironment in Get-Zw3dUiEnvironments) {
            $removeLayoutParams = @{
                Path = Join-Path $ProfileRoot "$role\$($uiEnvironment.Directory)\LayoutStrategy.zcui"
                Name = "CiduZW3DPage"
            }
            Remove-LayoutStrategyInsertByName @removeLayoutParams
        }
    }

    $actionParams = @{
        Path = Join-Path $resourcePool "Actions.zcui"
        InsertBlock = Get-CiduActionInsertBlock -PluginDir $PluginDir
        Marker = 'name="ID_CiduZW3DShowAgentForm"'
        ExistingBlockPattern = '(?ms)[\t ]*<Action\b(?=[^>]*\bname="ID_~?CiduZW3DShowAgentForm")[^>]*(?:/>|>.*?</Action>)\s*\r?\n?'
    }
    Merge-ActionResource @actionParams

    $userResourceParams = @{
        Path = Join-Path $resourcePool "RibbonPagesUser.zcui"
        RootOpenTag = '<RibbonPages>'
        RootCloseTag = '</RibbonPages>'
        Marker = 'name="CiduZW3DAgentPage"'
        InsertBlock = Get-CiduRibbonPageInsertBlock
        ExistingBlockPattern = '(?ms)[\t ]*<RibbonPage\b(?=[^>]*\bname="CiduZW3DAgentPage")[^>]*(?:/>|>.*?</RibbonPage>)\s*\r?\n?'
    }
    Merge-UserResource @userResourceParams

    foreach ($role in Get-Zw3dUiRoles) {
        foreach ($uiEnvironment in Get-Zw3dUiEnvironments) {
            $layoutParams = @{
                Path = Join-Path $ProfileRoot "$role\$($uiEnvironment.Directory)\LayoutStrategy.zcui"
                Environment = $uiEnvironment.Environment
                Layout = $uiEnvironment.Layout
                Role = $role
            }
            Merge-LayoutStrategy @layoutParams
        }
    }
}
