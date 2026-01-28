using System;
using System.Collections.Generic;
using System.Text.Json.Serialization;

namespace SongLedPc;

/// <summary>
/// ESP32-S3 设备配置模型
/// 映射到单片机的NVS存储结构
/// </summary>
internal class DeviceConfig
{
    /// <summary>
    /// 配置版本号
    /// </summary>
    [JsonPropertyName("version")]
    public uint Version { get; set; } = 1;

    /// <summary>
    /// UI菜单滚动速度 (1-50)
    /// </summary>
    [JsonPropertyName("ui_speed")]
    public uint UiSpeed { get; set; } = 13;

    /// <summary>
    /// 编码器旋转滚动速度 (1-50)
    /// </summary>
    [JsonPropertyName("sel_speed")]
    public uint SelSpeed { get; set; } = 25;

    /// <summary>
    /// 文本换行暂停时间 (0-50)
    /// </summary>
    [JsonPropertyName("wrap_pause")]
    public uint WrapPause { get; set; } = 15;

    /// <summary>
    /// 字体颜色色调 (0-50, maps to 0-360 degrees internally)
    /// </summary>
    [JsonPropertyName("font_hue")]
    public uint FontHue { get; set; } = 17;

    /// <summary>
    /// 文本滚动时间 (1-50)
    /// </summary>
    [JsonPropertyName("scroll_ms")]
    public uint ScrollMs { get; set; } = 30;

    /// <summary>
    /// 歌词逐字滚动速度 (1-30)
    /// </summary>
    [JsonPropertyName("lyric_cps")]
    public uint LyricCps { get; set; } = 8;

    /// <summary>
    /// 验证配置的有效性
    /// </summary>
    public bool IsValid()
    {
        return Version == 1 &&
               UiSpeed >= 1 && UiSpeed <= 50 &&
               SelSpeed >= 1 && SelSpeed <= 50 &&
               WrapPause <= 50 &&
               FontHue <= 50 &&
               ScrollMs >= 1 && ScrollMs <= 50 &&
               LyricCps >= 1 && LyricCps <= 30;
    }

    /// <summary>
    /// 获取配置的人类可读描述
    /// </summary>
    public override string ToString()
    {
        return $"DeviceConfig(v{Version}): " +
               $"UiSpeed={UiSpeed}, SelSpeed={SelSpeed}, " +
               $"WrapPause={WrapPause}, FontHue={FontHue}, " +
               $"ScrollMs={ScrollMs}, LyricCps={LyricCps}";
    }

    /// <summary>
    /// 获取与默认配置的差异
    /// </summary>
    public Dictionary<string, (uint Current, uint Default)> GetDifferences()
    {
        var defaults = new DeviceConfig();
        var diffs = new Dictionary<string, (uint, uint)>();

        if (UiSpeed != defaults.UiSpeed) diffs["ui_speed"] = (UiSpeed, defaults.UiSpeed);
        if (SelSpeed != defaults.SelSpeed) diffs["sel_speed"] = (SelSpeed, defaults.SelSpeed);
        if (WrapPause != defaults.WrapPause) diffs["wrap_pause"] = (WrapPause, defaults.WrapPause);
        if (FontHue != defaults.FontHue) diffs["font_hue"] = (FontHue, defaults.FontHue);
        if (ScrollMs != defaults.ScrollMs) diffs["scroll_ms"] = (ScrollMs, defaults.ScrollMs);
        if (LyricCps != defaults.LyricCps) diffs["lyric_cps"] = (LyricCps, defaults.LyricCps);

        return diffs;
    }
}

/// <summary>
/// 导出配置文件格式（完整的配置备份）
/// </summary>
internal class ExportedDeviceConfig
{
    /// <summary>
    /// 导出时间
    /// </summary>
    [JsonPropertyName("export_time")]
    public string ExportTime { get; set; } = DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss");

    /// <summary>
    /// 应用程序版本
    /// </summary>
    [JsonPropertyName("app_version")]
    public string AppVersion { get; set; } = "0.0.1";

    /// <summary>
    /// 设备配置
    /// </summary>
    [JsonPropertyName("device_config")]
    public DeviceConfig DeviceConfig { get; set; } = new();

    /// <summary>
    /// 当前连接信息
    /// </summary>
    [JsonPropertyName("connection_info")]
    public Dictionary<string, string> ConnectionInfo { get; set; } = new();

    /// <summary>
    /// PC端应用设置
    /// </summary>
    [JsonPropertyName("app_settings")]
    public Dictionary<string, string> AppSettings { get; set; } = new();

    /// <summary>
    /// 验证导出配置的有效性
    /// </summary>
    public bool IsValid()
    {
        return DeviceConfig?.IsValid() ?? false;
    }

    /// <summary>
    /// 获取配置摘要信息
    /// </summary>
    public string GetSummary()
    {
        var diffs = DeviceConfig.GetDifferences();
        var summary = $"设备配置导出\n" +
                      $"导出时间: {ExportTime}\n" +
                      $"应用版本: {AppVersion}\n" +
                      $"设备配置版本: {DeviceConfig.Version}\n";

        if (diffs.Count > 0)
        {
            summary += $"\n自定义配置 ({diffs.Count} 项):\n";
            foreach (var diff in diffs)
            {
                summary += $"  {diff.Key}: {diff.Value.Current} (默认: {diff.Value.Default})\n";
            }
        }
        else
        {
            summary += "\n配置为全部默认值";
        }

        return summary;
    }
}

/// <summary>
/// 设备配置管理器
/// 负责与ESP32-S3通过串口交互管理配置
/// </summary>
internal interface IDeviceConfigManager
{
    /// <summary>
    /// 从设备读取当前配置
    /// </summary>
    Task<DeviceConfig?> ReadConfigAsync();

    /// <summary>
    /// 写入配置到设备
    /// </summary>
    Task<bool> WriteConfigAsync(DeviceConfig config);

    /// <summary>
    /// 清除设备配置（恢复出厂）
    /// </summary>
    Task<bool> ClearConfigAsync();

    /// <summary>
    /// 保存设备配置到NVS
    /// </summary>
    Task<bool> SaveConfigAsync();
}
