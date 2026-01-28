using System;
using System.IO;
using System.Diagnostics;
using System.IO.Ports;
using System.Windows.Forms;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using System.Text.RegularExpressions;

namespace SongLedFlasher;

internal static class Program
{
    [STAThread]
    private static void Main()
    {
        ApplicationConfiguration.Initialize();
        Application.Run(new FlasherForm());
    }
}

internal sealed class FlasherForm : Form
{
    private readonly ComboBox _comPortCombo;
    private readonly Button _selectFileButton;
    private readonly Button _flashButton;
    private readonly Button _fullFlashButton;
    private readonly TextBox _statusBox;
    private readonly ProgressBar _progressBar;
    private string? _selectedBinFile;

    public FlasherForm()
    {
        Text = "SongLed 烧录工具";
        Width = 500;
        Height = 480;
        StartPosition = FormStartPosition.CenterScreen;
        FormBorderStyle = FormBorderStyle.FixedSingle;
        MaximizeBox = false;

        var panel = new TableLayoutPanel
        {
            Dock = DockStyle.Fill,
            Padding = new Padding(10),
            RowCount = 7,
            ColumnCount = 2
        };

        // COM 端口选择
        var portLabel = new Label { Text = "COM 端口:" };
        _comPortCombo = new ComboBox { Dock = DockStyle.Fill, DropDownStyle = ComboBoxStyle.DropDownList };
        RefreshComPorts();

        // 固件文件选择
        var fileLabel = new Label { Text = "固件文件:" };
        var filePanel = new FlowLayoutPanel { Dock = DockStyle.Fill, AutoSize = true };
        _selectFileButton = new Button { Text = "选择 .bin 文件", Width = 150 };
        _selectFileButton.Click += (_, _) => SelectBinFile();
        var fileTextBox = new TextBox { ReadOnly = true, Dock = DockStyle.Fill, Text = "未选择" };
        
        // 保存选择的文件引用
        _selectFileButton.Click += (_, _) => fileTextBox.Text = _selectedBinFile ?? "未选择";

        // 烧录按钮
        _flashButton = new Button { Text = "快速烧录 (仅应用)", Height = 40, Font = new Font(Font.FontFamily, 12, FontStyle.Bold) };
        _flashButton.Click += (_, _) => FlashFirmware(false);
        
        // 完整烧录按钮
        _fullFlashButton = new Button { Text = "完整烧录 (含引导程序)", Height = 40, Font = new Font(Font.FontFamily, 12, FontStyle.Bold) };
        _fullFlashButton.Click += (_, _) => FlashFirmware(true);

        // 进度条
        _progressBar = new ProgressBar { Dock = DockStyle.Fill, Height = 25 };

        // 状态输出
        var statusLabel = new Label { Text = "状态输出:" };
        _statusBox = new TextBox 
        { 
            Dock = DockStyle.Fill, 
            Multiline = true, 
            ReadOnly = true,
            ScrollBars = ScrollBars.Vertical
        };

        panel.Controls.Add(portLabel, 0, 0);
        panel.Controls.Add(_comPortCombo, 1, 0);

        panel.Controls.Add(fileLabel, 0, 1);
        panel.Controls.Add(_selectFileButton, 1, 1);

        panel.Controls.Add(_flashButton, 0, 2);
        panel.SetColumnSpan(_flashButton, 2);

        panel.Controls.Add(_fullFlashButton, 0, 3);
        panel.SetColumnSpan(_fullFlashButton, 2);

        panel.Controls.Add(_progressBar, 0, 4);
        panel.SetColumnSpan(_progressBar, 2);

        panel.Controls.Add(statusLabel, 0, 5);
        panel.SetColumnSpan(statusLabel, 2);

        panel.Controls.Add(_statusBox, 0, 6);
        panel.SetColumnSpan(_statusBox, 2);

        Controls.Add(panel);

        Load += (_, _) => RefreshComPorts();
    }

    private void SelectBinFile()
    {
        using var dialog = new OpenFileDialog
        {
            Filter = "固件文件 (*.bin)|*.bin|所有文件 (*.*)|*.*",
            Title = "选择固件文件"
        };

        if (dialog.ShowDialog() == DialogResult.OK)
        {
            _selectedBinFile = dialog.FileName;
            AppendStatus($"已选择: {Path.GetFileName(_selectedBinFile)}");
        }
    }

    private void RefreshComPorts()
    {
        var ports = SerialPort.GetPortNames();
        _comPortCombo.Items.Clear();
        _comPortCombo.Items.AddRange(ports.Cast<object>().ToArray());
        if (ports.Length > 0) _comPortCombo.SelectedIndex = 0;
    }

    private void FlashFirmware(bool fullFlash)
    {
        if (string.IsNullOrEmpty(_selectedBinFile))
        {
            MessageBox.Show("请先选择固件文件", "错误");
            return;
        }

        if (!File.Exists(_selectedBinFile))
        {
            MessageBox.Show("固件文件不存在", "错误");
            return;
        }

        var comPort = _comPortCombo.SelectedItem?.ToString();
        if (string.IsNullOrEmpty(comPort))
        {
            MessageBox.Show("请选择 COM 端口", "错误");
            return;
        }

        // 验证 esptool.exe 存在
        string esptoolPath = Path.Combine(AppContext.BaseDirectory, "esptool.exe");
        if (!File.Exists(esptoolPath))
        {
            AppendStatus($"错误: esptool.exe 未找到: {esptoolPath}");
            MessageBox.Show($"esptool.exe 不存在！\n\n期望位置: {esptoolPath}", "缺少 esptool");
            return;
        }

        _flashButton.Enabled = false;
        _fullFlashButton.Enabled = false;
        _progressBar.Value = 0;
        _statusBox.Clear();

        if (fullFlash)
        {
            // 完整烧录：检查 bootloader.bin
            string bootloaderPath = Path.Combine(Path.GetDirectoryName(AppContext.BaseDirectory) ?? "", "bootloader.bin");
            if (!File.Exists(bootloaderPath))
            {
                AppendStatus($"错误: bootloader.bin 未找到");
                MessageBox.Show($"bootloader.bin 不存在！", "缺少 bootloader");
                _flashButton.Enabled = true;
                _fullFlashButton.Enabled = true;
                return;
            }
            AppendStatus($"完整烧录: bootloader + 应用");
            AppendStatus($"Bootloader: {bootloaderPath}");
            AppendStatus($"应用: {Path.GetFileName(_selectedBinFile)}");
            Task.Run(() => FlashFirmwareAsync(esptoolPath, comPort, bootloaderPath));
        }
        else
        {
            AppendStatus($"快速烧录: 仅应用程序");
            AppendStatus($"固件: {Path.GetFileName(_selectedBinFile)}");
            Task.Run(() => FlashFirmwareAsync(esptoolPath, comPort, null));
        }
    }

    private async Task FlashFirmwareAsync(string esptoolPath, string comPort, string? bootloaderPath)
    {
        try
        {
            // 如果需要完整烧录，先烧录 bootloader
            if (!string.IsNullOrEmpty(bootloaderPath) && File.Exists(bootloaderPath))
            {
                AppendStatus("正在烧录引导程序...");
                await FlashFileAsync(esptoolPath, comPort, "0x0", bootloaderPath);
                AppendStatus("✓ 引导程序烧录完成");
            }

            // 烧录应用
            AppendStatus("正在烧录应用程序...");
            await FlashFileAsync(esptoolPath, comPort, "0x10000", _selectedBinFile);
            AppendStatus("✓ 应用程序烧录完成");

            UpdateProgress(100);
            AppendStatus("✓ 烧录成功！");
            MessageBox.Show("固件烧录完成！", "成功");
        }
        catch (Exception ex)
        {
            AppendStatus($"✗ 异常: {ex.Message}");
            MessageBox.Show($"错误: {ex.Message}", "错误");
        }
        finally
        {
            if (InvokeRequired)
            {
                Invoke(() =>
                {
                    _flashButton.Enabled = true;
                    _fullFlashButton.Enabled = true;
                });
            }
            else
            {
                _flashButton.Enabled = true;
                _fullFlashButton.Enabled = true;
            }
        }
    }

    private async Task FlashFileAsync(string esptoolPath, string comPort, string address, string filePath)
    {
        var psi = new ProcessStartInfo
        {
            FileName = esptoolPath,
            Arguments = $"-p {comPort} -b 460800 --chip esp32s3 write_flash {address} \"{filePath}\"",
            UseShellExecute = false,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            CreateNoWindow = true
        };

        using var process = Process.Start(psi);
        if (process == null) throw new Exception("无法启动 esptool.exe");

        process.OutputDataReceived += (_, e) =>
        {
            if (!string.IsNullOrEmpty(e.Data))
            {
                AppendStatus(e.Data);

                // 解析进度: "Writing at 0x00001234 (12% done)"
                var match = Regex.Match(e.Data, @"(\d+)%\s+done");
                if (match.Success && int.TryParse(match.Groups[1].Value, out int percent))
                {
                    UpdateProgress(percent);
                }
            }
        };

        process.ErrorDataReceived += (_, e) =>
        {
            if (!string.IsNullOrEmpty(e.Data))
                AppendStatus($"错误: {e.Data}");
        };

        process.BeginOutputReadLine();
        process.BeginErrorReadLine();

        await Task.Run(() => process.WaitForExit());

        if (process.ExitCode != 0)
        {
            throw new Exception($"烧录失败，退出码: {process.ExitCode}");
        }
    }

    private void UpdateProgress(int value)
    {
        if (InvokeRequired)
        {
            Invoke(() => UpdateProgress(value));
            return;
        }

        _progressBar.Value = Math.Min(100, Math.Max(0, value));
    }

    private void AppendStatus(string message)
    {
        if (InvokeRequired)
        {
            Invoke(() => AppendStatus(message));
            return;
        }

        _statusBox.AppendText($"[{DateTime.Now:HH:mm:ss}] {message}\r\n");
    }
}
