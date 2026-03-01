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
    private enum LinkMode
    {
        Auto = 0,
        UsbOnly,
        BleOnly
    }

    private readonly NotifyIcon _trayIcon;
    private readonly System.Windows.Forms.Timer _timer;
    private readonly SerialWorker _serial;
    private readonly Settings _settings;
    private readonly Logger _log;
    private readonly SmtcBridge _smtc;
    private readonly DeviceConfigManager _configManager;
    private string? _currentPort;
    private volatile bool _connecting;
    private bool _manualDisconnect;
    private long _lastHandshakeMs;
    private long _lastBleScanMs;
    private LinkMode _linkMode = LinkMode.Auto;
    private string? _manualPort;
    private string? _manualBleDeviceId;
    private string? _manualBleDeviceName;
    private readonly AppState _appState;
    private volatile bool _exiting;

    public TrayAppContext(string[] args)
    {
        _settings = Settings.Parse(args);
        _log = new Logger(Path.Combine(AppContext.BaseDirectory, "songled.log"));
        _serial = new SerialWorker(_log);
        _smtc = new SmtcBridge(_log, _serial);
        _configManager = new DeviceConfigManager(_serial, _log);
        _serial.HelloReceived += () => _smtc.ResendNowPlaying();
        _appState = AppState.Load();
        _manualPort = _appState.LastPort;
        _manualBleDeviceId = _appState.LastBleDeviceId;
        _manualBleDeviceName = _appState.LastBleDeviceName;
        if (Enum.IsDefined(typeof(LinkMode), _appState.LastLinkMode))
        {
            _linkMode = (LinkMode)_appState.LastLinkMode;
        }

        var menu = new ContextMenuStrip();
        var statusItem = new ToolStripMenuItem("状态: 启动中") { Enabled = false };
        var portItem = new ToolStripMenuItem("串口: 未连接") { Enabled = false };
        var linkMenu = new ToolStripMenuItem("连接设置");
        var linkAutoItem = new ToolStripMenuItem("模式: 自动");
        var linkUsbItem = new ToolStripMenuItem("模式: 仅USB");
        var linkBleItem = new ToolStripMenuItem("模式: 仅蓝牙");
        var linkPortMenu = new ToolStripMenuItem("手动串口");
        var linkBleDeviceMenu = new ToolStripMenuItem("手动蓝牙设备");
        var linkBleNowItem = new ToolStripMenuItem("立即蓝牙连接");
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
        menu.Items.Add(linkMenu);
        menu.Items.Add(testLyricItem);
        menu.Items.Add(autoStartItem);
        menu.Items.Add(reconnectItem);
        menu.Items.Add(disconnectItem);
        menu.Items.Add(new ToolStripSeparator());
        menu.Items.Add(exportConfigItem);
        menu.Items.Add(importConfigItem);
        menu.Items.Add(new ToolStripSeparator());
        menu.Items.Add(exitItem);

        linkMenu.DropDownItems.Add(linkAutoItem);
        linkMenu.DropDownItems.Add(linkUsbItem);
        linkMenu.DropDownItems.Add(linkBleItem);
        linkMenu.DropDownItems.Add(new ToolStripSeparator());
        linkMenu.DropDownItems.Add(linkPortMenu);
        linkMenu.DropDownItems.Add(linkBleDeviceMenu);
        linkMenu.DropDownItems.Add(linkBleNowItem);

        void SetLinkMode(LinkMode mode)
        {
            _linkMode = mode;
            linkAutoItem.Checked = mode == LinkMode.Auto;
            linkUsbItem.Checked = mode == LinkMode.UsbOnly;
            linkBleItem.Checked = mode == LinkMode.BleOnly;
            _appState.LastLinkMode = (int)mode;
            _appState.Save();
        }

        void RefreshPortMenu()
        {
            linkPortMenu.DropDownItems.Clear();
            var autoPortItem = new ToolStripMenuItem("自动检测")
            {
                Checked = string.IsNullOrWhiteSpace(_manualPort)
            };
            autoPortItem.Click += (_, _) =>
            {
                _manualPort = null;
                _appState.LastPort = null;
                _appState.Save();
                _manualDisconnect = false;
                StartConnect(force: true);
            };
            linkPortMenu.DropDownItems.Add(autoPortItem);
            linkPortMenu.DropDownItems.Add(new ToolStripSeparator());

            var ports = SerialPort.GetPortNames().OrderBy(p => p, StringComparer.OrdinalIgnoreCase).ToArray();
            if (ports.Length == 0)
            {
                linkPortMenu.DropDownItems.Add(new ToolStripMenuItem("(无串口)") { Enabled = false });
                return;
            }

            foreach (var p in ports)
            {
                var item = new ToolStripMenuItem(p)
                {
                    Checked = string.Equals(_manualPort, p, StringComparison.OrdinalIgnoreCase)
                };
                item.Click += (_, _) =>
                {
                    _manualPort = p;
                    _appState.LastPort = p;
                    _appState.Save();
                    _manualDisconnect = false;
                    StartConnect(force: true);
                };
                linkPortMenu.DropDownItems.Add(item);
            }
        }

        async Task RefreshBleDeviceMenuAsync()
        {
            linkBleDeviceMenu.DropDownItems.Clear();
            var loading = new ToolStripMenuItem("扫描中...") { Enabled = false };
            linkBleDeviceMenu.DropDownItems.Add(loading);

            IReadOnlyList<(string Id, string Name)> devices;
            try
            {
                devices = await _serial.ListBleDevicesAsync();
            }
            catch (Exception ex)
            {
                _log.Info($"BLE list failed: {ex.Message}");
                devices = Array.Empty<(string, string)>();
            }

            linkBleDeviceMenu.DropDownItems.Clear();
            var autoBleItem = new ToolStripMenuItem("自动检测")
            {
                Checked = string.IsNullOrWhiteSpace(_manualBleDeviceId)
            };
            autoBleItem.Click += (_, _) =>
            {
                _manualBleDeviceId = null;
                _manualBleDeviceName = null;
                _appState.LastBleDeviceId = null;
                _appState.LastBleDeviceName = null;
                _appState.Save();
                _manualDisconnect = false;
                StartConnect(force: true);
            };
            linkBleDeviceMenu.DropDownItems.Add(autoBleItem);
            linkBleDeviceMenu.DropDownItems.Add(new ToolStripSeparator());

            if (devices.Count == 0)
            {
                linkBleDeviceMenu.DropDownItems.Add(new ToolStripMenuItem("(未发现蓝牙设备)") { Enabled = false });
                return;
            }

            foreach (var d in devices)
            {
                var text = string.IsNullOrWhiteSpace(d.Name) ? d.Id : d.Name;
                var item = new ToolStripMenuItem(text)
                {
                    Checked = string.Equals(_manualBleDeviceId, d.Id, StringComparison.OrdinalIgnoreCase),
                    ToolTipText = d.Id
                };
                item.Click += (_, _) =>
                {
                    _manualBleDeviceId = d.Id;
                    _manualBleDeviceName = d.Name;
                    _appState.LastBleDeviceId = d.Id;
                    _appState.LastBleDeviceName = d.Name;
                    _appState.Save();
                    SetLinkMode(LinkMode.BleOnly);
                    _manualDisconnect = false;
                    StartConnect(force: true);
                };
                linkBleDeviceMenu.DropDownItems.Add(item);
            }
        }

        _trayIcon = new NotifyIcon
        {
            Icon = SystemIcons.Application,
            Text = "SongLed PC",
            ContextMenuStrip = menu,
            Visible = true
        };

        ApplyAutoStartAction(_settings.AutoStartAction, showToast: false);
        UpdateAutoStartMenu(autoStartItem);
        SetLinkMode(_linkMode);
        linkPortMenu.DropDownOpening += (_, _) => RefreshPortMenu();
        linkBleDeviceMenu.DropDownOpening += async (_, _) => await RefreshBleDeviceMenuAsync();
        linkAutoItem.Click += (_, _) =>
        {
            SetLinkMode(LinkMode.Auto);
            _manualDisconnect = false;
            StartConnect(force: true);
        };
        linkUsbItem.Click += (_, _) =>
        {
            SetLinkMode(LinkMode.UsbOnly);
            _manualDisconnect = false;
            StartConnect(force: true);
        };
        linkBleItem.Click += (_, _) =>
        {
            SetLinkMode(LinkMode.BleOnly);
            _manualDisconnect = false;
            StartConnect(force: true);
        };
        linkBleNowItem.Click += (_, _) =>
        {
            _manualDisconnect = false;
            StartConnect(force: true);
        };
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
            _serial.Close(fast: true);
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
            statusItem.Text = _serial.IsConnected
                ? (_serial.IsBleOpen ? "\u72B6\u6001: \u5DF2\u8FDE\u63A5(BLE)" : "\u72B6\u6001: \u5DF2\u8FDE\u63A5")
                : "\u72B6\u6001: \u7B49\u5F85\u8BBE\u5907";
            portItem.Text = $"串口: {(_currentPort ?? "未连接")}";
            UpdateAutoStartMenu(autoStartItem);
        };
        _timer.Start();

        StartConnect(force: true);
        _ = _smtc.InitializeAsync();
    }

    private void StartConnect(bool force)
    {
        if (_exiting) return;
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
        if (_exiting) return;

        if (force && _serial.IsOpen)
        {
            _serial.Close();
            _currentPort = null;
        }

        if (!force && _serial.IsConnected)
        {
            _currentPort = _serial.ConnectionLabel;
            return;
        }

        bool allowUsb = _linkMode != LinkMode.BleOnly;
        bool allowBle = _linkMode != LinkMode.UsbOnly;
        // Auto mode policy: wired first, BLE fallback.
        bool bleFirst = allowBle && (_linkMode == LinkMode.BleOnly);
        bool triedBle = false;
        string? port = null;

        if (bleFirst)
        {
            triedBle = true;
            if (TryConnectBle(force))
            {
                return;
            }
        }

        if (allowUsb)
        {
            port = ResolvePort(force);
            if (!string.IsNullOrWhiteSpace(port))
            {
                _serial.Open(port);
                if (_serial.IsUsbOpen)
                {
                    long start = Environment.TickCount64;
                    while (Environment.TickCount64 - start < 1200)
                    {
                        if (_serial.IsUsbReady)
                        {
                            _currentPort = _serial.ConnectionLabel;
                            _appState.LastPort = port;
                            _appState.LastConnected = DateTime.Now;
                            _appState.Save();
                            return;
                        }
                        Thread.Sleep(80);
                    }

                    _log.Info($"USB opened on {port} but no HELLO");
                    _serial.Close();
                }
            }
        }

        if (allowBle && !triedBle && TryConnectBle(force))
        {
            return;
        }

        if (string.IsNullOrWhiteSpace(port) && force)
        {
            _log.Info("No available link found");
        }

        if (_serial.IsOpen)
        {
            _serial.Close();
        }
        _currentPort = null;
    }

    private bool TryConnectBle(bool force)
    {
        long now = Environment.TickCount64;
        bool shouldTryBle = force || (now - _lastBleScanMs > 2000);
        if (!shouldTryBle) return false;

        _lastBleScanMs = now;
        string? targetId = _manualBleDeviceId ?? _appState.LastBleDeviceId;
        string targetName = _manualBleDeviceName ?? _appState.LastBleDeviceName ?? "SongLed";
        bool bleOk = _serial.OpenBleAsync(targetId, targetName).GetAwaiter().GetResult();
        if (!bleOk) return false;

        _currentPort = _serial.ConnectionLabel;
        _appState.LastConnected = DateTime.Now;
        _appState.LastBleDeviceId = _serial.BleDeviceId ?? targetId;
        _appState.LastBleDeviceName = _serial.BleDeviceName ?? targetName;
        _appState.Save();
        return true;
    }

    private string? ResolvePort(bool force)
    {
        if (!force && _serial.IsConnected && !string.IsNullOrWhiteSpace(_currentPort))
        {
            return _currentPort;
        }

        if (!string.IsNullOrWhiteSpace(_manualPort))
        {
            var portNames = SerialPort.GetPortNames();
            if (portNames.Any(p => string.Equals(p, _manualPort, StringComparison.OrdinalIgnoreCase)))
            {
                return _manualPort;
            }
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
        _exiting = true;
        _timer.Stop();
        _smtc.Dispose();
        _serial.Close(fast: true);
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
            MessageBox.Show("设备未连接", "错误");
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

        Task.Run(async () =>
        {
            try
            {
                _log.Info("正在导出设备配置...");
                
                // 从设备读取当前配置
                var deviceConfig = await _configManager.ReadConfigAsync();
                
                if (deviceConfig == null)
                {
                    _log.Info("无法读取设备配置，使用默认配置");
                    deviceConfig = new DeviceConfig();
                }

                // 创建导出格式
                var exported = _configManager.CreateExport(
                    deviceConfig,
                    _currentPort,
                    "1.0.0"
                );

                // 序列化为JSON
                var options = new JsonSerializerOptions 
                { 
                    WriteIndented = true,
                    DefaultIgnoreCondition = System.Text.Json.Serialization.JsonIgnoreCondition.WhenWritingNull
                };
                
                var jsonText = JsonSerializer.Serialize(exported, options);
                File.WriteAllText(saveDialog.FileName, jsonText, Encoding.UTF8);
                
                _log.Info($"配置已导出到: {saveDialog.FileName}");
                
                // 显示摘要信息
                var summary = exported.GetSummary();
                MessageBox.Show(
                    $"配置导出成功\n\n文件: {Path.GetFileName(saveDialog.FileName)}\n\n{summary}",
                    "导出完成",
                    MessageBoxButtons.OK,
                    MessageBoxIcon.Information
                );
            }
            catch (Exception ex)
            {
                _log.Info($"导出失败: {ex.Message}");
                MessageBox.Show($"导出失败: {ex.Message}", "错误");
            }
        });
    }

    private void ImportConfig()
    {
        if (!_serial.IsOpen)
        {
            MessageBox.Show("设备未连接", "错误");
            return;
        }

        using var openDialog = new OpenFileDialog
        {
            Filter = "SongLed Config (*.songled.cfg)|*.songled.cfg|All Files (*.*)|*.*"
        };

        if (openDialog.ShowDialog() != DialogResult.OK)
            return;

        Task.Run(async () =>
        {
            ProgressForm? progressForm = null;
            try
            {
                _log.Info($"正在导入配置: {openDialog.FileName}");
                
                // 读取并解析配置文件
                var jsonText = File.ReadAllText(openDialog.FileName, Encoding.UTF8);
                var exported = JsonSerializer.Deserialize<ExportedDeviceConfig>(jsonText);

                if (exported == null || !exported.IsValid())
                {
                    throw new Exception("配置文件格式无效或损坏");
                }

                // 显示配置摘要
                var summary = exported.GetSummary();
                var confirmResult = MessageBox.Show(
                    $"确认导入以下配置?\n\n{summary}",
                    "确认导入",
                    MessageBoxButtons.YesNo,
                    MessageBoxIcon.Question
                );

                if (confirmResult != DialogResult.Yes)
                {
                    _log.Info("用户取消了配置导入");
                    return;
                }

                try
                {
                    // 导入配置到设备
                    var success = await _configManager.LoadFromExportedConfigAsync(exported);

                    if (success)
                    {
                        _log.Info("配置导入成功");
                        
                        MessageBox.Show(
                            $"配置导入成功\n\n{exported.GetSummary()}",
                            "导入完成",
                            MessageBoxButtons.OK,
                            MessageBoxIcon.Information
                        );
                    }
                    else
                    {
                        throw new Exception("配置导入失败");
                    }
                }
                catch (Exception ex)
                {
                    throw;
                }
            }
            catch (Exception ex)
            {
                _log.Info($"导入失败: {ex.Message}");
                
                MessageBox.Show($"导入失败: {ex.Message}", "错误");
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
                Thread.Sleep(80);
                sp.DiscardInBuffer();
                var sw = Stopwatch.StartNew();
                long nextPing = 0;
                while (sw.ElapsedMilliseconds < 1200)
                {
                    if (sw.ElapsedMilliseconds >= nextPing)
                    {
                        nextPing = sw.ElapsedMilliseconds + 250;
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
    private List<MMDevice> _captureDevices = new();
    private bool _helloNotified;
    private DateTime _lastHelloAck = DateTime.MinValue;
    private TaskCompletionSource<string>? _cfgResponseWaiter;
    private readonly BleLinkClient _ble;

    public bool IsUsbOpen => _port != null && _port.IsOpen;
    public bool IsUsbReady => IsUsbOpen && _helloNotified;
    public bool IsBleOpen => _ble.IsConnected;
    public bool IsConnected => IsUsbReady || IsBleOpen;
    public bool IsOpen => IsUsbOpen || IsBleOpen;
    public string? BleDeviceId => _ble.ConnectedDeviceId ?? _ble.LastDeviceId;
    public string? BleDeviceName => _ble.IsConnected ? _ble.ConnectedName : _ble.LastDeviceName;
    public string ConnectionLabel => IsUsbOpen
        ? (_port?.PortName ?? "USB")
        : (IsBleOpen ? $"BLE:{_ble.ConnectedName}" : "Disconnected");
    public event Action? HelloReceived;

    public SerialWorker(Logger log)
    {
        _log = log;
        _ble = new BleLinkClient(_log, HandleLine);
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


    public async Task<bool> OpenBleAsync(string? deviceId = null, string? nameHint = "SongLed")
    {
        if (IsBleOpen) return true;
        Close();
        _helloNotified = false;
        _lastHelloAck = DateTime.MinValue;
        bool ok = await _ble.ConnectAsync(nameHint, deviceId);
        if (!ok) return false;
        SendLine("HELLO");
        _log.Info($"BLE connected: {_ble.ConnectedName}");
        return true;
    }

    public Task<IReadOnlyList<(string Id, string Name)>> ListBleDevicesAsync()
    {
        return _ble.ListDevicesAsync();
    }

    public void Close(bool fast = false)
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
        if (_reader != null && _reader.IsAlive)
        {
            try
            {
                _reader.Join(500);
            }
            catch
            {
                // ignore
            }
        }
        _reader = null;
        try
        {
            var bleTask = _ble.DisconnectAsync();
            if (!fast && !bleTask.Wait(800))
            {
                _log.Info("BLE disconnect timeout");
            }
        }
        catch
        {
            // ignore
        }
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
        if (line.Equals("MIC LIST", StringComparison.OrdinalIgnoreCase))
        {
            _log.Info("鏀跺埌MIC LIST璇锋眰");
            SendMicrophoneList();
            return;
        }
        if (line.StartsWith("MIC SET", StringComparison.OrdinalIgnoreCase))
        {
            int index = ParseInt(line, 7);
            SetDefaultMicrophone(index);
            SendCurrentMicrophone();
            return;
        }
        if (line.StartsWith("CFG", StringComparison.OrdinalIgnoreCase))
        {
            // Handle config commands: CFG SET (response from device)
            if (line.StartsWith("CFG SET", StringComparison.OrdinalIgnoreCase))
            {
                _log.Info($"收到配置响应: {line}");
                if (_cfgResponseWaiter != null && !_cfgResponseWaiter.Task.IsCompleted)
                {
                    _cfgResponseWaiter.TrySetResult(line);
                }
            }
            else if (line.StartsWith("CFG IMPORT OK", StringComparison.OrdinalIgnoreCase))
            {
                _log.Info($"收到配置导入确认: {line}");
                if (_cfgResponseWaiter != null && !_cfgResponseWaiter.Task.IsCompleted)
                {
                    _cfgResponseWaiter.TrySetResult(line);
                }
            }
            else
            {
                _log.Info($"配置命令: {line}");
            }
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
            if (_port != null && _port.IsOpen)
            {
                try
                {
                    _port.WriteLine(text);
                }
                catch (Exception ex)
                {
                    _log.Info($"Serial send failed: {ex.Message}");
                }
                return;
            }

            if (IsBleOpen)
            {
                _ = _ble.SendLineAsync(text);
            }
        }
    }

    /// <summary>
    /// 发送命令并等待 CFG SET 响应
    /// </summary>
    public async Task<string?> SendAndWaitForCfgResponseAsync(string command, int timeoutMs = 3000)
    {
        _cfgResponseWaiter = new TaskCompletionSource<string>();
        
        try
        {
            SendLine(command);
            
            // 使用超时等待响应
            var result = await Task.WhenAny(
                _cfgResponseWaiter.Task,
                Task.Delay(timeoutMs)
            );
            
            if (result == _cfgResponseWaiter.Task)
            {
                return _cfgResponseWaiter.Task.Result;
            }
            else
            {
                _log.Info($"等待 CFG 响应超时 ({timeoutMs}ms)");
                return null;
            }
        }
        catch (Exception ex)
        {
            _log.Info($"等待 CFG 响应出错: {ex.Message}");
            return null;
        }
        finally
        {
            _cfgResponseWaiter = null;
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

    private MMDevice? GetDefaultCaptureDevice()
    {
        try
        {
            return _enumerator.GetDefaultAudioEndpoint(DataFlow.Capture, Role.Multimedia);
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

    private void RefreshCaptureDevices()
    {
        _captureDevices = _enumerator.EnumerateAudioEndPoints(DataFlow.Capture, DeviceState.Active).ToList();
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

    private void SendMicrophoneList()
    {
        try
        {
            _log.Info("寮€濮嬪彂閫侀害鍏嬮鍒楄〃");
            RefreshCaptureDevices();
            _log.Info($"鏋氫妇鍒?{_captureDevices.Count} 涓害鍏嬮");
            SendLine("MIC BEGIN");
            for (int i = 0; i < _captureDevices.Count; i++)
            {
                string name = Sanitize(_captureDevices[i].FriendlyName);
                SendLine($"MIC ITEM {i} {name}");
            }
            SendLine("MIC END");
            SendCurrentMicrophone();
        }
        catch (Exception ex)
        {
            _log.Info($"鍙戦€侀害鍏嬮鍒楄〃澶辫触: {ex.Message}");
        }
    }

    private void SendCurrentMicrophone()
    {
        var dev = GetDefaultCaptureDevice();
        if (dev == null) return;
        if (_captureDevices.Count == 0) RefreshCaptureDevices();
        int index = _captureDevices.FindIndex(d => d.ID == dev.ID);
        if (index >= 0)
        {
            SendLine($"MIC CUR {index}");
        }
    }

    private void SetDefaultMicrophone(int index)
    {
        if (_captureDevices.Count == 0) RefreshCaptureDevices();
        if (index < 0 || index >= _captureDevices.Count) return;
        string id = _captureDevices[index].ID;
        try
        {
            var policy = new PolicyConfigClient() as IPolicyConfig;
            policy?.SetDefaultEndpoint(id, ERole.eConsole);
            policy?.SetDefaultEndpoint(id, ERole.eMultimedia);
            policy?.SetDefaultEndpoint(id, ERole.eCommunications);
        }
        catch (Exception ex)
        {
            _log.Info($"璁剧疆榛樿楹﹀厠椋庡け璐? {ex.Message}");
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

/// <summary>
/// 应用程序状态持久化管理类
/// 存储位置: %APPDATA%\SongLed\SongLedPc\appstate.json
/// 存储内容: 最后连接端口、窗口位置、用户偏好等
/// </summary>
internal sealed class AppState
{
    private static readonly string StateDir = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
        "SongLed", "SongLedPc"
    );
    
    private static readonly string StateFile = Path.Combine(StateDir, "appstate.json");
    
    public string? LastPort { get; set; }
    public string? LastBleDeviceId { get; set; }
    public string? LastBleDeviceName { get; set; }
    public int LastLinkMode { get; set; } = 0;
    public DateTime LastConnected { get; set; }
    public bool RememberLastPort { get; set; } = true;
    public string? Theme { get; set; }
    public Dictionary<string, string> UserPreferences { get; set; } = new();

    public static AppState Load()
    {
        try
        {
            if (File.Exists(StateFile))
            {
                var json = File.ReadAllText(StateFile, Encoding.UTF8);
                var state = JsonSerializer.Deserialize<AppState>(json);
                return state ?? new AppState();
            }
        }
        catch (Exception ex)
        {
            Debug.WriteLine($"Failed to load app state: {ex.Message}");
        }
        
        return new AppState();
    }

    public void Save()
    {
        try
        {
            Directory.CreateDirectory(StateDir);
            
            var options = new JsonSerializerOptions 
            { 
                WriteIndented = true,
                DefaultIgnoreCondition = System.Text.Json.Serialization.JsonIgnoreCondition.WhenWritingNull
            };
            
            var json = JsonSerializer.Serialize(this, options);
            File.WriteAllText(StateFile, json, Encoding.UTF8);
        }
        catch (Exception ex)
        {
            Debug.WriteLine($"Failed to save app state: {ex.Message}");
        }
    }
    
    public void Clear()
    {
        try
        {
            if (File.Exists(StateFile))
            {
                File.Delete(StateFile);
            }
        }
        catch (Exception ex)
        {
            Debug.WriteLine($"Failed to clear app state: {ex.Message}");
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
/// <summary>
/// 进度窗口 - 显示配置写入进度
/// </summary>
internal sealed class ProgressForm : Form
{
    private readonly Label _messageLabel;
    private readonly ProgressBar _progressBar;

    public ProgressForm(string title, string message)
    {
        Text = title;
        Width = 400;
        Height = 150;
        StartPosition = FormStartPosition.CenterScreen;
        FormBorderStyle = FormBorderStyle.FixedDialog;
        MaximizeBox = false;
        MinimizeBox = false;
        TopMost = true;
        ControlBox = false; // 禁止关闭按钮

        _messageLabel = new Label
        {
            Text = message,
            Dock = DockStyle.Top,
            Height = 40,
            TextAlign = ContentAlignment.MiddleCenter,
            Margin = new Padding(10)
        };

        _progressBar = new ProgressBar
        {
            Dock = DockStyle.Fill,
            Style = ProgressBarStyle.Marquee,
            MarqueeAnimationSpeed = 30,
            Height = 30,
            Margin = new Padding(10)
        };

        Controls.Add(_messageLabel);
        Controls.Add(_progressBar);
    }

    public void UpdateMessage(string message)
    {
        if (InvokeRequired)
        {
            Invoke(new Action(() => _messageLabel.Text = message));
        }
        else
        {
            _messageLabel.Text = message;
        }
    }

    public void SetDeterminateProgress(int value, int max = 100)
    {
        if (InvokeRequired)
        {
            Invoke(new Action(() =>
            {
                _progressBar.Style = ProgressBarStyle.Continuous;
                _progressBar.Maximum = max;
                _progressBar.Value = Math.Min(value, max);
            }));
        }
        else
        {
            _progressBar.Style = ProgressBarStyle.Continuous;
            _progressBar.Maximum = max;
            _progressBar.Value = Math.Min(value, max);
        }
    }
}
