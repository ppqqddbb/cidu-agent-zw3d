# Cidu ZW3D Installer

生成安装器：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File installer\build_installer.ps1
```

如果已经编译过插件，只重新打包安装器：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File installer\build_installer.ps1 -SkipBuild
```

输出文件：

```text
dist\CiduZW3DPluginInstaller.exe
```

安装器会执行：

- 要求先关闭 ZW3D。
- 自动检测当前安装的 ZW3D 年份、ProductVersion 和语言。
- 安装 `ZW3DAgentPlugin.dll` 与 `ZW3DAgentPlugin.zrc`，并把参数化引擎作为内部 `CiduParametricEngine.dll` 一起部署。
- 安装 WebView2Loader、Web 前端、表单、图标和配置文件。
- 写入当前用户的 ZW3D 插件注册表。
- 合并原生 Ribbon 页签到当前用户 profile。
- 注册当前用户卸载入口。

注意：`config\cidu_zw3d_config.json` 会被打进安装器，里面包含当前 API key。
