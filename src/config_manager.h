// Configuration Storage Helper - 规范的配置存储和管理
// 放在 src/ 目录下作为参考实现

#pragma once

#include <cstdint>
#include "nvs.h"
#include "nvs_flash.h"

namespace songled {

/**
 * @brief 配置版本
 * 递增此值以处理配置结构的向后兼容性
 */
constexpr uint8_t CONFIG_VERSION = 1;

/**
 * @brief NVS命名空间
 */
constexpr const char* NVS_NAMESPACE = "songled";

/**
 * @brief 配置项键名定义
 * 集中管理所有键名，避免拼写错误和不一致
 */
namespace ConfigKeys {
    constexpr const char* VERSION = "config_ver";
    
    // UI配置
    constexpr const char* UI_SPEED = "ui_speed";
    constexpr const char* SEL_SPEED = "sel_speed";
    constexpr const char* WRAP_PAUSE = "wrap_pause";
    constexpr const char* FONT_HUE = "font_hue";
    constexpr const char* SCROLL_MS = "scroll_ms";
    constexpr const char* LYRIC_CPS = "lyric_cps";
    
    // 未来扩展
    // constexpr const char* AUDIO_MODE = "audio_mode";
    // constexpr const char* LED_BRIGHTNESS = "led_brightness";
    // constexpr const char* ANIMATION_TYPE = "anim_type";
}

/**
 * @brief 配置结构体
 * 用于未来升级到结构化存储
 */
struct SongLedConfig {
    uint8_t version = CONFIG_VERSION;
    
    // UI配置
    uint8_t ui_speed = 13;
    uint8_t sel_speed = 25;
    uint8_t wrap_pause = 15;
    uint8_t font_hue = 17;
    uint8_t scroll_ms = 30;
    uint8_t lyric_cps = 8;
    
    // 验证配置的有效性
    bool isValid() const {
        return version == CONFIG_VERSION &&
               ui_speed >= 1 && ui_speed <= 50 &&
               sel_speed >= 1 && sel_speed <= 50 &&
               wrap_pause <= 50 &&
               font_hue <= 50 &&
               scroll_ms >= 1 && scroll_ms <= 50 &&
               lyric_cps >= 1 && lyric_cps <= 30;
    }
};

/**
 * @brief 配置管理类
 * 提供规范的NVS操作接口
 */
class ConfigManager {
public:
    /**
     * @brief 初始化NVS闪存
     * @return true 初始化成功
     */
    static bool initNVS() {
        esp_err_t err = nvs_flash_init();
        if (err == ESP_ERR_NVS_NO_FREE_PAGES || 
            err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
            nvs_flash_erase();
            err = nvs_flash_init();
        }
        return err == ESP_OK;
    }
    
    /**
     * @brief 从NVS加载配置
     * @param config 配置结构体引用
     * @return true 加载成功
     */
    static bool loadConfig(SongLedConfig& config) {
        nvs_handle_t handle;
        if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
            return false;
        }
        
        uint8_t version = 0;
        if (nvs_get_u8(handle, ConfigKeys::VERSION, &version) != ESP_OK) {
            // 版本键不存在，可能是旧配置
            version = CONFIG_VERSION;
        }
        
        // 根据版本号处理不同的配置格式
        bool success = loadConfigByVersion(handle, version, config);
        
        nvs_close(handle);
        return success && config.isValid();
    }
    
    /**
     * @brief 保存配置到NVS
     * @param config 配置结构体引用
     * @return true 保存成功
     */
    static bool saveConfig(const SongLedConfig& config) {
        if (!config.isValid()) {
            return false;
        }
        
        nvs_handle_t handle;
        if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) {
            return false;
        }
        
        bool success = true;
        
        // 保存版本
        if (nvs_set_u8(handle, ConfigKeys::VERSION, config.version) != ESP_OK) {
            success = false;
        }
        
        // 保存所有配置项
        if (nvs_set_u8(handle, ConfigKeys::UI_SPEED, config.ui_speed) != ESP_OK ||
            nvs_set_u8(handle, ConfigKeys::SEL_SPEED, config.sel_speed) != ESP_OK ||
            nvs_set_u8(handle, ConfigKeys::WRAP_PAUSE, config.wrap_pause) != ESP_OK ||
            nvs_set_u8(handle, ConfigKeys::FONT_HUE, config.font_hue) != ESP_OK ||
            nvs_set_u8(handle, ConfigKeys::SCROLL_MS, config.scroll_ms) != ESP_OK ||
            nvs_set_u8(handle, ConfigKeys::LYRIC_CPS, config.lyric_cps) != ESP_OK) {
            success = false;
        }
        
        // 提交到闪存
        if (success) {
            success = nvs_commit(handle) == ESP_OK;
        }
        
        nvs_close(handle);
        return success;
    }
    
    /**
     * @brief 清除所有配置
     * @return true 清除成功
     */
    static bool clearAllConfig() {
        nvs_handle_t handle;
        if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) {
            return false;
        }
        
        bool success = true;
        if (nvs_erase_all(handle) != ESP_OK) {
            success = false;
        }
        
        if (success) {
            success = nvs_commit(handle) == ESP_OK;
        }
        
        nvs_close(handle);
        return success;
    }
    
    /**
     * @brief 获取NVS统计信息（诊断用）
     */
    static void printNVSStats() {
        nvs_stats_t stats;
        if (nvs_get_stats(NULL, &stats) == ESP_OK) {
            // ESP_LOGI("CONFIG", "NVS: %d used / %d total", 
            //          stats.used_entries, stats.total_entries);
        }
    }

private:
    /**
     * @brief 根据版本号加载不同格式的配置
     */
    static bool loadConfigByVersion(nvs_handle_t handle, 
                                   uint8_t version,
                                   SongLedConfig& config) {
        if (version != CONFIG_VERSION) {
            // 版本不匹配，可以在此处理迁移
            return false;
        }
        
        uint8_t val = 0;
        
        if (nvs_get_u8(handle, ConfigKeys::UI_SPEED, &val) == ESP_OK && 
            val >= 1 && val <= 50) {
            config.ui_speed = val;
        }
        
        if (nvs_get_u8(handle, ConfigKeys::SEL_SPEED, &val) == ESP_OK) {
            if (val >= 1 && val <= 50) {
                config.sel_speed = val;
            } else if (val >= 60) {
                // 兼容旧的毫秒格式
                config.sel_speed = mapSelSpeedFromMs(static_cast<float>(val));
            }
        }
        
        if (nvs_get_u8(handle, ConfigKeys::WRAP_PAUSE, &val) == ESP_OK) {
            if (val <= 50) {
                config.wrap_pause = val;
            } else {
                // 兼容旧的毫秒格式
                config.wrap_pause = mapWrapPauseFromMs(static_cast<float>(val));
            }
        }
        
        if (nvs_get_u8(handle, ConfigKeys::FONT_HUE, &val) == ESP_OK && 
            val <= 50) {
            config.font_hue = val;
        }
        
        if (nvs_get_u8(handle, ConfigKeys::SCROLL_MS, &val) == ESP_OK) {
            if (val >= 1 && val <= 50) {
                config.scroll_ms = val;
            } else if (val > 50) {
                // 兼容旧的毫秒格式
                config.scroll_ms = mapScrollFromMs(static_cast<float>(val));
            }
        }
        
        if (nvs_get_u8(handle, ConfigKeys::LYRIC_CPS, &val) == ESP_OK &&
            val >= 1 && val <= 30) {
            config.lyric_cps = val;
        }
        
        return true;
    }
    
    // 前向声明，这些函数在main.cpp中定义
    static uint8_t mapSelSpeedFromMs(float ms);
    static uint8_t mapWrapPauseFromMs(float ms);
    static uint8_t mapScrollFromMs(float ms);
};

}  // namespace songled
