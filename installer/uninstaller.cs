using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Forms;

internal static class Program
{
    [STAThread]
    private static int Main()
    {
        Application.EnableVisualStyles();
        Application.SetCompatibleTextRenderingDefault(false);
        using (UninstallWizard wizard = new UninstallWizard())
        {
            Application.Run(wizard);
            return wizard.ExitCode;
        }
    }
}

internal sealed class UninstallWizard : Form
{
    private readonly Panel pageHost = new Panel();
    private readonly Button backButton = new Button();
    private readonly Button nextButton = new Button();
    private readonly Button cancelButton = new Button();
    private readonly Label statusLabel = new Label();
    private readonly ProgressBar progress = new ProgressBar();
    private readonly TextBox logBox = new TextBox();
    private bool running;
    private int pageIndex;
    private int exitCode = 1;

    public int ExitCode
    {
        get { return exitCode; }
    }

    public UninstallWizard()
    {
        Text = "Cidu ZW3D Agent 卸载程序";
        StartPosition = FormStartPosition.CenterScreen;
        ClientSize = new Size(680, 440);
        MinimumSize = new Size(640, 420);
        MaximizeBox = false;
        Font = new Font("Microsoft YaHei UI", 9F);
        Icon = Icon.ExtractAssociatedIcon(Application.ExecutablePath);

        pageHost.Dock = DockStyle.Fill;
        Controls.Add(pageHost);

        Panel buttons = new Panel();
        buttons.Dock = DockStyle.Bottom;
        buttons.Height = 58;
        buttons.BackColor = SystemColors.Control;
        FlowLayoutPanel buttonLayout = new FlowLayoutPanel();
        buttonLayout.Dock = DockStyle.Right;
        buttonLayout.Width = 340;
        buttonLayout.FlowDirection = FlowDirection.RightToLeft;
        buttonLayout.WrapContents = false;
        buttonLayout.Padding = new Padding(0, 14, 14, 0);
        backButton.Text = "< 返回(B)";
        nextButton.Text = "卸载(U)";
        cancelButton.Text = "取消(C)";
        backButton.Size = new Size(104, 30);
        nextButton.Size = new Size(104, 30);
        cancelButton.Size = new Size(92, 30);
        backButton.Margin = new Padding(4, 0, 4, 0);
        nextButton.Margin = new Padding(4, 0, 4, 0);
        cancelButton.Margin = new Padding(4, 0, 4, 0);
        buttonLayout.Controls.AddRange(new Control[] { cancelButton, nextButton, backButton });
        buttons.Controls.Add(buttonLayout);
        Controls.Add(buttons);

        backButton.Click += delegate { ShowPage(0); };
        nextButton.Click += delegate { Next(); };
        cancelButton.Click += delegate { if (!running) Close(); };
        FormClosing += delegate(object sender, FormClosingEventArgs e) { if (running) e.Cancel = true; };

        ShowPage(0);
    }

    private void ShowPage(int index)
    {
        pageIndex = index;
        pageHost.Controls.Clear();
        if (index == 0)
        {
            pageHost.Controls.Add(CreateConfirmPage());
            backButton.Enabled = false;
            nextButton.Text = "卸载(U)";
            nextButton.Enabled = true;
            cancelButton.Enabled = true;
        }
        else if (index == 1)
        {
            pageHost.Controls.Add(CreateProgressPage());
            backButton.Enabled = false;
            nextButton.Enabled = false;
            cancelButton.Enabled = false;
            BeginUninstall();
        }
        else
        {
            pageHost.Controls.Add(CreateFinishPage());
            backButton.Enabled = false;
            nextButton.Text = "完成(F)";
            nextButton.Enabled = true;
            cancelButton.Enabled = false;
        }
    }

    private void Next()
    {
        if (pageIndex == 2)
        {
            exitCode = 0;
            Close();
            return;
        }
        if (pageIndex == 0 && !EnsureZw3dClosedBeforeUninstall())
        {
            return;
        }
        ShowPage(pageIndex + 1);
    }

    private bool EnsureZw3dClosedBeforeUninstall()
    {
        Process[] runningProcesses = GetRunningZw3dProcesses();
        if (runningProcesses.Length == 0)
        {
            return true;
        }

        DialogResult result = MessageBox.Show(
            this,
            "检测到 ZW3D 正在运行。\r\n\r\n继续卸载需要关闭 ZW3D，否则插件文件和界面资源可能无法删除。\r\n是否强制退出 ZW3D 并继续卸载？\r\n\r\n请先确认当前模型和文件已经保存。",
            "关闭 ZW3D 后继续卸载",
            MessageBoxButtons.YesNo,
            MessageBoxIcon.Warning,
            MessageBoxDefaultButton.Button2);
        if (result != DialogResult.Yes)
        {
            DisposeProcesses(runningProcesses);
            return false;
        }

        try
        {
            CloseZw3dProcesses(runningProcesses);
            return true;
        }
        catch (Exception ex)
        {
            MessageBox.Show(this, "无法关闭 ZW3D：\r\n" + ex.Message + "\r\n\r\n请手动关闭 ZW3D 后再继续卸载。", "关闭 ZW3D 失败", MessageBoxButtons.OK, MessageBoxIcon.Error);
            return false;
        }
        finally
        {
            DisposeProcesses(runningProcesses);
        }
    }

    private static Process[] GetRunningZw3dProcesses()
    {
        List<Process> runningProcesses = new List<Process>();
        foreach (Process process in Process.GetProcesses())
        {
            try
            {
                string name = process.ProcessName;
                if (name.Equals("zw3d", StringComparison.OrdinalIgnoreCase) ||
                    name.StartsWith("ZW3D", StringComparison.OrdinalIgnoreCase))
                {
                    runningProcesses.Add(process);
                    continue;
                }
            }
            catch
            {
            }
            process.Dispose();
        }
        return runningProcesses.ToArray();
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

    private Control CreateConfirmPage()
    {
        Panel page = BasePage("卸载 Cidu ZW3D Agent", "此向导将从当前 ZW3D 环境中移除 Cidu ZW3D Agent。");
        AddBody(page, "继续前请关闭所有正在运行的 ZW3D 窗口。\r\n\r\n卸载将删除插件注册项、Ribbon 入口、插件文件以及开始菜单快捷方式。\r\n\r\n单击“卸载”继续。");
        return page;
    }

    private Control CreateProgressPage()
    {
        Panel page = BasePage("正在卸载", "请稍候，卸载向导正在移除 Cidu ZW3D Agent。");
        statusLabel.SetBounds(34, 112, 600, 24);
        statusLabel.Text = "正在执行卸载脚本...";
        progress.SetBounds(34, 144, 600, 22);
        progress.Style = ProgressBarStyle.Marquee;
        logBox.SetBounds(34, 184, 600, 150);
        logBox.Multiline = true;
        logBox.ReadOnly = true;
        logBox.ScrollBars = ScrollBars.Vertical;
        page.Controls.AddRange(new Control[] { statusLabel, progress, logBox });
        return page;
    }

    private Control CreateFinishPage()
    {
        Panel page = BasePage("卸载完成", "Cidu ZW3D Agent 已卸载完成。");
        AddBody(page, "已从当前 ZW3D 环境移除 Cidu ZW3D Agent。\r\n\r\n单击“完成”退出卸载向导。");
        return page;
    }

    private Panel BasePage(string title, string subtitle)
    {
        Panel page = new Panel();
        page.Dock = DockStyle.Fill;
        page.BackColor = Color.White;
        Panel header = new Panel();
        header.Dock = DockStyle.Top;
        header.Height = 82;
        header.BackColor = Color.FromArgb(245, 248, 252);
        Label titleLabel = NewLabel(title, 28, 16, 600, 28, true);
        titleLabel.Font = new Font(Font.FontFamily, 14F, FontStyle.Bold);
        Label subtitleLabel = NewLabel(subtitle, 28, 48, 600, 22, false);
        header.Controls.AddRange(new Control[] { titleLabel, subtitleLabel });
        page.Controls.Add(header);
        return page;
    }

    private static Label NewLabel(string text, int x, int y, int width, int height, bool bold)
    {
        Label label = new Label();
        label.Text = text;
        label.Location = new Point(x, y);
        label.Size = new Size(width, height);
        label.Font = new Font("Microsoft YaHei UI", 9F, bold ? FontStyle.Bold : FontStyle.Regular);
        label.AutoSize = false;
        return label;
    }

    private void AddBody(Control page, string text)
    {
        Label body = NewLabel(text, 34, 112, 600, 190, false);
        body.Font = new Font(Font.FontFamily, 10F);
        page.Controls.Add(body);
    }

    private async void BeginUninstall()
    {
        running = true;
        try
        {
            int code = await Task.Run(new Func<int>(RunUninstall));
            if (code != 0)
            {
                throw new InvalidOperationException("卸载脚本退出代码：" + code);
            }
            statusLabel.Text = "卸载完成。";
            progress.Style = ProgressBarStyle.Blocks;
            progress.Value = 100;
            running = false;
            ShowPage(2);
        }
        catch (Exception ex)
        {
            running = false;
            progress.Style = ProgressBarStyle.Blocks;
            statusLabel.Text = "卸载失败。";
            AppendLog(ex.Message);
            MessageBox.Show(this, ex.Message, "卸载失败", MessageBoxButtons.OK, MessageBoxIcon.Error);
            backButton.Enabled = true;
            nextButton.Text = "重试(U)";
            nextButton.Enabled = true;
            cancelButton.Enabled = true;
            pageIndex = 0;
        }
    }

    private int RunUninstall()
    {
        string installDir = AppDomain.CurrentDomain.BaseDirectory.TrimEnd(Path.DirectorySeparatorChar);
        string script = Path.Combine(installDir, "CiduZW3DInstaller", "uninstall.ps1");
        if (!File.Exists(script))
        {
            throw new FileNotFoundException("未找到卸载脚本。", script);
        }
        string powershell = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.Windows), "System32", "WindowsPowerShell", "v1.0", "powershell.exe");
        string args = "-NoProfile -ExecutionPolicy Bypass -File \"" + script + "\" -InstallDir \"" + installDir + "\"";
        ProcessStartInfo info = new ProcessStartInfo();
        info.FileName = powershell;
        info.Arguments = args;
        info.UseShellExecute = false;
        info.CreateNoWindow = true;
        info.RedirectStandardOutput = true;
        info.RedirectStandardError = true;
        info.WorkingDirectory = installDir;
        info.StandardOutputEncoding = Encoding.Default;
        info.StandardErrorEncoding = Encoding.Default;
        using (Process process = new Process())
        {
            process.StartInfo = info;
            process.OutputDataReceived += delegate(object sender, DataReceivedEventArgs e) { if (e.Data != null) AppendLog(e.Data); };
            process.ErrorDataReceived += delegate(object sender, DataReceivedEventArgs e) { if (e.Data != null) AppendLog(e.Data); };
            process.Start();
            process.BeginOutputReadLine();
            process.BeginErrorReadLine();
            process.WaitForExit();
            return process.ExitCode;
        }
    }

    private void AppendLog(string text)
    {
        if (InvokeRequired)
        {
            BeginInvoke(new Action<string>(AppendLog), text);
            return;
        }
        logBox.AppendText(text + Environment.NewLine);
    }
}
