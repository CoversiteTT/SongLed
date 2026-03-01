using System;
using System.IO;
using System.Diagnostics;
using System.IO.Ports;
using System.Windows.Forms;
using System.Linq;
using System.Threading.Tasks;
using System.Text.RegularExpressions;
using System.Management;
using System.Collections.Generic;

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

internal class PortInfo
{
    public string PortName { get; set; } = "";
    public string DeviceName { get; set; } = "";
    public string DisplayName => string.IsNullOrEmpty(DeviceName) ? PortName : $"{PortName} - {DeviceName}";
    public bool IsESP32 { get; set; }

    public override string ToString() => DisplayName;
}

internal sealed class FlasherForm : Form
{
    private readonly ComboBox _comPortCombo;
    private readonly Button _selectFirmwareButton;
    private readonly Button _flashButton;
    private readonly TextBox _firmwarePathBox;
    private readonly TextBox _statusBox;
    private readonly ProgressBar _progressBar;
    private string? _selectedFirmwareFile;
    private System.Windows.Forms.Timer? _portRefreshTimer;
    private Dictionary<string, PortInfo> _portInfoCache = new();
    private string? _lastSelectedPort;
    private bool _isListeningMode = false;
    private Button? _listenButton;
    private List<string> _previousPorts = new();

    private sealed class EsptoolCommand
    {
        public bool UsePython { get; set; }
        public string ToolPath { get; set; } = "";
        public string? PythonPath { get; set; }
    }

    public FlasherForm()
    {
        Text = "SongLed Flasher";
        Width = 650;
        Height = 650;
        StartPosition = FormStartPosition.CenterScreen;
        FormBorderStyle = FormBorderStyle.FixedSingle;
        MaximizeBox = false;

        var panel = new TableLayoutPanel
        {
            Dock = DockStyle.Fill,
            Padding = new Padding(10),
            RowCount = 12,
            ColumnCount = 2,
            AutoSize = false
        };
        panel.RowStyles.Add(new RowStyle(SizeType.Absolute, 80));  // Instructions
        panel.RowStyles.Add(new RowStyle(SizeType.Absolute, 30));  // Port label/combo
        panel.RowStyles.Add(new RowStyle(SizeType.Absolute, 30));  // Listen button row
        panel.RowStyles.Add(new RowStyle(SizeType.Absolute, 30));  // Firmware
        panel.RowStyles.Add(new RowStyle(SizeType.Absolute, 5));   // Spacer
        panel.RowStyles.Add(new RowStyle(SizeType.Absolute, 40));  // Flash button
        panel.RowStyles.Add(new RowStyle(SizeType.Absolute, 25));  // Progress bar
        panel.RowStyles.Add(new RowStyle(SizeType.Absolute, 30));  // Log label
        panel.RowStyles.Add(new RowStyle(SizeType.Percent, 100)); // Status box

        // Instructions
        var instructionsLabel = new Label 
        { 
            Text = "Instructions: Step 1: Click 'Listen' button first. Step 2: Plug in your ESP32 device. Step 3: Select merged firmware .bin file (write to 0x0). Step 4: Click 'Start Flashing'.",
            Dock = DockStyle.Fill,
            AutoSize = false,
            TextAlign = ContentAlignment.TopLeft,
            Font = new Font(Font.FontFamily, 9),
            Padding = new Padding(5)
        };
        instructionsLabel.MaximumSize = new Size(600, 80);

        // COM Port Selection
        var portLabel = new Label { Text = "COM Port:" };
        _comPortCombo = new ComboBox 
        { 
            Dock = DockStyle.Fill, 
            DropDownStyle = ComboBoxStyle.DropDownList 
        };
        RefreshComPorts();

        // Listen Button Row
        _listenButton = new Button 
        { 
            Text = "Listen for Device",
            Dock = DockStyle.Fill,
            BackColor = System.Drawing.Color.LightBlue,
            Font = new Font(Font.FontFamily, 10, FontStyle.Bold)
        };
        _listenButton.Click += (_, _) => ToggleListenMode();

        // Firmware File Selection
        var firmwareLabel = new Label { Text = "Merged Firmware (.bin):" };
        var firmwarePanel = new FlowLayoutPanel { Dock = DockStyle.Fill, AutoSize = false, Height = 30 };
        _selectFirmwareButton = new Button { Text = "Select", Width = 80, Height = 24 };
        _selectFirmwareButton.Click += (_, _) => SelectFirmwareFile();
        _firmwarePathBox = new TextBox { ReadOnly = true, Dock = DockStyle.Fill, Text = "(none)" };
        firmwarePanel.Controls.Add(_selectFirmwareButton);
        firmwarePanel.Controls.Add(_firmwarePathBox);

        // Flash Button
        _flashButton = new Button 
        { 
            Text = "Start Flashing", 
            Height = 40, 
            Font = new Font(Font.FontFamily, 12, FontStyle.Bold),
            BackColor = System.Drawing.Color.LimeGreen,
            ForeColor = System.Drawing.Color.White
        };
        _flashButton.Click += (_, _) => FlashFirmware();

        // Progress Bar
        _progressBar = new ProgressBar { Dock = DockStyle.Fill, Height = 25 };

        // Status Output
        var statusLabel = new Label { Text = "Log Output:" };
        _statusBox = new TextBox 
        { 
            Dock = DockStyle.Fill, 
            Multiline = true, 
            ReadOnly = true,
            ScrollBars = ScrollBars.Vertical,
            Font = new Font("Courier New", 9)
        };

        // Add controls to panel
        panel.Controls.Add(instructionsLabel, 0, 0);
        panel.SetColumnSpan(instructionsLabel, 2);

        panel.Controls.Add(portLabel, 0, 1);
        panel.Controls.Add(_comPortCombo, 1, 1);

        panel.Controls.Add(_listenButton, 0, 2);
        panel.SetColumnSpan(_listenButton, 2);

        panel.Controls.Add(firmwareLabel, 0, 3);
        panel.Controls.Add(firmwarePanel, 1, 3);

        panel.Controls.Add(_flashButton, 0, 5);
        panel.SetColumnSpan(_flashButton, 2);

        panel.Controls.Add(_progressBar, 0, 6);
        panel.SetColumnSpan(_progressBar, 2);

        panel.Controls.Add(statusLabel, 0, 7);
        panel.SetColumnSpan(statusLabel, 2);

        panel.Controls.Add(_statusBox, 0, 8);
        panel.SetColumnSpan(_statusBox, 2);
        _statusBox.Height = 200;

        Controls.Add(panel);

        Load += (_, _) =>
        {
            RefreshComPorts();
            _previousPorts = _portInfoCache.Keys.ToList();
            DetectAndSelectESP32Port();
            AutoDetectMergedFirmwareFile();
            
            // Start auto-refresh timer (every 1 second)
            _portRefreshTimer = new System.Windows.Forms.Timer();
            _portRefreshTimer.Interval = 1000;
            _portRefreshTimer.Tick += (_, _) => AutoRefreshComPorts();
            _portRefreshTimer.Start();
        };
        
        FormClosing += (_, _) =>
        {
            _portRefreshTimer?.Stop();
            _portRefreshTimer?.Dispose();
        };
    }

    private void SelectFirmwareFile()
    {
        using var dialog = new OpenFileDialog
        {
            Filter = "Merged Firmware (*.bin)|*.bin|All Files (*.*)|*.*",
            Title = "Select Merged Firmware Binary"
        };

        if (dialog.ShowDialog() == DialogResult.OK)
        {
            _selectedFirmwareFile = dialog.FileName;
            _firmwarePathBox.Text = Path.GetFileName(_selectedFirmwareFile);
            AppendLog($"Selected merged firmware: {Path.GetFileName(_selectedFirmwareFile)}");
        }
    }

    private void AutoDetectMergedFirmwareFile()
    {
        try
        {
            string appDir = Path.GetDirectoryName(AppContext.BaseDirectory) ?? string.Empty;
            string[] candidates =
            {
                Path.Combine(appDir, "songled_v0.0.2_merged.bin"),
                Path.Combine(appDir, "songled_merged.bin"),
                Path.Combine(appDir, "merged.bin")
            };

            string? found = candidates.FirstOrDefault(File.Exists);
            if (string.IsNullOrEmpty(found))
            {
                found = Directory.EnumerateFiles(appDir, "*merged*.bin", SearchOption.TopDirectoryOnly).FirstOrDefault();
            }
            if (string.IsNullOrEmpty(found))
            {
                found = Directory.EnumerateFiles(appDir, "*.bin", SearchOption.TopDirectoryOnly).FirstOrDefault();
            }
            if (!string.IsNullOrEmpty(found))
            {
                _selectedFirmwareFile = found;
                _firmwarePathBox.Text = $"{Path.GetFileName(found)} (auto)";
                AppendLog($"Auto-detected merged firmware: {Path.GetFileName(found)}");
            }
        }
        catch
        {
            // Ignore
        }
    }

    private void RefreshComPorts()
    {
        var ports = SerialPort.GetPortNames();
        _comPortCombo.Items.Clear();
        _portInfoCache.Clear();

        PortInfo? esp32Port = null;

        foreach (var port in ports)
        {
            var portInfo = GetPortInfo(port);
            _portInfoCache[port] = portInfo;
            _comPortCombo.Items.Add(portInfo);

            if (portInfo.IsESP32)
            {
                esp32Port = portInfo;
            }
        }

        // Auto-select ESP32 if found, otherwise select first port
        if (esp32Port != null)
        {
            _comPortCombo.SelectedItem = esp32Port;
            _lastSelectedPort = esp32Port.PortName;
            AppendLog($"Auto-selected ESP32 on {esp32Port.PortName}");
        }
        else if (_comPortCombo.Items.Count > 0)
        {
            _comPortCombo.SelectedIndex = 0;
            var selected = _comPortCombo.SelectedItem as PortInfo;
            if (selected != null) _lastSelectedPort = selected.PortName;
        }
    }

    private void AutoRefreshComPorts()
    {
        try
        {
            var currentPorts = SerialPort.GetPortNames();
            
            // 鍦ㄧ洃鍚ā寮忎笅锛屼笌蹇収姣斿锛涘惁鍒欎笌缂撳瓨姣斿
            var previousPorts = _isListeningMode ? _previousPorts.ToArray() : _portInfoCache.Keys.ToArray();
            
            // Check if port list changed
            if (currentPorts.SequenceEqual(previousPorts))
            {
                return; // No changes
            }

            // Save currently selected port
            var selectedPort = (_comPortCombo.SelectedItem as PortInfo)?.PortName;

            // Find new ports (added)
            var newPorts = currentPorts.Where(p => !previousPorts.Contains(p)).ToList();

            // Remove disconnected ports from cache
            foreach (var port in _portInfoCache.Keys.Where(p => !currentPorts.Contains(p)).ToList())
            {
                _portInfoCache.Remove(port);
            }

            // Add new ports and refresh display
            _comPortCombo.Items.Clear();

            PortInfo? esp32Port = null;
            PortInfo? newESP32Port = null;

            foreach (var port in currentPorts)
            {
                PortInfo portInfo;
                if (!_portInfoCache.ContainsKey(port))
                {
                    portInfo = GetPortInfo(port);
                    _portInfoCache[port] = portInfo;
                    
                    // Check if this is a new port
                    if (newPorts.Contains(port))
                    {
                        newESP32Port = portInfo;
                    }
                }
                else
                {
                    portInfo = _portInfoCache[port];
                }

                _comPortCombo.Items.Add(portInfo);

                if (portInfo.IsESP32)
                {
                    esp32Port = portInfo;
                }
            }

            // If listening mode is active and a new device was inserted
            if (_isListeningMode && newESP32Port != null)
            {
                _comPortCombo.SelectedItem = newESP32Port;
                AppendLog($"[LISTEN MODE] Detected new device: {newESP32Port.PortName} - {newESP32Port.DeviceName}");
                AppendLog("Device auto-selected. Ready to flash!");
                _lastSelectedPort = newESP32Port.PortName;
                
                // Update snapshot to avoid re-detecting same device
                _previousPorts = currentPorts.ToList();
                
                // Auto-exit listening mode
                ToggleListenMode();
            }
            // Normal mode: Restore or auto-select ESP32
            else if (!_isListeningMode)
            {
                if (!string.IsNullOrEmpty(selectedPort) && _portInfoCache.ContainsKey(selectedPort))
                {
                    _comPortCombo.SelectedItem = _portInfoCache[selectedPort];
                }
                else if (esp32Port != null)
                {
                    _comPortCombo.SelectedItem = esp32Port;
                    if (_lastSelectedPort != esp32Port.PortName)
                    {
                        _lastSelectedPort = esp32Port.PortName;
                        AppendLog($"Detected ESP32 on {esp32Port.PortName} - {esp32Port.DeviceName}");
                    }
                }
                else if (_comPortCombo.Items.Count > 0)
                {
                    _comPortCombo.SelectedIndex = 0;
                    var selected = _comPortCombo.SelectedItem as PortInfo;
                    if (selected != null) _lastSelectedPort = selected.PortName;
                }
            }
        }
        catch
        {
            // Silently ignore errors in auto-refresh
        }
    }

    private void ToggleListenMode()
    {
        if (_listenButton == null) return;

        _isListeningMode = !_isListeningMode;

        if (_isListeningMode)
        {
            _listenButton.BackColor = System.Drawing.Color.LightCoral;
            _listenButton.Text = "Stop Listening (Listening...)";
            _listenButton.ForeColor = System.Drawing.Color.White;
            _statusBox.Clear();
            AppendLog("[LISTEN MODE] Waiting for device insertion...");
            AppendLog("Please connect your ESP32 device...");
            _previousPorts = _portInfoCache.Keys.ToList();
        }
        else
        {
            _listenButton.BackColor = System.Drawing.Color.LightBlue;
            _listenButton.Text = "Listen for Device";
            _listenButton.ForeColor = System.Drawing.Color.Black;
            AppendLog("[LISTEN MODE] Stopped listening");
        }
    }

    private PortInfo GetPortInfo(string portName)
    {
        var portInfo = new PortInfo { PortName = portName };

        try
        {
            using (var searcher = new ManagementObjectSearcher(
                "SELECT * FROM Win32_PnPEntity WHERE Description LIKE '%COM%' OR Name LIKE '%COM%'"))
            {
                foreach (var device in searcher.Get())
                {
                    var pnpDeviceId = device.GetPropertyValue("PnPDeviceID")?.ToString() ?? "";
                    var name = device.GetPropertyValue("Name")?.ToString() ?? "";

                    // Extract COM port from name
                    var match = Regex.Match(name, @"(COM\d+)");
                    if (match.Success && match.Groups[1].Value == portName)
                    {
                        portInfo.DeviceName = name.Replace($"({portName})", "").Trim();

                        // Check if it's an ESP32 device
                        if (pnpDeviceId.Contains("10C4_EA60") || pnpDeviceId.Contains("1A86_7523") ||
                            name.Contains("USB-to-UART") || name.Contains("CH340") || name.Contains("CP210x"))
                        {
                            portInfo.IsESP32 = true;
                        }

                        break;
                    }
                }
            }
        }
        catch
        {
            // If WMI query fails, just use the port name
        }

        return portInfo;
    }

    private void DetectAndSelectESP32Port()
    {
        try
        {
            foreach (var portInfo in _portInfoCache.Values)
            {
                if (portInfo.IsESP32)
                {
                    _comPortCombo.SelectedItem = portInfo;
                    _lastSelectedPort = portInfo.PortName;
                    AppendLog($"Detected ESP32 on {portInfo.PortName} - {portInfo.DeviceName}");
                    return;
                }
            }
        }
        catch (Exception ex)
        {
            AppendLog($"Auto-detection note: {ex.Message}");
        }
    }

    private string? FindESP32Port()
    {
        try
        {
            foreach (var portInfo in _portInfoCache.Values)
            {
                if (portInfo.IsESP32)
                {
                    return portInfo.PortName;
                }
            }
        }
        catch
        {
            // Fallback
        }

        return null;
    }

    private void FlashFirmware()
    {
        if (string.IsNullOrEmpty(_selectedFirmwareFile))
        {
            MessageBox.Show("Please select a merged firmware file first", "Error");
            return;
        }

        if (!File.Exists(_selectedFirmwareFile))
        {
            MessageBox.Show("Merged firmware file not found", "Error");
            return;
        }

        var selectedPortInfo = _comPortCombo.SelectedItem as PortInfo;
        if (selectedPortInfo == null || string.IsNullOrEmpty(selectedPortInfo.PortName))
        {
            MessageBox.Show("Please select a COM port", "Error");
            return;
        }

        var esptool = ResolveEsptoolCommand();
        if (esptool == null)
        {
            string baseDir = AppContext.BaseDirectory;
            AppendLog("Error: no esptool backend found");
            MessageBox.Show($"Missing flasher backend.\nExpected one of:\n{Path.Combine(baseDir, "esptool.exe")}\n{Path.Combine(baseDir, "esptool.py")} + python.exe", "Missing esptool");
            return;
        }

        _flashButton.Enabled = false;
        _selectFirmwareButton.Enabled = false;
        _comPortCombo.Enabled = false;
        _progressBar.Value = 0;
        _statusBox.Clear();

        AppendLog($"Starting flash to {selectedPortInfo.PortName}...");
        AppendLog("Mode: single merged image");
        AppendLog($"Image: {Path.GetFileName(_selectedFirmwareFile)}");

        AppendLog(esptool.UsePython ? $"Backend: python + {Path.GetFileName(esptool.ToolPath)}" : $"Backend: {Path.GetFileName(esptool.ToolPath)}");
        Task.Run(() => FlashFirmwareAsync(esptool, selectedPortInfo.PortName));
    }

    private async Task FlashFirmwareAsync(EsptoolCommand esptool, string comPort)
    {
        try
        {
            AppendLog("Flashing merged image (0x0)...");
            await FlashFileAsync(esptool, comPort, "0x0", _selectedFirmwareFile!);
            UpdateProgress(100);
            AppendLog("FLASH COMPLETE");
            MessageBox.Show("Merged firmware flashed successfully.", "Success");
        }
        catch (Exception ex)
        {
            AppendLog($"ERROR: {ex.Message}");
            MessageBox.Show($"Flash failed: {ex.Message}", "Error");
        }
        finally
        {
            if (InvokeRequired)
            {
                Invoke(() =>
                {
                    _flashButton.Enabled = true;
                    _selectFirmwareButton.Enabled = true;
                    _comPortCombo.Enabled = true;
                });
            }
            else
            {
                _flashButton.Enabled = true;
                _selectFirmwareButton.Enabled = true;
                _comPortCombo.Enabled = true;
            }
        }
    }

    private async Task FlashFileAsync(EsptoolCommand esptool, string comPort, string address, string filePath)
    {
        string args = $"-p {comPort} -b 460800 --chip esp32s3 write_flash {address} \"{filePath}\"";
        var psi = new ProcessStartInfo
        {
            FileName = esptool.UsePython ? esptool.PythonPath! : esptool.ToolPath,
            Arguments = esptool.UsePython ? $"\"{esptool.ToolPath}\" {args}" : args,
            UseShellExecute = false,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            CreateNoWindow = true
        };

        using var process = Process.Start(psi);
        if (process == null) throw new Exception("Failed to start esptool.exe");

        process.OutputDataReceived += (_, e) =>
        {
            if (!string.IsNullOrEmpty(e.Data))
            {
                AppendLog(e.Data);

                // Parse progress: "Writing at 0x00001234 (12% done)"
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
                AppendLog($"[ERROR] {e.Data}");
        };

        process.BeginOutputReadLine();
        process.BeginErrorReadLine();

        await Task.Run(() => process.WaitForExit());

        if (process.ExitCode != 0)
        {
            throw new Exception($"esptool exit code: {process.ExitCode}");
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

    private void AppendLog(string message)
    {
        if (InvokeRequired)
        {
            Invoke(() => AppendLog(message));
            return;
        }

        _statusBox.AppendText($"[{DateTime.Now:HH:mm:ss}] {message}\r\n");
    }

    private static EsptoolCommand? ResolveEsptoolCommand()
    {
        string baseDir = AppContext.BaseDirectory;
        string exePath = Path.Combine(baseDir, "esptool.exe");
        if (File.Exists(exePath))
        {
            return new EsptoolCommand { UsePython = false, ToolPath = exePath };
        }

        string pyPath = Path.Combine(baseDir, "esptool.py");
        if (File.Exists(pyPath))
        {
            string? python = ResolvePythonCommand(baseDir);
            if (!string.IsNullOrEmpty(python))
            {
                return new EsptoolCommand
                {
                    UsePython = true,
                    ToolPath = pyPath,
                    PythonPath = python
                };
            }
        }

        return null;
    }

    private static string? ResolvePythonCommand(string baseDir)
    {
        string localPython = Path.Combine(baseDir, "python.exe");
        if (File.Exists(localPython)) return localPython;

        string? envPython = Environment.GetEnvironmentVariable("PYTHON");
        if (!string.IsNullOrEmpty(envPython) && File.Exists(envPython)) return envPython;

        string? pathPython = FindInPath("python.exe");
        if (!string.IsNullOrEmpty(pathPython)) return pathPython;

        return null;
    }

    private static string? FindInPath(string fileName)
    {
        string? pathEnv = Environment.GetEnvironmentVariable("PATH");
        if (string.IsNullOrEmpty(pathEnv)) return null;
        foreach (string dir in pathEnv.Split(Path.PathSeparator))
        {
            try
            {
                if (string.IsNullOrWhiteSpace(dir)) continue;
                string candidate = Path.Combine(dir.Trim(), fileName);
                if (File.Exists(candidate)) return candidate;
            }
            catch
            {
                // ignore bad path entry
            }
        }
        return null;
    }
}

