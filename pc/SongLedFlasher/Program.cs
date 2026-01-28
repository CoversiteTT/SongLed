using System;
using System.IO;
using System.Diagnostics;
using System.IO.Ports;
using System.Windows.Forms;
using System.Linq;
using System.Threading.Tasks;
using System.Text.RegularExpressions;
using System.Management;

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
    private readonly Button _selectBootloaderButton;
    private readonly Button _flashButton;
    private readonly TextBox _firmwarePathBox;
    private readonly TextBox _bootloaderPathBox;
    private readonly TextBox _statusBox;
    private readonly ProgressBar _progressBar;
    private string? _selectedFirmwareFile;
    private string? _selectedBootloaderFile;
    private System.Windows.Forms.Timer? _portRefreshTimer;
    private Dictionary<string, PortInfo> _portInfoCache = new();
    private string? _lastSelectedPort;
    private bool _isListeningMode = false;
    private Button? _listenButton;
    private List<string> _previousPorts = new();

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
        panel.RowStyles.Add(new RowStyle(SizeType.Absolute, 30));  // Bootloader
        panel.RowStyles.Add(new RowStyle(SizeType.Absolute, 5));   // Spacer
        panel.RowStyles.Add(new RowStyle(SizeType.Absolute, 40));  // Flash button
        panel.RowStyles.Add(new RowStyle(SizeType.Absolute, 25));  // Progress bar
        panel.RowStyles.Add(new RowStyle(SizeType.Absolute, 30));  // Log label
        panel.RowStyles.Add(new RowStyle(SizeType.Percent, 100)); // Status box

        // Instructions
        var instructionsLabel = new Label 
        { 
            Text = "Instructions: Step 1: Click 'Listen' button first. Step 2: Plug in your ESP32 device. Step 3: Select firmware .bin file. Step 4: Click 'Start Flashing'. If flashing fails, try including bootloader file.",
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

        var listenInfo = new Label 
        { 
            Text = "(waiting for device...)",
            Dock = DockStyle.Fill,
            AutoSize = false,
            TextAlign = ContentAlignment.MiddleLeft,
            Font = new Font(Font.FontFamily, 9, FontStyle.Italic),
            ForeColor = System.Drawing.Color.Gray
        };

        // Firmware File Selection
        var firmwareLabel = new Label { Text = "Firmware (.bin):" };
        var firmwarePanel = new FlowLayoutPanel { Dock = DockStyle.Fill, AutoSize = false, Height = 30 };
        _selectFirmwareButton = new Button { Text = "Select", Width = 80, Height = 24 };
        _selectFirmwareButton.Click += (_, _) => SelectFirmwareFile();
        _firmwarePathBox = new TextBox { ReadOnly = true, Dock = DockStyle.Fill, Text = "(none)" };
        firmwarePanel.Controls.Add(_selectFirmwareButton);
        firmwarePanel.Controls.Add(_firmwarePathBox);

        // Bootloader File Selection
        var bootloaderLabel = new Label { Text = "Bootloader (.bin):" };
        var bootloaderPanel = new FlowLayoutPanel { Dock = DockStyle.Fill, AutoSize = false, Height = 30 };
        _selectBootloaderButton = new Button { Text = "Select", Width = 80, Height = 24 };
        _selectBootloaderButton.Click += (_, _) => SelectBootloaderFile();
        _bootloaderPathBox = new TextBox { ReadOnly = true, Dock = DockStyle.Fill, Text = "(none)" };
        bootloaderPanel.Controls.Add(_selectBootloaderButton);
        bootloaderPanel.Controls.Add(_bootloaderPathBox);

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

        panel.Controls.Add(bootloaderLabel, 0, 4);
        panel.Controls.Add(bootloaderPanel, 1, 4);

        panel.Controls.Add(_flashButton, 0, 6);
        panel.SetColumnSpan(_flashButton, 2);

        panel.Controls.Add(_progressBar, 0, 7);
        panel.SetColumnSpan(_progressBar, 2);

        panel.Controls.Add(statusLabel, 0, 8);
        panel.SetColumnSpan(statusLabel, 2);

        panel.Controls.Add(_statusBox, 0, 9);
        panel.SetColumnSpan(_statusBox, 2);
        _statusBox.Height = 200;

        Controls.Add(panel);

        Load += (_, _) =>
        {
            RefreshComPorts();
            _previousPorts = _portInfoCache.Keys.ToList();
            DetectAndSelectESP32Port();
            AutoDetectBootloaderFile();
            
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
            Filter = "Firmware Files (*.bin)|*.bin|All Files (*.*)|*.*",
            Title = "Select Firmware Binary"
        };

        if (dialog.ShowDialog() == DialogResult.OK)
        {
            _selectedFirmwareFile = dialog.FileName;
            _firmwarePathBox.Text = Path.GetFileName(_selectedFirmwareFile);
            AppendLog($"Selected firmware: {Path.GetFileName(_selectedFirmwareFile)}");
        }
    }

    private void SelectBootloaderFile()
    {
        using var dialog = new OpenFileDialog
        {
            Filter = "Bootloader Files (*.bin)|*.bin|All Files (*.*)|*.*",
            Title = "Select Bootloader Binary"
        };

        if (dialog.ShowDialog() == DialogResult.OK)
        {
            _selectedBootloaderFile = dialog.FileName;
            _bootloaderPathBox.Text = Path.GetFileName(_selectedBootloaderFile);
            AppendLog($"Selected bootloader: {Path.GetFileName(_selectedBootloaderFile)}");
        }
    }

    private void AutoDetectBootloaderFile()
    {
        try
        {
            string appDir = Path.GetDirectoryName(AppContext.BaseDirectory) ?? "";
            string bootloaderPath = Path.Combine(appDir, "bootloader.bin");
            if (File.Exists(bootloaderPath))
            {
                _selectedBootloaderFile = bootloaderPath;
                _bootloaderPathBox.Text = "bootloader.bin (auto)";
                AppendLog("Auto-detected bootloader.bin");
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
            
            // 在监听模式下，与快照比对；否则与缓存比对
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
            MessageBox.Show("Please select a firmware file first", "Error");
            return;
        }

        if (!File.Exists(_selectedFirmwareFile))
        {
            MessageBox.Show("Firmware file not found", "Error");
            return;
        }

        var selectedPortInfo = _comPortCombo.SelectedItem as PortInfo;
        if (selectedPortInfo == null || string.IsNullOrEmpty(selectedPortInfo.PortName))
        {
            MessageBox.Show("Please select a COM port", "Error");
            return;
        }

        string esptoolPath = Path.Combine(AppContext.BaseDirectory, "esptool.exe");
        if (!File.Exists(esptoolPath))
        {
            AppendLog($"Error: esptool.exe not found at {esptoolPath}");
            MessageBox.Show($"esptool.exe not found!\n\nExpected: {esptoolPath}", "Missing esptool");
            return;
        }

        _flashButton.Enabled = false;
        _selectFirmwareButton.Enabled = false;
        _selectBootloaderButton.Enabled = false;
        _comPortCombo.Enabled = false;
        _progressBar.Value = 0;
        _statusBox.Clear();

        bool hasBootloader = !string.IsNullOrEmpty(_selectedBootloaderFile) && File.Exists(_selectedBootloaderFile);
        AppendLog($"Starting flash to {selectedPortInfo.PortName}...");
        if (hasBootloader)
            AppendLog($"Bootloader: {Path.GetFileName(_selectedBootloaderFile)}");
        AppendLog($"Firmware: {Path.GetFileName(_selectedFirmwareFile)}");

        Task.Run(() => FlashFirmwareAsync(esptoolPath, selectedPortInfo.PortName, hasBootloader));
    }

    private async Task FlashFirmwareAsync(string esptoolPath, string comPort, bool hasBootloader)
    {
        try
        {
            int totalFiles = hasBootloader ? 2 : 1;
            int currentFile = 0;

            if (hasBootloader)
            {
                currentFile = 1;
                AppendLog("Flashing bootloader (0x0)...");
                await FlashFileAsync(esptoolPath, comPort, "0x0", _selectedBootloaderFile, currentFile, totalFiles);
                AppendLog("✓ Bootloader flashed successfully");
            }

            currentFile++;
            AppendLog("Flashing firmware (0x10000)...");
            await FlashFileAsync(esptoolPath, comPort, "0x10000", _selectedFirmwareFile, currentFile, totalFiles);
            AppendLog("✓ Firmware flashed successfully");

            UpdateProgress(100);
            AppendLog("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
            AppendLog("✓ FLASH COMPLETE!");
            AppendLog("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
            MessageBox.Show("Firmware flashed successfully!", "Success");
        }
        catch (Exception ex)
        {
            AppendLog($"✗ ERROR: {ex.Message}");
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
                    _selectBootloaderButton.Enabled = true;
                    _comPortCombo.Enabled = true;
                });
            }
            else
            {
                _flashButton.Enabled = true;
                _selectFirmwareButton.Enabled = true;
                _selectBootloaderButton.Enabled = true;
                _comPortCombo.Enabled = true;
            }
        }
    }

    private async Task FlashFileAsync(string esptoolPath, string comPort, string address, string filePath, int fileNum, int totalFiles)
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
        if (process == null) throw new Exception("Failed to start esptool.exe");

        int fileProgress = 0;
        int baseProgress = (fileNum - 1) * (100 / totalFiles);

        process.OutputDataReceived += (_, e) =>
        {
            if (!string.IsNullOrEmpty(e.Data))
            {
                AppendLog(e.Data);

                // Parse progress: "Writing at 0x00001234 (12% done)"
                var match = Regex.Match(e.Data, @"(\d+)%\s+done");
                if (match.Success && int.TryParse(match.Groups[1].Value, out int percent))
                {
                    fileProgress = percent;
                    int overallProgress = baseProgress + (fileProgress / totalFiles);
                    UpdateProgress(overallProgress);
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
}
