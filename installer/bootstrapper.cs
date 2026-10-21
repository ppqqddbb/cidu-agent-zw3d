using System;
using System.ComponentModel;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Reflection;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Forms;
using Microsoft.Win32;

internal static class Program
{
    [STAThread]
    private static int Main()
    {
        Application.EnableVisualStyles();
        Application.SetCompatibleTextRenderingDefault(false);

        string installLocation = Zw3dDetector.DetectInstallLocation();
        if (string.IsNullOrEmpty(installLocation))
        {
            MessageBox.Show(
                "未找到 ZW3D 安装信息（HKLM\\Software\\ZWSOFT\\ZW3D）。\r\n请先安装 ZW3D 2026 再运行本安装器。",
                "Cidu ZW3D Agent 安装程序",
                MessageBoxButtons.OK,
                MessageBoxIcon.Error);
            return 2;
        }

        string userAppData = Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData);

        using (InstallerWizard wizard = new InstallerWizard(installLocation, userAppData))
        {
            Application.Run(wizard);
            return wizard.ExitCode;
        }
    }
}

internal static class Zw3dDetector
{
    public static string DetectInstallLocation()
    {
        string year = ReadHklmString(@"Software\ZWSOFT\ZW3D\CurrentVersion", "LastInstallVersion");
        if (string.IsNullOrEmpty(year))
        {
            year = ReadHklmString(@"Software\WOW6432Node\ZWSOFT\ZW3D\CurrentVersion", "LastInstallVersion");
        }
        if (string.IsNullOrEmpty(year))
        {
            year = FindLatestYear();
        }
        if (string.IsNullOrEmpty(year))
        {
            return null;
        }

        string install = ReadHklmString("Software\\ZWSOFT\\ZW3D\\" + year + "\\ZW3D " + year, "InstallLocation");
        if (string.IsNullOrEmpty(install))
        {
            install = ReadHklmString("Software\\WOW6432Node\\ZWSOFT\\ZW3D\\" + year + "\\ZW3D " + year, "InstallLocation");
        }
        if (string.IsNullOrEmpty(install))
        {
            return null;
        }
        return install.TrimEnd('\\', '/');
    }

    private static string FindLatestYear()
    {
        string best = null;
        foreach (string root in new string[] { @"Software\ZWSOFT\ZW3D", @"Software\WOW6432Node\ZWSOFT\ZW3D" })
        {
            try
            {
                using (RegistryKey key = RegistryKey.OpenBaseKey(RegistryHive.LocalMachine, RegistryView.Default).OpenSubKey(root))
                {
                    if (key == null) continue;
                    foreach (string name in key.GetSubKeyNames())
                    {
                        int parsed;
                        if (int.TryParse(name, out parsed) && parsed >= 2000 && parsed < 3000)
                        {
                            if (best == null || int.Parse(best) < parsed)
                            {
                                best = name;
                            }
                        }
                    }
                }
            }
            catch
            {
            }
        }
        return best;
    }

    private static string ReadHklmString(string subPath, string name)
    {
        try
        {
            using (RegistryKey key = RegistryKey.OpenBaseKey(RegistryHive.LocalMachine, RegistryView.Default).OpenSubKey(subPath))
            {
                if (key == null) return null;
                object value = key.GetValue(name);
                if (value == null) return null;
                string s = value.ToString();
                return string.IsNullOrEmpty(s) ? null : s;
            }
        }
        catch
        {
            return null;
        }
    }
}

internal sealed class InstallerWizard : Form
{
    private readonly Panel pageHost = new Panel();
    private readonly Button backButton = new Button();
    private readonly Button nextButton = new Button();
    private readonly Button cancelButton = new Button();
    private readonly TextBox shortcutBox = new TextBox();
    private readonly RadioButton currentUserShortcut = new RadioButton();
    private readonly RadioButton allUsersShortcut = new RadioButton();
    private readonly Label statusLabel = new Label();
    private readonly ProgressBar progress = new ProgressBar();
    private readonly TextBox logBox = new TextBox();
    private readonly string zw3dInstallLocation;
    private readonly string pluginDir;
    private readonly string userAppData;
    private int pageIndex;
    private bool installing;

    private int exitCode = 1;
    public int ExitCode { get { return exitCode; } }

    public InstallerWizard(string installLocation, string appData)
    {
        zw3dInstallLocation = installLocation;
        pluginDir = Path.Combine(installLocation, "apilibs");
        userAppData = appData;

        Text = "Cidu ZW3D Agent 安装程序";
        StartPosition = FormStartPosition.CenterScreen;
        ClientSize = new Size(760, 540);
        MinimumSize = new Size(720, 500);
        MaximizeBox = false;
        Font = new Font("Microsoft YaHei UI", 9F);
        try { Icon = Icon.ExtractAssociatedIcon(Application.ExecutablePath); } catch { }

        pageHost.Dock = DockStyle.Fill;
        Controls.Add(pageHost);

        Panel buttons = new Panel { Dock = DockStyle.Bottom, Height = 58, BackColor = SystemColors.Control };
        FlowLayoutPanel buttonLayout = new FlowLayoutPanel();
        buttonLayout.Dock = DockStyle.Right;
        buttonLayout.Width = 360;
        buttonLayout.FlowDirection = FlowDirection.RightToLeft;
        buttonLayout.WrapContents = false;
        buttonLayout.Padding = new Padding(0, 14, 14, 0);
        backButton.Text = "< 返回(B)";
        nextButton.Text = "下一步(N) >";
        cancelButton.Text = "取消(C)";
        backButton.Size = new Size(104, 30);
        nextButton.Size = new Size(112, 30);
        cancelButton.Size = new Size(92, 30);
        backButton.Margin = new Padding(4, 0, 4, 0);
        nextButton.Margin = new Padding(4, 0, 4, 0);
        cancelButton.Margin = new Padding(4, 0, 4, 0);
        buttonLayout.Controls.AddRange(new Control[] { cancelButton, nextButton, backButton });
        buttons.Controls.Add(buttonLayout);
        Controls.Add(buttons);

        shortcutBox.Text = "Cidu ZW3D";
        allUsersShortcut.Text = "使快捷方式对所有用户都可用";
        currentUserShortcut.Text = "只对当前用户安装快捷方式";
        allUsersShortcut.Checked = true;

        backButton.Click += delegate { if (pageIndex > 0) ShowPage(pageIndex - 1); };
        nextButton.Click += delegate { Next(); };
        cancelButton.Click += delegate { if (!installing) Close(); };
        FormClosing += delegate(object sender, FormClosingEventArgs e) { if (installing) e.Cancel = true; };

        ShowPage(0);
    }

    private void ShowPage(int index)
    {
        pageIndex = index;
        pageHost.Controls.Clear();
        switch (pageIndex)
        {
            case 0:
                pageHost.Controls.Add(CreateWelcomePage());
                backButton.Enabled = false;
                nextButton.Text = "下一步(N) >";
                nextButton.Enabled = true;
                cancelButton.Enabled = true;
                break;
            case 1:
                pageHost.Controls.Add(CreateLocationPage());
                backButton.Enabled = true;
                nextButton.Text = "下一步(N) >";
                nextButton.Enabled = true;
                cancelButton.Enabled = true;
                break;
            case 2:
                pageHost.Controls.Add(CreateShortcutPage());
                backButton.Enabled = true;
                nextButton.Text = "下一步(N) >";
                nextButton.Enabled = true;
                cancelButton.Enabled = true;
                break;
            case 3:
                pageHost.Controls.Add(CreateReadyPage());
                backButton.Enabled = true;
                nextButton.Text = "安装(I)";
                nextButton.Enabled = true;
                cancelButton.Enabled = true;
                break;
            case 4:
                pageHost.Controls.Add(CreateInstallPage());
                backButton.Enabled = false;
                nextButton.Enabled = false;
                cancelButton.Enabled = false;
                BeginInstall();
                break;
            case 5:
                pageHost.Controls.Add(CreateFinishPage());
                backButton.Enabled = false;
                nextButton.Text = "完成(F)";
                nextButton.Enabled = true;
                cancelButton.Enabled = false;
                break;
        }
    }

    private void Next()
    {
        if (pageIndex == 5)
        {
            exitCode = 0;
            Close();
            return;
        }
        if (pageIndex == 3)
        {
            if (!EnsureZw3dClosedBeforeInstall())
            {
                return;
            }
            ShowPage(4);
            return;
        }
        ShowPage(pageIndex + 1);
    }

    private bool EnsureZw3dClosedBeforeInstall()
    {
        Process[] running = GetRunningZw3dProcesses();
        if (running.Length == 0)
        {
            return true;
        }

        DialogResult result = MessageBox.Show(
            this,
            "检测到 ZW3D 正在运行。\r\n\r\n继续安装需要关闭 ZW3D，否则插件文件和界面资源可能无法覆盖。\r\n是否强制退出 ZW3D 并继续安装？\r\n\r\n请先确认当前模型和文件已经保存。",
            "关闭 ZW3D 后继续安装",
            MessageBoxButtons.YesNo,
            MessageBoxIcon.Warning,
            MessageBoxDefaultButton.Button2);
        if (result != DialogResult.Yes)
        {
            DisposeProcesses(running);
            return false;
        }

        try
        {
            CloseZw3dProcesses(running);
            return true;
        }
        catch (Exception ex)
        {
            MessageBox.Show(this, "无法关闭 ZW3D：\r\n" + ex.Message + "\r\n\r\n请手动关闭 ZW3D 后再继续安装。", "关闭 ZW3D 失败", MessageBoxButtons.OK, MessageBoxIcon.Error);
            return false;
        }
        finally
        {
            DisposeProcesses(running);
        }
    }

    private static Process[] GetRunningZw3dProcesses()
    {
        List<Process> running = new List<Process>();
        foreach (Process process in Process.GetProcesses())
        {
            try
            {
                string name = process.ProcessName;
                if (name.Equals("zw3d", StringComparison.OrdinalIgnoreCase) ||
                    name.StartsWith("ZW3D", StringComparison.OrdinalIgnoreCase))
                {
                    running.Add(process);
                    continue;
                }
            }
            catch
            {
            }
            process.Dispose();
        }
        return running.ToArray();
    }

    private static void CloseZw3dProcesses(Process[] processes)
    {
        foreach (Process process in processes)
        {
            if (process.HasExited)
            {
                continue;
            }
            try
            {
                process.CloseMainWindow();
            }
            catch
            {
            }
        }

        foreach (Process process in processes)
        {
            if (!process.HasExited)
            {
                try
                {
                    process.WaitForExit(3000);
                }
                catch
                {
                }
            }
            if (!process.HasExited)
            {
                process.Kill();
                process.WaitForExit(5000);
            }
            if (!process.HasExited)
            {
                throw new InvalidOperationException(process.ProcessName + " 仍在运行。");
            }
        }
    }

    private static void DisposeProcesses(Process[] processes)
    {
        foreach (Process process in processes)
        {
            process.Dispose();
        }
    }

    private Control CreateWelcomePage()
    {
        Panel page = BasePage("欢迎", "欢迎使用 Cidu ZW3D Agent 安装向导。");
        AddBody(page, "本向导将把 Cidu ZW3D Agent 安装到您的 ZW3D 2026 安装目录下的 apilibs，并在您的用户配置中启用 Ribbon 标签。\r\n\r\n安装需要管理员权限，单击“安装”时会弹出 UAC 提示。\r\n\r\n继续安装前，请关闭所有正在运行的 ZW3D 窗口。\r\n\r\n单击“下一步”继续。");
        return page;
    }

    private Control CreateLocationPage()
    {
        Panel page = BasePage("检测到的 ZW3D", "安装位置由 ZW3D 注册表自动确定。");
        page.Controls.Add(NewLabel("ZW3D 安装目录：", 34, 120, 650, 22, true));
        TextBox zw3dBox = new TextBox();
        zw3dBox.Text = zw3dInstallLocation;
        zw3dBox.ReadOnly = true;
        zw3dBox.SetBounds(34, 148, 680, 26);
        page.Controls.Add(zw3dBox);

        page.Controls.Add(NewLabel("插件部署目录：", 34, 196, 650, 22, true));
        TextBox pluginBox = new TextBox();
        pluginBox.Text = pluginDir;
        pluginBox.ReadOnly = true;
        pluginBox.SetBounds(34, 224, 680, 26);
        page.Controls.Add(pluginBox);

        page.Controls.Add(NewLabel("用户配置目录：", 34, 272, 650, 22, true));
        TextBox userBox = new TextBox();
        userBox.Text = userAppData;
        userBox.ReadOnly = true;
        userBox.SetBounds(34, 300, 680, 26);
        page.Controls.Add(userBox);

        page.Controls.Add(NewLabel("插件 DLL/资源将复制到 ZW3D 安装目录的 apilibs 下，由 ZW3D 自动加载；Ribbon 标签写入您当前用户的 ZW3D 配置。", 34, 344, 680, 60, false));
        return page;
    }

    private Control CreateShortcutPage()
    {
        Panel page = BasePage("快捷方式文件夹", "请选择开始菜单快捷方式位置。");
        page.Controls.Add(NewLabel("快捷方式文件夹：", 34, 120, 650, 22, true));
        shortcutBox.SetBounds(34, 148, 650, 26);
        currentUserShortcut.SetBounds(34, 198, 300, 28);
        allUsersShortcut.SetBounds(34, 228, 320, 28);
        page.Controls.AddRange(new Control[] { shortcutBox, currentUserShortcut, allUsersShortcut });
        return page;
    }

    private Control CreateReadyPage()
    {
        Panel page = BasePage("准备安装", "安装向导已准备好开始安装。");
        string text = "将使用以下设置：\r\n\r\n" +
            "ZW3D 安装目录：    " + zw3dInstallLocation + "\r\n" +
            "插件部署目录：     " + pluginDir + "\r\n" +
            "用户配置目录：     " + userAppData + "\r\n" +
            "快捷方式文件夹：   " + shortcutBox.Text + "\r\n\r\n" +
            "单击“安装”后将弹出 UAC 管理员授权提示。";
        AddBody(page, text);
        return page;
    }

    private Control CreateInstallPage()
    {
        Panel page = BasePage("正在安装", "请稍候，安装向导正在安装 Cidu ZW3D Agent。");
        statusLabel.SetBounds(34, 118, 680, 24);
        statusLabel.Text = "正在等待管理员授权 (UAC)...";
        progress.SetBounds(34, 150, 680, 22);
        progress.Style = ProgressBarStyle.Marquee;
        logBox.SetBounds(34, 190, 680, 240);
        logBox.Multiline = true;
        logBox.ReadOnly = true;
        logBox.ScrollBars = ScrollBars.Vertical;
        page.Controls.AddRange(new Control[] { statusLabel, progress, logBox });
        return page;
    }

    private Control CreateFinishPage()
    {
        Panel page = BasePage("安装完成", "Cidu ZW3D Agent 已安装完成。");
        AddBody(page, "感谢您安装 Cidu ZW3D Agent。\r\n\r\n请重新启动 ZW3D 以加载插件并显示 Cidu ZW3D Ribbon 标签。\r\n\r\n单击“完成”退出安装向导。");
        return page;
    }

    private Panel BasePage(string title, string subtitle)
    {
        Panel page = new Panel { Dock = DockStyle.Fill, BackColor = Color.White };
        Panel header = new Panel { Dock = DockStyle.Top, Height = 86, BackColor = Color.FromArgb(245, 248, 252) };
        Label titleLabel = NewLabel(title, 28, 18, 680, 28, true);
        titleLabel.Font = new Font(Font.FontFamily, 14F, FontStyle.Bold);
        Label subtitleLabel = NewLabel(subtitle, 28, 50, 680, 22, false);
        header.Controls.AddRange(new Control[] { titleLabel, subtitleLabel });
        page.Controls.Add(header);
        return page;
    }

    private static Label NewLabel(string text, int x, int y, int width, int height, bool bold)
    {
        return new Label
        {
            Text = text,
            Location = new Point(x, y),
            Size = new Size(width, height),
            Font = new Font("Microsoft YaHei UI", 9F, bold ? FontStyle.Bold : FontStyle.Regular),
            AutoSize = false
        };
    }

    private void AddBody(Control page, string text)
    {
        Label body = NewLabel(text, 34, 120, 680, 320, false);
        body.Font = new Font(Font.FontFamily, 10F);
        page.Controls.Add(body);
    }

    private async void BeginInstall()
    {
        installing = true;
        string tempDir = Path.Combine(Path.GetTempPath(), "CiduZW3DInstaller-" + Guid.NewGuid().ToString("N"));
        string logFile = Path.Combine(tempDir, "install.log");
        try
        {
            Directory.CreateDirectory(tempDir);
            ExtractResource("CiduZW3D.install.ps1", Path.Combine(tempDir, "install.ps1"));
            ExtractResource("CiduZW3D.payload.zip", Path.Combine(tempDir, "payload.zip"));
            statusLabel.Text = "正在等待管理员授权 (UAC)...";

            int code = await Task.Run(delegate { return RunInstall(tempDir, logFile); });

            string log = ReadLogSafe(logFile);
            if (!string.IsNullOrEmpty(log))
            {
                AppendLog(log);
            }

            if (code != 0)
            {
                throw new InvalidOperationException("安装脚本退出代码：" + code);
            }
            statusLabel.Text = "安装完成。";
            progress.Style = ProgressBarStyle.Blocks;
            progress.Value = 100;
            installing = false;
            ShowPage(5);
        }
        catch (Exception ex)
        {
            installing = false;
            progress.Style = ProgressBarStyle.Blocks;
            statusLabel.Text = "安装失败。";
            string log = ReadLogSafe(logFile);
            if (!string.IsNullOrEmpty(log))
            {
                AppendLog(log);
            }
            AppendLog(ex.Message);
            MessageBox.Show(this, ex.Message, "安装失败", MessageBoxButtons.OK, MessageBoxIcon.Error);
            backButton.Enabled = true;
            nextButton.Enabled = true;
            nextButton.Text = "重试(I)";
            cancelButton.Enabled = true;
            pageIndex = 3;
        }
        finally
        {
            try
            {
                if (Directory.Exists(tempDir))
                {
                    Directory.Delete(tempDir, true);
                }
            }
            catch
            {
            }
        }
    }

    private int RunInstall(string tempDir, string logFile)
    {
        string powershell = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.Windows), "System32", "WindowsPowerShell", "v1.0", "powershell.exe");
        string args =
            "-NoProfile -ExecutionPolicy Bypass -File \"" + Path.Combine(tempDir, "install.ps1") + "\"" +
            " -PayloadZip \"" + Path.Combine(tempDir, "payload.zip") + "\"" +
            " -TargetUserAppData \"" + userAppData + "\"" +
            " -ShortcutFolder \"" + shortcutBox.Text + "\"" +
            " -LogFile \"" + logFile + "\"" +
            (allUsersShortcut.Checked ? " -ShortcutsForAllUsers" : "");

        ProcessStartInfo info = new ProcessStartInfo
        {
            FileName = powershell,
            Arguments = args,
            UseShellExecute = true,
            Verb = "runas",
            WindowStyle = ProcessWindowStyle.Hidden,
            WorkingDirectory = tempDir
        };
        try
        {
            using (Process process = Process.Start(info))
            {
                if (process == null) return 1223;
                process.WaitForExit();
                return process.ExitCode;
            }
        }
        catch (Win32Exception ex)
        {
            if (ex.NativeErrorCode == 1223)
            {
                throw new InvalidOperationException("用户取消了管理员授权 (UAC)。");
            }
            throw;
        }
    }

    private static string ReadLogSafe(string path)
    {
        try
        {
            if (File.Exists(path))
            {
                return File.ReadAllText(path, Encoding.UTF8);
            }
        }
        catch
        {
        }
        return null;
    }

    private void AppendLog(string text)
    {
        if (text == null) return;
        if (InvokeRequired)
        {
            BeginInvoke(new Action<string>(AppendLog), text);
            return;
        }
        logBox.AppendText(text + Environment.NewLine);
    }

    private static void ExtractResource(string resourceName, string outputPath)
    {
        Assembly assembly = Assembly.GetExecutingAssembly();
        using (Stream input = assembly.GetManifestResourceStream(resourceName))
        {
            if (input == null)
            {
                throw new InvalidOperationException("Missing installer resource: " + resourceName);
            }
            using (FileStream output = File.Create(outputPath))
            {
                input.CopyTo(output);
            }
        }
    }
}
