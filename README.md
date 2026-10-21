# cidu-ZW3D

C++ scaffold for a ZW3D agent plugin plus its internal modeling engine:

1. `ZW3DAgentPlugin`
   - Talks to a Cidu application API.
   - Asks the agent for model parameter suggestions.
   - Passes generated parameters to the parametric generation layer.

2. `CiduParametricEngine`
   - Internal DLL loaded by the Agent plugin.
   - Reads and writes model parameters.
   - Generates models from a JSON parameter payload.

Only `ZW3DAgentPlugin` is registered as a ZW3D plugin. The parameterized
modeling engine is deployed as `CiduParametricEngine.dll` and is not registered
with ZW3D.

## Repository Layout

```text
cidu-ZW3D
├─ CMakeLists.txt
├─ config
│  ├─ cidu_zw3d_config.example.json
│  └─ model_parameter_schema.json
├─ docs
├─ include/cidu_zw3d
├─ scripts
├─ src
│  ├─ agent_plugin
│  ├─ parametric_plugin
│  └─ shared
└─ tools
```

## Runtime Flow

```text
User in ZW3D
  -> ZW3DAgentPlugin
  -> Cidu /v1/chat-messages
  -> model parameter JSON
  -> CiduParametricEngine
  -> ZW3D SDK calls
  -> generated model
```

The parameter JSON contract is:

```json
{
  "template_name": "box",
  "numeric": {
    "length": 100,
    "width": 60,
    "height": 30
  },
  "text": {
    "material": "steel",
    "part_no": "DEMO-001"
  }
}
```

## Build

Install CMake and a Windows C++ compiler, then run:

```powershell
cmake -S . -B build -G "Visual Studio 18 2026" -A x64 -DCIDU_ZW3D_WITH_REAL_SDK=ON -DCIDU_ZW3D_INSTALL_DIR="D:/Program Files/ZWSOFT/ZW3D 2026"
cmake --build build --config Release
```

The build produces:

```text
CiduParametricEngine.dll
ZW3DAgentPlugin.dll
ZW3DAgentPlugin.zrc
cidu_zw3d_cli.exe
```

## Install Into ZW3D

Close ZW3D first, then run:

```powershell
scripts\register_plugins.bat
```

The script copies the Agent plugin, internal engine, `ZW3DAgentPlugin.zrc`,
and the config file into:

```text
%APPDATA%\ZWSOFT\ZW3D\ZW3D 2026\custom\apilibs
```

It registers only `ZW3DAgentPlugin` under the current user's ZW3D `Plugin_x64`
registry key. After restarting ZW3D, use the `Cidu Agent` button on the
`Cidu ZW3D` ribbon page.

## Cidu Config

Copy:

```text
config/cidu_zw3d_config.example.json
```

to the plugin output directory as:

```text
cidu_zw3d_config.json
```

Then set:

```json
{
  "base_url": "http://127.0.0.1/v1",
  "api_key": "app-your-cidu-api-key",
  "user": "zw3d-user"
}
```

## ZW3D SDK Integration

The current SDK boundary is:

```text
include/cidu_zw3d/zw3d_host.h
src/shared/zw3d_host.cpp
```

Replace the stub implementation with real ZW3D calls for:

- command registration
- parameter read/write
- part/assembly creation
- feature regeneration
- file save/export

Then update `CMakeLists.txt` and enable:

```powershell
cmake -S . -B build -DCIDU_ZW3D_WITH_REAL_SDK=ON
```

## Notes

- `scripts/register_plugins.bat` detects the installed ZW3D version/language
  from the registry and registers the built DLLs for the current user.
- The Cidu client uses WinHTTP and posts to `/chat-messages`.
- For a local Cidu repo at `D:\workspace\code\cidu`, run Cidu separately; this
  plugin project only consumes the application API.

## 编译打包命令

```powershell
# 切换目录
cd D:\File\VSCodeFile\QDHB\cidu-dify-plugins\cidu-zw3d-agent-main

# 准备 WebView2 SDK
New-Item -ItemType Directory -Force .\third_party\webview2 | Out-Null
Invoke-WebRequest -Uri "https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2" -OutFile ".\third_party\webview2\webview2.nupkg"
Copy-Item ".\third_party\webview2\webview2.nupkg" ".\third_party\webview2\webview2.zip" -Force
Expand-Archive ".\third_party\webview2\webview2.zip" ".\third_party\webview2\package" -Force

# 编译插件
Remove-Item -Recurse -Force .\build -ErrorAction SilentlyContinue

cmake -S . -B build -G "Visual Studio 18 2026" -A x64 `
  -DCIDU_ZW3D_WITH_REAL_SDK=ON `
  -DCIDU_ZW3D_INSTALL_DIR="D:/File/3D" `
  -DCIDU_ZW3D_ZRC_EXE="D:/File/3D/zrc.exe" `
  -DCIDU_ZW3D_WEBVIEW2_DIR="D:/File/VSCodeFile/QDHB/cidu-dify-plugins/cidu-zw3d-agent-main/third_party/webview2/package/build/native"

cmake --build build --config Release

# 本机安装测试
scripts\register_plugins.bat

# 打包
powershell -NoProfile -ExecutionPolicy Bypass -File installer\build_installer.ps1 -SkipBuild
```
