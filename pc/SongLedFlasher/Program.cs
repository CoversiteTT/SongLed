using System;
using System.IO;
using System.Diagnostics;
using System.IO.Ports;
using System.Windows.Forms;
using System.Linq;

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
    private readonly TextBox _statusBox;
    private readonly ProgressBar _progressBar;
    private string? _selectedBinFile;

    public FlasherForm()
    {
        Text = "SongLed 烧录工具";
        Width = 500;
        Height = 400;
        StartPosition = FormStartPosition.CenterScreen;
        FormBorderStyle = FormBorderStyle.FixedSingle;
        MaximizeBox = false;

        var panel = new TableLayoutPanel
        {
            Dock = DockStyle.Fill,
            Padding = new Padding(10),
            RowCount = 6,
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
        _flashButton = new Button { Text = "开始烧录", Height = 40, Font = new Font(Font.FontFamily, 12, FontStyle.Bold) };
        _flashButton.Click += (_, _) => FlashFirmware();

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

        panel.Controls.Add(_progressBar, 0, 3);
        panel.SetColumnSpan(_progressBar, 2);

        panel.Controls.Add(statusLabel, 0, 4);
        panel.SetColumnSpan(statusLabel, 2);

        panel.Controls.Add(_statusBox, 0, 5);
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

    private void FlashFirmware()
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

        _flashButton.Enabled = false;
        _progressBar.Value = 0;
        _statusBox.Clear();

        AppendStatus($"正在烧录到 {comPort}...");
        AppendStatus($"固件: {Path.GetFileName(_selectedBinFile)}");

        try
        {
            // Call local esptool.exe
            string esptoolPath = Path.Combine(AppContext.BaseDirectory, "esptool.exe");
            
            if (!File.Exists(esptoolPath))
            {
                AppendStatus($"Error: esptool.exe not found at {esptoolPath}");
                MessageBox.Show($"esptool.exe not found!\n\nExpected location: {esptoolPath}", "Missing esptool");
                return;
            }

            var psi = new ProcessStartInfo
            {
                FileName = esptoolPath,
                Arguments = $"-p {comPort} -b 460800 --chip esp32s3 write_flash 0x0 \"{_selectedBinFile}\"",
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
                    if (e.Data.Contains("Writing at") || e.Data.Contains("Wrote"))
                    {
                        _progressBar.Value = Math.Min(100, _progressBar.Value + 5);
                    }
                }
            };

            process.ErrorDataReceived += (_, e) =>
            {
                if (!string.IsNullOrEmpty(e.Data))
                    AppendStatus($"Error: {e.Data}");
            };

            process.BeginOutputReadLine();
            process.BeginErrorReadLine();
            process.WaitForExit();

            if (process.ExitCode == 0)
            {
                _progressBar.Value = 100;
                AppendStatus("Success: Flash complete!");
                MessageBox.Show("Flash completed successfully!", "Success");
            }
            else
            {
                AppendStatus($"Flash failed (exit code: {process.ExitCode})");
                MessageBox.Show($"Flash failed with exit code: {process.ExitCode}", "Failed");
            }
        }
        catch (Exception ex)
        {
            AppendStatus($"Exception: {ex.Message}");
            MessageBox.Show($"Error: {ex.Message}", "Error");
        }
        finally
        {
            _flashButton.Enabled = true;
        }
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
