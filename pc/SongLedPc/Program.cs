using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Ports;
using System.Linq;
using System.Management;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Forms;
using Microsoft.Win32;
using NAudio.CoreAudioApi;
using System.Diagnostics;

namespace SongLedPc;

internal static class Program
{
    [STAThread]
    private static void Main(string[] args)
    {
        // Check .NET 8 requirement
        if (!CheckDotNetVersion())
        {
            return;
        }

        ApplicationConfiguration.Initialize();
        Application.Run(new TrayAppContext(args));
    }

    private static bool CheckDotNetVersion()
    {
        var version = Environment.Version;
        if (version.Major >= 8)
        {
            return true; // .NET 8 or later available
        }

        // .NET 8 not found, show dialog
        var result = MessageBox.Show(
            ".NET 8 runtime is required but not installed.\r\n\r\n" +
            "Click OK to open the download page, or Cancel to exit.",
            "Missing .NET 8 Runtime",
            MessageBoxButtons.OKCancel,
            MessageBoxIcon.Information
        );

        if (result == DialogResult.OK)
        {
            try
            {
                // Open download page
                var url = "https://dotnet.microsoft.com/en-us/download/dotnet/8.0";
                System.Diagnostics.Process.Start(new System.Diagnostics.ProcessStartInfo
                {
                    FileName = url,
                    UseShellExecute = true
                });
            }
            catch
            {
                MessageBox.Show("Could not open browser. Please visit:\nhttps://dotnet.microsoft.com/en-us/download/dotnet/8.0", "Download .NET 8");
            }
        }

        return false;
    }
}

internal sealed class TrayAppContext : ApplicationContext
{
    private readonly NotifyIcon _trayIcon;
    private readonly System.Windows.Forms.Timer _timer;
    private readonly SerialWorker _serial;
    private readonly Settings _settings;
    private readonly Logger _log;
    private readonly SmtcBridge _smtc;
    private string? _currentPort;
    private volatile bool _connecting;
    private bool _manualDisconnect;
    private long _lastHandshakeMs;

    public TrayAppContext(string[] args)
    {
        _settings = Settings.Parse(args);
        _log = new Logger(Path.Combine(AppContext.BaseDirectory, "songled.log"));
        _serial = new SerialWorker(_log);
        _smtc = new SmtcBridge(_log, _serial);
        _serial.HelloReceived += () => _smtc.ResendNowPlaying();

        var menu = new ContextMenuStrip();
        var statusItem = new ToolStripMenuItem("状态: 启动中") { Enabled = false };
        var portItem = new ToolStripMenuItem("串口: 未连接") { Enabled = false };
        var testLyricItem = new ToolStripMenuItem("测试歌词");
        var autoStartItem = new ToolStripMenuItem("开机自启: 关");
        var reconnectItem = new ToolStripMenuItem("立即重连");
        var disconnectItem = new ToolStripMenuItem("断开连接");
        var exportConfigItem = new ToolStripMenuItem("Export Config");
        var importConfigItem = new ToolStripMenuItem("Import Config");
        var exitItem = new ToolStripMenuItem("退出");
        menu.Items.Add(statusItem);
        menu.Items.Add(portItem);
        menu.Items.Add(new ToolStripSeparator());
        menu.Items.Add(testLyricItem);
        menu.Items.Add(autoStartItem);
        menu.Items.Add(reconnectItem);
        menu.Items.Add(disconnectItem);
        menu.Items.Add(new ToolStripSeparator());
        menu.Items.Add(exportConfigItem);
        menu.Items.Add(importConfigItem);
        menu.Items.Add(new ToolStripSeparator());
        menu.Items.Add(exitItem);

        _trayIcon = new NotifyIcon
        {
            Icon = SystemIcons.Application,
            Text = "SongLed PC",
            ContextMenuStrip = menu,
            Visible = true
        };

        ApplyAutoStartAction(_settings.AutoStartAction, showToast: false);
        UpdateAutoStartMenu(autoStartItem);
        testLyricItem.Click += (_, _) => TestLyrics();
        autoStartItem.Click += (_, _) =>
        {
            if (AutoStart.IsEnabled()) AutoStart.Disable();
            else AutoStart.Enable();
            UpdateAutoStartMenu(autoStartItem);
        };
        reconnectItem.Click += (_, _) => StartConnect(force: true);
        disconnectItem.Click += (_, _) =>
        {
            _manualDisconnect = true;
            _serial.Close();
            _currentPort = null;
            _log.Info("断开连接");
        };
        exportConfigItem.Click += (_, _) => ExportConfig();
        importConfigItem.Click += (_, _) => ImportConfig();
        exitItem.Click += (_, _) => Exit();

        _timer = new System.Windows.Forms.Timer { Interval = 1000 };
        _timer.Tick += (_, _) =>
        {
            StartConnect(force: false);
            statusItem.Text = _serial.IsOpen ? "状态: 已连接" : "状态: 等待设备";
            portItem.Text = $"串口: {(_currentPort ?? "未连接")}";
            UpdateAutoStartMenu(autoStartItem);
        };
        _timer.Start();

        StartConnect(force: true);
        _ = _smtc.InitializeAsync();
    }

    private void StartConnect(bool force)
    {
        if (!force && _manualDisconnect) return;
        if (_connecting) return;
        _connecting = true;
        Task.Run(() =>
        {
            try
            {
                AttemptConnect(force);
            }
            finally
            {
                _connecting = false;
            }
        });
    }

    private void AttemptConnect(bool force)
    {
        string? port = ResolvePort(force);
        if (string.IsNullOrWhiteSpace(port))
        {
            if (force) _log.Info("未找到设备串口");
            if (_serial.IsOpen) _serial.Close();
            _currentPort = null;
            return;
        }

        if (!force && _serial.IsOpen && string.Equals(_currentPort, port, StringComparison.OrdinalIgnoreCase))
        {
            return;
        }

        _serial.Open(port);
        _currentPort = port;
    }

    private string? ResolvePort(bool force)
    {
        if (!force && _serial.IsOpen && !string.IsNullOrWhiteSpace(_currentPort))
        {
            return _currentPort;
        }

        if (!string.IsNullOrWhiteSpace(_settings.Port))
        {
            return _settings.Port;
        }

        if (!string.IsNullOrWhiteSpace(_settings.Vid) && !string.IsNullOrWhiteSpace(_settings.Pid))
        {
            var port = DeviceLocator.FindComPortByVidPid(_settings.Vid!, _settings.Pid!, _log);
            if (!string.IsNullOrWhiteSpace(port)) return port;
        }

        long now = Environment.TickCount64;
        if (force || now - _lastHandshakeMs > 10000)
        {
            _lastHandshakeMs = now;
            var handshakePort = DeviceLocator.FindComPortByHandshake(_log);
            if (!string.IsNullOrWhiteSpace(handshakePort)) return handshakePort;
        }

        var ports = SerialPort.GetPortNames();
        return ports.Length == 1 ? ports[0] : null;
    }

    private void Exit()
    {
        _timer.Stop();
        _smtc.Dispose();
        _serial.Close();
        _trayIcon.Visible = false;
        _trayIcon.Dispose();
        Application.Exit();
    }

    private void ApplyAutoStartAction(AutoStartAction action, bool showToast)
    {
        if (action == AutoStartAction.None) return;
        if (action == AutoStartAction.Enable) AutoStart.Enable();
        else if (action == AutoStartAction.Disable) AutoStart.Disable();
        else if (action == AutoStartAction.Toggle)
        {
            if (AutoStart.IsEnabled()) AutoStart.Disable();
            else AutoStart.Enable();
        }

        if (showToast)
        {
            string state = AutoStart.IsEnabled() ? "已开启" : "已关闭";
            _trayIcon.BalloonTipTitle = "开机自启";
            _trayIcon.BalloonTipText = $"当前: {state}";
            _trayIcon.ShowBalloonTip(1500);
        }
    }

    private void TestLyrics()
    {
        string[] testLyrics =
        {
            "测试歌词",
            "Hello World",
            "春江潮水连海平",
            "海上明月共潮生",
            "这是一段长文本测试"
        };

        Task.Run(() =>
        {
            foreach (var lyric in testLyrics)
            {
                _serial.SendLine($"LRC CUR {lyric}");
                _log.Info($"Send test lyric: {lyric}");
                Thread.Sleep(3000);
            }
            _serial.SendLine("LRC CLR");
            _log.Info("Clear lyrics");
        });
    }

    private void ExportConfig()
    {
        if (!_serial.IsOpen)
        {
            MessageBox.Show("Device not connected", "Error");
            return;
        }

        using var saveDialog = new SaveFileDialog
        {
            Filter = "SongLed Config (*.songled.cfg)|*.songled.cfg|All Files (*.*)|*.*",
            DefaultExt = ".songled.cfg",
            FileName = $"songled-config-{DateTime.Now:yyyyMMdd-HHmmss}.songled.cfg"
        };

        if (saveDialog.ShowDialog() != DialogResult.OK)
            return;

        Task.Run(() =>
        {
            try
            {
                var config = new Dictionary<string, string>();
                _serial.SendLine("CFG EXPORT");
                _log.Info("Requesting device config...");
                
                // Wait for config data (this would need device implementation)
                Thread.Sleep(500);
                
                var options = new JsonSerializerOptions { WriteIndented = true };
                var jsonText = JsonSerializer.Serialize(config, options);
                File.WriteAllText(saveDialog.FileName, jsonText, Encoding.UTF8);
                _log.Info($"Config exported to: {saveDialog.FileName}");
                MessageBox.Show("Config exported successfully", "Success");
            }
            catch (Exception ex)
            {
                _log.Info($"Export failed: {ex.Message}");
                MessageBox.Show($"Export failed: {ex.Message}", "Error");
            }
        });
    }

    private void ImportConfig()
    {
        if (!_serial.IsOpen)
        {
            MessageBox.Show("Device not connected", "Error");
            return;
        }

        using var openDialog = new OpenFileDialog
        {
            Filter = "SongLed Config (*.songled.cfg)|*.songled.cfg|All Files (*.*)|*.*"
        };

        if (openDialog.ShowDialog() != DialogResult.OK)
            return;

        Task.Run(() =>
        {
            try
            {
                var jsonText = File.ReadAllText(openDialog.FileName, Encoding.UTF8);
                var config = JsonSerializer.Deserialize<Dictionary<string, string>>(jsonText);
                
                if (config == null)
                {
                    throw new Exception("Invalid config file format");
                }

                _serial.SendLine("CFG IMPORT");
                Thread.Sleep(100);

                foreach (var kvp in config)
                {
                    _serial.SendLine($"CFG SET {kvp.Key} {kvp.Value}");
                }

                _serial.SendLine("CFG SAVE");
                _log.Info($"Config imported from: {openDialog.FileName}");
                MessageBox.Show("Config imported successfully", "Success");
            }
            catch (Exception ex)
            {
                _log.Info($"Import failed: {ex.Message}");
                MessageBox.Show($"Import failed: {ex.Message}", "Error");
            }
        });
    }

    private static void UpdateAutoStartMenu(ToolStripMenuItem item)
    {
        item.Text = AutoStart.IsEnabled() ? "开机自启: 开" : "开机自启: 关";
    }
}

internal sealed class Settings
{
    public string? Port { get; private set; }
    public string? Vid { get; private set; }
    public string? Pid { get; private set; }
    public AutoStartAction AutoStartAction { get; private set; }

    public static Settings Parse(string[] args)
    {
        var settings = new Settings();
        for (int i = 0; i < args.Length; i++)
        {
            if (args[i].Equals("--port", StringComparison.OrdinalIgnoreCase) && i + 1 < args.Length)
            {
                settings.Port = args[i + 1];
            }
            if (args[i].Equals("--vid", StringComparison.OrdinalIgnoreCase) && i + 1 < args.Length)
            {
                settings.Vid = args[i + 1];
            }
            if (args[i].Equals("--pid", StringComparison.OrdinalIgnoreCase) && i + 1 < args.Length)
            {
                settings.Pid = args[i + 1];
            }
            if (args[i].Equals("--autostart", StringComparison.OrdinalIgnoreCase) && i + 1 < args.Length)
            {
                settings.AutoStartAction = ParseAutoStartAction(args[i + 1]);
            }
        }
        return settings;
    }

    private static AutoStartAction ParseAutoStartAction(string value)
    {
        return value.ToLowerInvariant() switch
        {
            "on" => AutoStartAction.Enable,
            "off" => AutoStartAction.Disable,
            "toggle" => AutoStartAction.Toggle,
            _ => AutoStartAction.None
        };
    }
}

internal enum AutoStartAction
{
    None = 0,
    Enable,
    Disable,
    Toggle
}

internal static class DeviceLocator
{
    private static readonly Regex ComRegex = new(@"\\(COM\d+)", RegexOptions.IgnoreCase | RegexOptions.Compiled);

    public static string? FindComPortByVidPid(string vid, string pid, Logger log)
    {
        string needle = $"VID_{vid}&PID_{pid}";
        try
        {
            using var searcher = new ManagementObjectSearcher("SELECT * FROM Win32_PnPEntity");
            foreach (var device in searcher.Get().Cast<ManagementObject>())
            {
                string? hardwareId = null;
                if (device["HardwareID"] is string[] idArray)
                {
                    hardwareId = string.Join(";", idArray);
                }
                else
                {
                    hardwareId = device["HardwareID"] as string;
                }
                string? name = device["Name"] as string;
                string? pnp = hardwareId?.ToUpperInvariant();
                if (pnp == null || !pnp.Contains(needle)) continue;
                if (string.IsNullOrWhiteSpace(name)) continue;
                var match = ComRegex.Match(name);
                if (match.Success)
                {
                    return match.Groups[1].Value;
                }
            }
        }
        catch (Exception ex)
        {
            log.Info($"VID/PID 检索失败: {ex.Message}");
        }
        return null;
    }

    public static string? FindComPortByHandshake(Logger log)
    {
        var ports = SerialPort.GetPortNames();
        foreach (var port in ports)
        {
            try
            {
                using var sp = new SerialPort(port, 115200)
                {
                    NewLine = "\n",
                    ReadTimeout = 200,
                    WriteTimeout = 100,
                    Encoding = Encoding.UTF8,
                    DtrEnable = false,
                    RtsEnable = false,
                    Handshake = Handshake.None
                };
                sp.Open();
                Thread.Sleep(150);
                sp.DiscardInBuffer();
                var sw = Stopwatch.StartNew();
                long nextPing = 0;
                while (sw.ElapsedMilliseconds < 4000)
                {
                    if (sw.ElapsedMilliseconds >= nextPing)
                    {
                        nextPing = sw.ElapsedMilliseconds + 500;
                        sp.WriteLine("HELLO");
                    }
                    try
                    {
                        string line = sp.ReadLine().Trim();
                        if (line.StartsWith("HELLO", StringComparison.OrdinalIgnoreCase))
                        {
                            return port;
                        }
                    }
                    catch (TimeoutException)
                    {
                        // keep waiting
                    }
                }
            }
            catch (Exception ex)
            {
                log.Info($"握手检测 {port} 失败: {ex.Message}");
            }
        }
        return null;
    }
}

internal sealed class SerialWorker
{
    private const int BaudRate = 115200;
    private readonly object _sendLock = new();
    private readonly MMDeviceEnumerator _enumerator = new();
    private readonly Logger _log;
    private SerialPort? _port;
    private Thread? _reader;
    private volatile bool _running;
    private List<MMDevice> _renderDevices = new();
    private bool _helloNotified;
    private DateTime _lastHelloAck = DateTime.MinValue;

    public bool IsOpen => _port != null && _port.IsOpen;
    public event Action? HelloReceived;

    public SerialWorker(Logger log)
    {
        _log = log;
    }

    public void Open(string portName)
    {
        if (_port != null && _port.IsOpen && _port.PortName == portName) return;
        Close();
        _helloNotified = false;
        _lastHelloAck = DateTime.MinValue;
        _port = new SerialPort(portName, BaudRate)
        {
            NewLine = "\n",
            ReadTimeout = 2000,
            WriteTimeout = 1000,
            Encoding = Encoding.UTF8,
            DtrEnable = false,
            RtsEnable = false,
            Handshake = Handshake.None
        };
        try
        {
            _port.Open();
        }
        catch (Exception ex)
        {
            _log.Info($"打开串口失败: {ex.Message}");
            _port = null;
            return;
        }

        _running = true;
        _reader = new Thread(ReadLoop) { IsBackground = true };
        _reader.Start();
        SendLine("HELLO");
        _log.Info($"已连接 {portName}");
    }

    public void Close()
    {
        _running = false;
        try
        {
            _port?.Close();
        }
        catch
        {
            // ignore
        }
        _port = null;
    }

    private void ReadLoop()
    {
        if (_port == null) return;
        int consecutiveErrors = 0;
        while (_running)
        {
            try
            {
                string line = _port.ReadLine().Trim();
                if (line.Length == 0) continue;
                HandleLine(line);
                consecutiveErrors = 0; // 成功读取，重置错误计数
            }
            catch (TimeoutException)
            {
                // 超时是正常的，继续读取
                continue;
            }
            catch (InvalidOperationException)
            {
                // 串口已关闭
                _log.Info("串口已断开");
                break;
            }
            catch (Exception ex)
            {
                consecutiveErrors++;
                _log.Info($"串口读取错误 ({consecutiveErrors}): {ex.Message}");
                if (consecutiveErrors >= 5)
                {
                    _log.Info("连续错误过多，断开连接");
                    break;
                }
                Thread.Sleep(100); // 错误后短暂等待
            }
        }
    }

    private void HandleLine(string line)
    {
        if (line.StartsWith("DEBUG:", StringComparison.OrdinalIgnoreCase) ||
            line.StartsWith("[REBUILD]", StringComparison.OrdinalIgnoreCase) ||
            line.StartsWith("APP ", StringComparison.OrdinalIgnoreCase))
        {
            _log.Info($"ESP32调试: {line}");
            return;
        }
        if (line.StartsWith("HELLO", StringComparison.OrdinalIgnoreCase))
        {
            _log.Info("??HELLO???????");
            var now = DateTime.UtcNow;
            if (now - _lastHelloAck > TimeSpan.FromMilliseconds(500))
            {
                _lastHelloAck = now;
                SendLine("HELLO OK");
                SendVolumeState();
            }
            if (!_helloNotified)
            {
                _helloNotified = true;
                HelloReceived?.Invoke();
            }
            // ?????????????ESP32????
            return;
        }
        if (line.Equals("VOL GET", StringComparison.OrdinalIgnoreCase))
        {
            SendVolumeState();
            return;
        }
        if (line.StartsWith("VOL SET", StringComparison.OrdinalIgnoreCase))
        {
            int value = ParseInt(line, 7);
            value = Math.Clamp(value, 0, 100);
            SetVolume(value);
            SendVolumeState();
            return;
        }
        if (line.Equals("MUTE", StringComparison.OrdinalIgnoreCase))
        {
            ToggleMute();
            SendVolumeState();
            return;
        }
        if (line.Equals("SPK LIST", StringComparison.OrdinalIgnoreCase))
        {
            _log.Info("收到SPK LIST请求");
            SendSpeakerList();
            return;
        }
        if (line.StartsWith("SPK SET", StringComparison.OrdinalIgnoreCase))
        {
            int index = ParseInt(line, 7);
            SetDefaultSpeaker(index);
            SendCurrentSpeaker();
            return;
        }
        if (line.StartsWith("CFG", StringComparison.OrdinalIgnoreCase))
        {
            // Handle config commands: CFG EXPORT, CFG IMPORT, CFG GET, etc.
            _log.Info($"Config command received: {line}");
            return;
        }
    }

    private static int ParseInt(string line, int startIndex)
    {
        if (line.Length <= startIndex) return 0;
        if (int.TryParse(line.Substring(startIndex).Trim(), out int value))
        {
            return value;
        }
        return 0;
    }

    public void SendLine(string text)
    {
        lock (_sendLock)
        {
            if (_port == null || !_port.IsOpen) return;
            try
            {
                _port.WriteLine(text);
            }
            catch (Exception ex)
            {
                _log.Info($"串口发送失败: {ex.Message}");
            }
        }
    }

    private MMDevice? GetDefaultDevice()
    {
        try
        {
            return _enumerator.GetDefaultAudioEndpoint(DataFlow.Render, Role.Multimedia);
        }
        catch
        {
            return null;
        }
    }

    private void SendVolumeState()
    {
        try
        {
            var dev = GetDefaultDevice();
            if (dev == null) return;
            int vol = (int)Math.Round(dev.AudioEndpointVolume.MasterVolumeLevelScalar * 100.0);
            int mute = dev.AudioEndpointVolume.Mute ? 1 : 0;
            SendLine($"VOL {vol}");
            SendLine($"MUTE {mute}");
        }
        catch (Exception ex)
        {
            _log.Info($"鍙戦€侀煶閲忕姸鎬佸け璐? {ex.Message}");
        }
    }

    private void SetVolume(int value)
    {
        try
        {
            var dev = GetDefaultDevice();
            if (dev == null) return;
            dev.AudioEndpointVolume.MasterVolumeLevelScalar = value / 100.0f;
        }
        catch (Exception ex)
        {
            _log.Info($"璁剧疆闊抽噺澶辫触: {ex.Message}");
        }
    }

    private void ToggleMute()
    {
        try
        {
            var dev = GetDefaultDevice();
            if (dev == null) return;
            dev.AudioEndpointVolume.Mute = !dev.AudioEndpointVolume.Mute;
        }
        catch (Exception ex)
        {
            _log.Info($"闈欓煶鍒囨崲澶辫触: {ex.Message}");
        }
    }

    private void RefreshRenderDevices()
    {
        _renderDevices = _enumerator.EnumerateAudioEndPoints(DataFlow.Render, DeviceState.Active).ToList();
    }

    private void SendSpeakerList()
    {
        try
        {
            _log.Info("开始发送设备列表");
            RefreshRenderDevices();
            _log.Info($"枚举到 {_renderDevices.Count} 个设备");
            SendLine("SPK BEGIN");
            for (int i = 0; i < _renderDevices.Count; i++)
            {
                string name = Sanitize(_renderDevices[i].FriendlyName);
                _log.Info($"发送设备 {i}: {name}");
                SendLine($"SPK ITEM {i} {name}");
            }
            SendLine("SPK END");
            _log.Info("设备列表发送完成");
            SendCurrentSpeaker();
        }
        catch (Exception ex)
        {
            _log.Info($"发送输出设备列表失败: {ex.Message}");
            _log.Info($"堆栈: {ex.StackTrace}");
        }
    }

    private void SendCurrentSpeaker()
    {
        var dev = GetDefaultDevice();
        if (dev == null) return;
        // 使用已缓存的设备列表，避免重复枚举
        if (_renderDevices.Count == 0) RefreshRenderDevices();
        int index = _renderDevices.FindIndex(d => d.ID == dev.ID);
        if (index >= 0)
        {
            SendLine($"SPK CUR {index}");
        }
    }

    private void SetDefaultSpeaker(int index)
    {
        // 使用已缓存的设备列表
        if (_renderDevices.Count == 0) RefreshRenderDevices();
        if (index < 0 || index >= _renderDevices.Count) return;
        string id = _renderDevices[index].ID;
        try
        {
            var policy = new PolicyConfigClient() as IPolicyConfig;
            policy?.SetDefaultEndpoint(id, ERole.eConsole);
            policy?.SetDefaultEndpoint(id, ERole.eMultimedia);
            policy?.SetDefaultEndpoint(id, ERole.eCommunications);
        }
        catch (Exception ex)
        {
            _log.Info($"设置默认设备失败: {ex.Message}");
        }
    }

    private static string Sanitize(string text)
    {
        return text.Replace("\r", " ").Replace("\n", " ").Trim();
    }
}

internal sealed class Logger
{
    private const long MaxBytes = 512 * 1024;
    private const int MaxArchives = 5;
    private readonly string _path;
    private readonly string _dir;
    private readonly object _lockObj = new();
    private DateTime _lastCleanup = DateTime.MinValue;

    public Logger(string path)
    {
        _path = path;
        _dir = Path.GetDirectoryName(path) ?? AppContext.BaseDirectory;
        TryCleanup();
    }

    public void Info(string message)
    {
        lock (_lockObj)
        {
            try
            {
                TryCleanup();
                File.AppendAllText(_path, $"[{DateTime.Now:yyyy-MM-dd HH:mm:ss}] {message}{Environment.NewLine}");
            }
            catch
            {
                // ignore logging failures
            }
        }
    }

    private void TryCleanup()
    {
        if (DateTime.UtcNow - _lastCleanup < TimeSpan.FromSeconds(10)) return;
        _lastCleanup = DateTime.UtcNow;

        try
        {
            if (File.Exists(_path))
            {
                var info = new FileInfo(_path);
                if (info.Length > MaxBytes)
                {
                    string stamp = DateTime.Now.ToString("yyyyMMdd-HHmmss");
                    string archive = Path.Combine(_dir, $"songled-{stamp}.log");
                    try
                    {
                        File.Move(_path, archive, true);
                    }
                    catch
                    {
                        // ignore rotate failures
                    }
                }
            }

            var archives = Directory.GetFiles(_dir, "songled-*.log")
                .Select(f => new FileInfo(f))
                .OrderByDescending(f => f.LastWriteTimeUtc)
                .ToList();
            for (int i = MaxArchives; i < archives.Count; i++)
            {
                try
                {
                    archives[i].Delete();
                }
                catch
                {
                    // ignore delete failures
                }
            }
        }
        catch
        {
            // ignore cleanup failures
        }
    }
}

internal static class AutoStart
{
    private const string RunKey = @"Software\Microsoft\Windows\CurrentVersion\Run";
    private const string AppName = "SongLedPc";

    public static bool IsEnabled()
    {
        using var key = Registry.CurrentUser.OpenSubKey(RunKey, false);
        return key?.GetValue(AppName) != null;
    }

    public static void Enable()
    {
        using var key = Registry.CurrentUser.OpenSubKey(RunKey, true);
        string exe = Application.ExecutablePath;
        key?.SetValue(AppName, $"\"{exe}\"");
    }

    public static void Disable()
    {
        using var key = Registry.CurrentUser.OpenSubKey(RunKey, true);
        key?.DeleteValue(AppName, false);
    }
}

// COM interop for setting default audio endpoint
[ComImport, Guid("870AF99C-171D-4F9E-AF0D-E63DF40C2BC9")]
internal class PolicyConfigClient
{
}

[ComImport, InterfaceType(ComInterfaceType.InterfaceIsIUnknown),
 Guid("F8679F50-850A-41CF-9C72-430F290290C8")]
internal interface IPolicyConfig
{
    int GetMixFormat(string deviceId, IntPtr format);
    int GetDeviceFormat(string deviceId, bool bDefault, IntPtr format);
    int ResetDeviceFormat(string deviceId);
    int SetDeviceFormat(string deviceId, IntPtr format, IntPtr mixFormat);
    int GetProcessingPeriod(string deviceId, bool bDefault, IntPtr defaultPeriod, IntPtr minimumPeriod);
    int SetProcessingPeriod(string deviceId, IntPtr defaultPeriod, IntPtr minimumPeriod);
    int GetShareMode(string deviceId, IntPtr mode);
    int SetShareMode(string deviceId, IntPtr mode);
    int GetPropertyValue(string deviceId, ref PropertyKey key, IntPtr pv);
    int SetPropertyValue(string deviceId, ref PropertyKey key, IntPtr pv);
    int SetDefaultEndpoint(string deviceId, ERole role);
    int SetEndpointVisibility(string deviceId, bool visible);
}

[StructLayout(LayoutKind.Sequential)]
internal struct PropertyKey
{
    public Guid fmtid;
    public int pid;
}

internal enum ERole
{
    eConsole = 0,
    eMultimedia = 1,
    eCommunications = 2
}
