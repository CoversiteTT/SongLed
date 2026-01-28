using System;
using System.Threading;
using System.Threading.Tasks;

namespace SongLedPc;

/// <summary>
/// 设备配置管理器实现
/// 通过串口与ESP32-S3通信
/// </summary>
internal class DeviceConfigManager : IDeviceConfigManager
{
    private readonly SerialWorker _serial;
    private readonly Logger _log;

    public DeviceConfigManager(SerialWorker serial, Logger log)
    {
        _serial = serial ?? throw new ArgumentNullException(nameof(serial));
        _log = log ?? throw new ArgumentNullException(nameof(log));
    }

    /// <summary>
    /// 从设备读取配置
    /// 通过发送 CFG GET 命令并解析响应
    /// </summary>
    public async Task<DeviceConfig?> ReadConfigAsync()
    {
        if (!_serial.IsOpen)
        {
            _log.Info("设备未连接，无法读取配置");
            return null;
        }

        try
        {
            _log.Info("正在从设备读取配置...");

            // 发送查询命令
            var response = await _serial.SendAndWaitForCfgResponseAsync("CFG GET", 3000);
            
            if (response == null)
            {
                _log.Info("未收到设备响应");
                return null;
            }

            // 解析响应格式: "CFG SET ui_speed=13 sel_speed=25 wrap_pause=15 font_hue=17 scroll_ms=30 lyric_cps=8"
            var config = ParseCfgResponse(response);
            
            if (config == null)
            {
                _log.Info("配置响应格式无效");
                return null;
            }

            _log.Info($"设备配置读取完成: {config}");
            return config;
        }
        catch (Exception ex)
        {
            _log.Info($"读取设备配置失败: {ex.Message}");
            return null;
        }
    }

    /// <summary>
    /// 解析设备响应中的配置数据
    /// </summary>
    private DeviceConfig? ParseCfgResponse(string response)
    {
        // 预期格式: "CFG SET ui_speed=13 sel_speed=25 wrap_pause=15 font_hue=17 scroll_ms=30 lyric_cps=8"
        if (!response.StartsWith("CFG SET", StringComparison.OrdinalIgnoreCase))
        {
            return null;
        }

        var config = new DeviceConfig();
        var parts = response.Substring(7).Trim().Split(' ');

        foreach (var part in parts)
        {
            var kv = part.Split('=');
            if (kv.Length != 2) continue;

            string key = kv[0].Trim();
            if (!uint.TryParse(kv[1].Trim(), out uint value))
            {
                continue;
            }

            switch (key.ToLowerInvariant())
            {
                case "ui_speed":
                    config.UiSpeed = value;
                    break;
                case "sel_speed":
                    config.SelSpeed = value;
                    break;
                case "wrap_pause":
                    config.WrapPause = value;
                    break;
                case "font_hue":
                    config.FontHue = value;
                    break;
                case "scroll_ms":
                    config.ScrollMs = value;
                    break;
                case "lyric_cps":
                    config.LyricCps = value;
                    break;
            }
        }

        return config;
    }

    /// <summary>
    /// 写入配置到设备（仅内存，不保存到NVS）
    /// </summary>
    public async Task<bool> WriteConfigAsync(DeviceConfig config)
    {
        if (!_serial.IsOpen)
        {
            _log.Info("设备未连接，无法写入配置");
            return false;
        }

        if (!config.IsValid())
        {
            _log.Info("配置无效，无法写入");
            return false;
        }

        try
        {
            _log.Info("正在向设备写入配置...");

            // 发送每个配置项
            _serial.SendLine($"CFG SET ui_speed {config.UiSpeed}");
            await Task.Delay(50);

            _serial.SendLine($"CFG SET sel_speed {config.SelSpeed}");
            await Task.Delay(50);

            _serial.SendLine($"CFG SET wrap_pause {config.WrapPause}");
            await Task.Delay(50);

            _serial.SendLine($"CFG SET font_hue {config.FontHue}");
            await Task.Delay(50);

            _serial.SendLine($"CFG SET scroll_ms {config.ScrollMs}");
            await Task.Delay(50);

            _serial.SendLine($"CFG SET lyric_cps {config.LyricCps}");
            await Task.Delay(50);

            _log.Info("配置已发送到设备");
            return true;
        }
        catch (Exception ex)
        {
            _log.Info($"写入配置失败: {ex.Message}");
            return false;
        }
    }

    /// <summary>
    /// 清除设备配置（恢复出厂设置）
    /// </summary>
    public async Task<bool> ClearConfigAsync()
    {
        if (!_serial.IsOpen)
        {
            _log.Info("设备未连接，无法清除配置");
            return false;
        }

        try
        {
            _log.Info("正在清除设备配置...");

            _serial.SendLine("CFG CLR");
            
            // 等待设备处理
            await Task.Delay(1000);

            _log.Info("设备配置已清除，恢复为出厂设置");
            return true;
        }
        catch (Exception ex)
        {
            _log.Info($"清除配置失败: {ex.Message}");
            return false;
        }
    }

    /// <summary>
    /// 保存配置到NVS（持久化）
    /// </summary>
    public async Task<bool> SaveConfigAsync()
    {
        if (!_serial.IsOpen)
        {
            _log.Info("设备未连接，无法保存配置");
            return false;
        }

        try
        {
            _log.Info("正在保存配置到设备NVS...");

            _serial.SendLine("CFG SAVE");
            
            // 等待设备处理
            await Task.Delay(500);

            _log.Info("配置已保存到设备");
            return true;
        }
        catch (Exception ex)
        {
            _log.Info($"保存配置失败: {ex.Message}");
            return false;
        }
    }

    /// <summary>
    /// 从导出的配置文件加载配置到设备
    /// </summary>
    public async Task<bool> LoadFromExportedConfigAsync(ExportedDeviceConfig exported)
    {
        if (!exported.IsValid())
        {
            _log.Info("导入的配置文件无效");
            return false;
        }

        if (!_serial.IsOpen)
        {
            _log.Info("设备未连接，无法导入配置");
            return false;
        }

        try
        {
            var config = exported.DeviceConfig;
            
            // 构建 JSON 格式的配置字符串
            string jsonConfig = $"\"ui_speed\":{config.UiSpeed},\"sel_speed\":{config.SelSpeed}," +
                              $"\"wrap_pause\":{config.WrapPause},\"font_hue\":{config.FontHue}," +
                              $"\"scroll_ms\":{config.ScrollMs},\"lyric_cps\":{config.LyricCps}";
            
            string command = $"CFG IMPORT {{{jsonConfig}}}";
            _log.Info($"发送配置导入命令: {command}");
            
            // 发送 CFG IMPORT 命令，让设备直接处理和写入
            var response = await _serial.SendAndWaitForCfgResponseAsync(command, 5000);
            
            _log.Info($"收到响应: {response}");
            
            if (response == null || !response.Contains("CFG IMPORT OK"))
            {
                _log.Info("设备配置导入失败，未收到确认");
                return false;
            }

            _log.Info($"配置已导入到设备: {config}");
            return true;
        }
        catch (Exception ex)
        {
            _log.Info($"配置导入失败: {ex.Message}");
            return false;
        }
    }

    /// <summary>
    /// 导出当前设备配置到文件格式
    /// </summary>
    public ExportedDeviceConfig CreateExport(DeviceConfig config, 
                                            string? currentPort = null,
                                            string? appVersion = null)
    {
        return new ExportedDeviceConfig
        {
            ExportTime = DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss"),
            AppVersion = appVersion ?? "1.0.0",
            DeviceConfig = config,
            ConnectionInfo = new()
            {
                ["port"] = currentPort ?? "unknown",
                ["connected"] = _serial.IsOpen.ToString(),
                ["timestamp"] = DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss")
            },
            AppSettings = new()
            {
                ["app_name"] = "SongLedPc",
                ["config_format_version"] = "1"
            }
        };
    }
}
