#include "hal_astra_esp32.h"

#include <cmath>
#include <cstring>
#include <algorithm>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/semphr.h"

#include "hal/hal.h"
#include "u8g2.h"
#include "u8x8.h"
#include "hal/lcd_types.h"

namespace {
// Base config: 320x240 ST7789 (landscape).
constexpr int SCREEN_W = 320;
constexpr int SCREEN_H = 240;
constexpr int UI_SCALE = 1; // Use native resolution for better lyrics display
constexpr int LOGICAL_W = (SCREEN_W + UI_SCALE - 1) / UI_SCALE;
constexpr int LOGICAL_H = (SCREEN_H + UI_SCALE - 1) / UI_SCALE;
constexpr int TILE_W = (LOGICAL_W + 7) / 8;
constexpr int TILE_H = (LOGICAL_H + 7) / 8;
constexpr spi_host_device_t TFT_SPI_HOST = SPI2_HOST;
constexpr bool TFT_DC_HIGH_ON_CMD = false;
constexpr bool TFT_DC_LOW_ON_DATA = false;
constexpr bool TFT_DC_LOW_ON_PARAM = false;
constexpr bool TFT_CS_HIGH_ACTIVE = false;
constexpr int TFT_GAP_X = 0;
constexpr int TFT_GAP_Y = 0;
constexpr bool TFT_INVERT = false;
constexpr bool TFT_SWAP_XY = true;
constexpr bool TFT_MIRROR_X = true;
constexpr bool TFT_MIRROR_Y = false;

constexpr int SPI_HZ_MIN = 1000000;
constexpr int SPI_HZ_MAX = 80000000;

constexpr gpio_num_t PIN_TFT_MOSI = GPIO_NUM_11;
constexpr gpio_num_t PIN_TFT_SCLK = GPIO_NUM_12;
constexpr gpio_num_t PIN_TFT_CS = GPIO_NUM_10;
constexpr gpio_num_t PIN_TFT_DC = GPIO_NUM_9;
constexpr gpio_num_t PIN_TFT_RST = GPIO_NUM_7;
constexpr gpio_num_t PIN_TFT_BL = GPIO_NUM_14;

constexpr gpio_num_t PIN_ENC_A = GPIO_NUM_15;
constexpr gpio_num_t PIN_ENC_B = GPIO_NUM_16;
constexpr gpio_num_t PIN_ENC_SW = GPIO_NUM_17;
constexpr gpio_num_t PIN_BTN_BACK = GPIO_NUM_18;

constexpr uint16_t RGB565(uint8_t r, uint8_t g, uint8_t b) {
  return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

constexpr uint16_t COLOR_BG = RGB565(0, 0, 0);
constexpr uint16_t COLOR_FG = RGB565(0, 255, 0);

static volatile bool s_flush_busy = false;
const char *TAG = "hal";

constexpr uint8_t BAYER4[4][4] = {
    {0, 8, 2, 10},
    {12, 4, 14, 6},
    {3, 11, 1, 9},
    {15, 7, 13, 5}};

inline float wrap_deg(float deg) {
  while (deg < 0.0f) deg += 360.0f;
  while (deg >= 360.0f) deg -= 360.0f;
  return deg;
}

inline bool angle_in_range(float angle, float start_deg, float end_deg) {
  angle = wrap_deg(angle);
  start_deg = wrap_deg(start_deg);
  end_deg = wrap_deg(end_deg);
  if (start_deg <= end_deg) {
    return angle >= start_deg && angle <= end_deg;
  }
  return angle >= start_deg || angle <= end_deg;
}

static bool lcd_on_color_trans_done(esp_lcd_panel_io_handle_t panel_io,
                                    esp_lcd_panel_io_event_data_t *edata,
                                    void *user_ctx) {
  (void)panel_io;
  (void)edata;
  (void)user_ctx;
  s_flush_busy = false;
  return false;
}

static const u8x8_display_info_t u8x8_esp32_320x240_info = {
    0,
    1,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    TILE_W,
    TILE_H,
    0,
    0,
    LOGICAL_W,
    LOGICAL_H};

uint8_t u8x8_d_esp32_320x240_cb(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr) {
  (void)arg_int;
  (void)arg_ptr;
  switch (msg) {
    case U8X8_MSG_DISPLAY_SETUP_MEMORY:
      u8x8_d_helper_display_setup_memory(u8x8, &u8x8_esp32_320x240_info);
      break;
    case U8X8_MSG_DISPLAY_INIT:
      u8x8_d_helper_display_init(u8x8);
      break;
    default:
      break;
  }
  return 1;
}

uint8_t u8x8_dummy_ok_cb(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr) {
  (void)u8x8;
  (void)msg;
  (void)arg_int;
  (void)arg_ptr;
  return 1;
}

inline bool get_pixel(const uint8_t *buf, int x, int y, int tile_w) {
  int tile_x = x >> 3;
  int tile_y = y >> 3;
  int tile_index = tile_y * tile_w + tile_x;
  int byte_index = tile_index * 8 + (x & 7);
  return (buf[byte_index] >> (y & 7)) & 0x01;
}
}  // namespace

HALAstraESP32 *HALAstraESP32::s_instance = nullptr;

HALAstraESP32::HALAstraESP32() = default;

HALAstraESP32::~HALAstraESP32() {
  if (u8g2_buf) {
    free(u8g2_buf);
    u8g2_buf = nullptr;
  }
  if (linebuf) {
    free(linebuf);
    linebuf = nullptr;
  }
  if (framebuf) {
    free(framebuf);
    framebuf = nullptr;
  }
}

void HALAstraESP32::init() {
  s_instance = this;
  config.screenWeight = LOGICAL_W;
  config.screenHeight = LOGICAL_H;
  config.screenBright = 255;
  fgColor = COLOR_FG;

  init_display();
  init_inputs();

  tile_width = TILE_W;
  tile_height = TILE_H;
  size_t buf_size = tile_width * tile_height * 8;
  u8g2_buf = static_cast<uint8_t *>(calloc(buf_size, 1));

  u8g2_SetupDisplay(&u8g2,
                    u8x8_d_esp32_320x240_cb,
                    u8x8_cad_empty,
                    u8x8_dummy_ok_cb,
                    u8x8_dummy_ok_cb);
  u8g2_SetupBuffer(&u8g2,
                   u8g2_buf,
                   static_cast<uint8_t>(tile_height),
                   u8g2_ll_hvline_vertical_top_lsb,
                   &u8g2_cb_r0);
  u8g2_buf = u8g2_GetBufferPtr(&u8g2);

  u8g2_InitDisplay(&u8g2);
  u8g2_SetPowerSave(&u8g2, 0);
  u8g2_ClearBuffer(&u8g2);
  u8g2_SetFontMode(&u8g2, 1);
  u8g2_SetFontDirection(&u8g2, 0);
  u8g2_SetFont(&u8g2, u8g2_font_zpix);
}

int HALAstraESP32::clamp_spi_hz(int hz) const {
  if (hz < SPI_HZ_MIN) return SPI_HZ_MIN;
  if (hz > SPI_HZ_MAX) return SPI_HZ_MAX;
  return hz;
}

bool HALAstraESP32::init_buffers_once() {
  if (!framebuf) {
    if (displayConfig.use_psram) {
      // Use PSRAM for framebuffer (slower but saves SRAM)
      framebuf = static_cast<uint16_t *>(
          heap_caps_malloc(SCREEN_W * SCREEN_H * sizeof(uint16_t),
                           MALLOC_CAP_SPIRAM));
      if (framebuf) {
        framebuf_dma = false;
        ESP_LOGI(TAG, "Framebuffer in PSRAM: %d KB", 
                 (SCREEN_W * SCREEN_H * 2) / 1024);
      } else {
        ESP_LOGW(TAG, "PSRAM allocation failed, falling back to SRAM");
      }
    }
    if (!framebuf) {
      // Use internal SRAM with DMA for maximum performance
      framebuf = static_cast<uint16_t *>(
          heap_caps_malloc(SCREEN_W * SCREEN_H * sizeof(uint16_t),
                           MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
      if (framebuf) {
        framebuf_dma = true;
        ESP_LOGI(TAG, "Framebuffer in internal SRAM with DMA: %d KB", 
                 (SCREEN_W * SCREEN_H * 2) / 1024);
      } else {
        framebuf = static_cast<uint16_t *>(
            malloc(SCREEN_W * SCREEN_H * sizeof(uint16_t)));
        framebuf_dma = false;
        ESP_LOGW(TAG, "Framebuffer in SRAM without DMA");
      }
    }
  }
  
  // Allocate second buffer for double buffering
  if (displayConfig.use_double_buffer && !backbuf) {
    if (displayConfig.use_psram) {
      backbuf = static_cast<uint16_t *>(
          heap_caps_malloc(SCREEN_W * SCREEN_H * sizeof(uint16_t),
                           MALLOC_CAP_SPIRAM));
    } else {
      backbuf = static_cast<uint16_t *>(
          heap_caps_malloc(SCREEN_W * SCREEN_H * sizeof(uint16_t),
                           MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    }
    if (backbuf) {
      ESP_LOGI(TAG, "Double buffer allocated: %d KB", 
               (SCREEN_W * SCREEN_H * 2) / 1024);
    } else {
      ESP_LOGW(TAG, "Double buffer allocation failed");
      displayConfig.use_double_buffer = false;
    }
  }
  
  if (!linebuf) {
    linebuf = static_cast<uint16_t *>(
        heap_caps_malloc(SCREEN_W * sizeof(uint16_t),
                         MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    if (linebuf) {
      linebuf_dma = true;
    } else {
      linebuf = static_cast<uint16_t *>(malloc(SCREEN_W * sizeof(uint16_t)));
      linebuf_dma = false;
    }
  }
  if (!framebuf || !linebuf) {
    ESP_LOGE(TAG, "buffer alloc failed");
    return false;
  }
  return true;
}

void HALAstraESP32::adjust_dma_for_buffers() {
  if (!displayConfig.use_dma) return;
  if (displayConfig.use_framebuffer && !framebuf_dma) {
    ESP_LOGW(TAG, "framebuffer not DMA capable, forcing DMA off");
    displayConfig.use_dma = false;
  }
  if (!displayConfig.use_framebuffer && !linebuf_dma) {
    ESP_LOGW(TAG, "line buffer not DMA capable, forcing DMA off");
    displayConfig.use_dma = false;
  }
}

bool HALAstraESP32::init_display() {
  if (!displayMutex) {
    displayMutex = xSemaphoreCreateMutex();
  }

  if (!init_buffers_once()) {
    return false;
  }

  displayConfig.spi_hz = clamp_spi_hz(displayConfig.spi_hz);
  adjust_dma_for_buffers();

  gpio_config_t bk_conf = {
      .pin_bit_mask = (1ULL << PIN_TFT_BL),
      .mode = GPIO_MODE_OUTPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE};
  gpio_config(&bk_conf);
  gpio_set_level(PIN_TFT_BL, 1);

  spi_bus_config_t buscfg = {};
  buscfg.sclk_io_num = PIN_TFT_SCLK;
  buscfg.mosi_io_num = PIN_TFT_MOSI;
  buscfg.miso_io_num = -1;
  buscfg.quadwp_io_num = -1;
  buscfg.quadhd_io_num = -1;
  buscfg.max_transfer_sz = SCREEN_W * SCREEN_H * sizeof(uint16_t);
  spi_dma_chan_t dma_chan = displayConfig.use_dma ? SPI_DMA_CH_AUTO : SPI_DMA_DISABLED;
  spi_bus_initialize(TFT_SPI_HOST, &buscfg, dma_chan);

  esp_lcd_panel_io_spi_config_t io_config = {};
  io_config.dc_gpio_num = PIN_TFT_DC;
  io_config.cs_gpio_num = PIN_TFT_CS;
  io_config.pclk_hz = displayConfig.spi_hz;
  io_config.lcd_cmd_bits = 8;
  io_config.lcd_param_bits = 8;
  io_config.spi_mode = 0;
  io_config.trans_queue_depth = 1;
  io_config.on_color_trans_done = lcd_on_color_trans_done;
  io_config.user_ctx = nullptr;
  io_config.flags.dc_high_on_cmd = TFT_DC_HIGH_ON_CMD;
  io_config.flags.dc_low_on_data = TFT_DC_LOW_ON_DATA;
  io_config.flags.dc_low_on_param = TFT_DC_LOW_ON_PARAM;
  io_config.flags.cs_high_active = TFT_CS_HIGH_ACTIVE;
  esp_lcd_new_panel_io_spi(reinterpret_cast<esp_lcd_spi_bus_handle_t>(TFT_SPI_HOST), &io_config,
                           reinterpret_cast<esp_lcd_panel_io_handle_t *>(&panel_io));

  esp_lcd_panel_dev_config_t panel_config = {};
  panel_config.reset_gpio_num = PIN_TFT_RST;
  panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
  panel_config.data_endian = LCD_RGB_DATA_ENDIAN_BIG;
  panel_config.bits_per_pixel = 16;
  esp_lcd_new_panel_st7789(reinterpret_cast<esp_lcd_panel_io_handle_t>(panel_io),
                           &panel_config,
                           reinterpret_cast<esp_lcd_panel_handle_t *>(&panel));
  esp_lcd_panel_reset(reinterpret_cast<esp_lcd_panel_handle_t>(panel));
  vTaskDelay(pdMS_TO_TICKS(20));
  esp_lcd_panel_init(reinterpret_cast<esp_lcd_panel_handle_t>(panel));
  esp_lcd_panel_set_gap(reinterpret_cast<esp_lcd_panel_handle_t>(panel), TFT_GAP_X, TFT_GAP_Y);
  esp_lcd_panel_invert_color(reinterpret_cast<esp_lcd_panel_handle_t>(panel), TFT_INVERT);
  esp_lcd_panel_swap_xy(reinterpret_cast<esp_lcd_panel_handle_t>(panel), TFT_SWAP_XY);
  esp_lcd_panel_mirror(reinterpret_cast<esp_lcd_panel_handle_t>(panel), TFT_MIRROR_X, TFT_MIRROR_Y);
  esp_lcd_panel_disp_on_off(reinterpret_cast<esp_lcd_panel_handle_t>(panel), true);

  return true;
}

void HALAstraESP32::deinit_display() {
  if (panel) {
    esp_lcd_panel_del(reinterpret_cast<esp_lcd_panel_handle_t>(panel));
    panel = nullptr;
  }
  if (panel_io) {
    esp_lcd_panel_io_del(reinterpret_cast<esp_lcd_panel_io_handle_t>(panel_io));
    panel_io = nullptr;
  }
  spi_bus_free(TFT_SPI_HOST);
  
  // Free buffers so they can be reallocated with new config (e.g., PSRAM vs SRAM)
  if (framebuf) {
    free(framebuf);
    framebuf = nullptr;
    framebuf_dma = false;
  }
  if (backbuf) {
    free(backbuf);
    backbuf = nullptr;
  }
  if (linebuf) {
    free(linebuf);
    linebuf = nullptr;
    linebuf_dma = false;
  }
}

void HALAstraESP32::lcdFill(uint16_t color) {
  if (!linebuf || !panel) return;
  if (displayMutex) xSemaphoreTake(displayMutex, portMAX_DELAY);
  for (int x = 0; x < SCREEN_W; ++x) {
    linebuf[x] = color;
  }
  for (int y = 0; y < SCREEN_H; ++y) {
    s_flush_busy = true;
    esp_lcd_panel_draw_bitmap(reinterpret_cast<esp_lcd_panel_handle_t>(panel), 0, y, SCREEN_W, y + 1, linebuf);
    while (s_flush_busy) {
      vTaskDelay(pdMS_TO_TICKS(1));
    }
  }
  if (displayMutex) xSemaphoreGive(displayMutex);
}

uint16_t HALAstraESP32::blend565(uint16_t bg, uint16_t fg, float alpha) const {
  if (alpha <= 0.0f) return bg;
  if (alpha >= 1.0f) return fg;
  int r_bg = ((bg >> 11) & 0x1F) << 3;
  int g_bg = ((bg >> 5) & 0x3F) << 2;
  int b_bg = (bg & 0x1F) << 3;
  int r_fg = ((fg >> 11) & 0x1F) << 3;
  int g_fg = ((fg >> 5) & 0x3F) << 2;
  int b_fg = (fg & 0x1F) << 3;
  int r = static_cast<int>(r_bg + (r_fg - r_bg) * alpha);
  int g = static_cast<int>(g_bg + (g_fg - g_bg) * alpha);
  int b = static_cast<int>(b_bg + (b_fg - b_bg) * alpha);
  return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

float HALAstraESP32::ditherAlpha(float alpha, int x, int y) const {
  if (alpha <= 0.0f) return 0.0f;
  if (alpha >= 1.0f) return 1.0f;
  float level = alpha * 15.0f;
  int low = static_cast<int>(std::floor(level));
  if (low < 0) low = 0;
  if (low > 15) low = 15;
  float frac = level - static_cast<float>(low);
  int threshold = BAYER4[y & 3][x & 3];
  int pick = (frac * 16.0f > static_cast<float>(threshold)) ? (low + 1) : low;
  if (pick > 15) pick = 15;
  return static_cast<float>(pick) / 15.0f;
}

void HALAstraESP32::drawArcOverlayLine(int y, uint16_t *line, int width) {
  if (!arcOverlay.enabled || !line) return;

  float cx = arcOverlay.cx;
  float cy = arcOverlay.cy;
  float r = arcOverlay.r;
  float thickness = static_cast<float>(arcOverlay.thickness);
  float inner = r - thickness;
  if (inner < 0.0f) inner = 0.0f;
  float outer = r;

  float baseStart = arcOverlay.start_deg;
  float baseEnd = arcOverlay.start_deg + arcOverlay.sweep_deg;
  float progEnd = arcOverlay.start_deg + arcOverlay.sweep_deg * arcOverlay.progress;

  int minx = static_cast<int>(std::floor(cx - outer - 1.0f));
  int maxx = static_cast<int>(std::ceil(cx + outer + 1.0f));
  if (maxx < 0 || minx >= width) return;
  if (minx < 0) minx = 0;
  if (maxx >= width) maxx = width - 1;

  for (int x = minx; x <= maxx; ++x) {
    float baseAccum = 0.0f;
    float progAccum = 0.0f;
    int samples = arcOverlay.aa_samples <= 1 ? 1 : 2;
    float offsets[2] = {0.5f, 0.5f};
    if (samples == 2) {
      offsets[0] = 0.25f;
      offsets[1] = 0.75f;
    }
    for (int sy = 0; sy < samples; ++sy) {
      float dy = (static_cast<float>(y) + offsets[sy]) - cy;
      for (int sx = 0; sx < samples; ++sx) {
        float dx = (static_cast<float>(x) + offsets[sx]) - cx;
        float dist = std::sqrt(dx * dx + dy * dy);
        if (dist > outer + 1.0f || dist < inner - 1.0f) continue;

        float alpha = 0.0f;
        if (dist >= inner && dist <= outer) {
          alpha = 1.0f;
        } else if (dist >= inner - 1.0f && dist < inner) {
          alpha = dist - (inner - 1.0f);
        } else if (dist > outer && dist <= outer + 1.0f) {
          alpha = 1.0f - (dist - outer);
        }
        if (alpha <= 0.0f) continue;

        float angle = std::atan2(dy, dx) * 180.0f / 3.1415926f;
        if (angle < 0.0f) angle += 360.0f;

        if (angle_in_range(angle, baseStart, baseEnd)) {
          baseAccum += alpha;
        }
        if (angle_in_range(angle, baseStart, progEnd)) {
          progAccum += alpha;
        }
      }
    }

    const float sampleScale = 1.0f / static_cast<float>(samples * samples);
    float baseAlpha = (baseAccum * sampleScale) * 0.2f;
    float progAlpha = (progAccum * sampleScale);
    float finalAlpha = (progAlpha > baseAlpha) ? progAlpha : baseAlpha;
    if (finalAlpha <= 0.0f) continue;
    finalAlpha = ditherAlpha(finalAlpha, x, y);
    line[x] = blend565(line[x], fgColor, finalAlpha);
  }
}

void HALAstraESP32::drawArcOverlayFrame() {
  if (!arcOverlay.enabled || !framebuf) return;
  if (arcOverlay.aa_samples <= 1) {
    drawArcOverlayFrameFast();
    return;
  }
  int miny = static_cast<int>(std::floor(arcOverlay.cy - arcOverlay.r - 1.0f));
  int maxy = static_cast<int>(std::ceil(arcOverlay.cy + arcOverlay.r + 1.0f));
  if (miny < 0) miny = 0;
  if (maxy >= SCREEN_H) maxy = SCREEN_H - 1;
  for (int y = miny; y <= maxy; ++y) {
    drawArcOverlayLine(y, framebuf + y * SCREEN_W, SCREEN_W);
  }
}

void HALAstraESP32::drawArcOverlayFrameFast() {
  if (!arcOverlay.enabled || !framebuf) return;

  const float cx = arcOverlay.cx;
  const float cy = arcOverlay.cy;
  const float r = arcOverlay.r;
  const int thickness = std::max(1, arcOverlay.thickness);
  const float baseStart = arcOverlay.start_deg;
  const float baseEnd = arcOverlay.start_deg + arcOverlay.sweep_deg;
  const float progEnd = arcOverlay.start_deg + arcOverlay.sweep_deg * arcOverlay.progress;
  const float baseStepDeg = std::max(0.8f, 1.2f * 57.2958f / std::max(1.0f, r));
  const float progStepDeg = std::max(0.35f, 0.7f * 57.2958f / std::max(1.0f, r));

  auto plot = [&](int px, int py, float alpha) {
    if (alpha <= 0.0f) return;
    if (px < 0 || px >= SCREEN_W || py < 0 || py >= SCREEN_H) return;
    uint16_t &dst = framebuf[py * SCREEN_W + px];
    float a = ditherAlpha(alpha, px, py);
    dst = blend565(dst, fgColor, a);
  };

  auto drawLine = [&](int x0, int y0, int x1, int y1, float alpha) {
    int dx = std::abs(x1 - x0);
    int dy = -std::abs(y1 - y0);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx + dy;
    int x = x0;
    int y = y0;
    while (true) {
      plot(x, y, alpha);
      if (x == x1 && y == y1) break;
      int e2 = err * 2;
      if (e2 >= dy) {
        err += dy;
        x += sx;
      }
      if (e2 <= dx) {
        err += dx;
        y += sy;
      }
    }
  };

  auto drawSegment = [&](float startDeg, float endDeg, float alpha, float step) {
    if (endDeg < startDeg) return;
    const float rad0 = startDeg * 3.1415926f / 180.0f;
    float c0 = std::cos(rad0);
    float s0 = std::sin(rad0);
    for (int t = 0; t < thickness; ++t) {
      float rr = r - static_cast<float>(t);
      int prevX = static_cast<int>(std::round(cx + c0 * rr));
      int prevY = static_cast<int>(std::round(cy + s0 * rr));
      for (float deg = startDeg + step; deg <= endDeg + 0.0001f; deg += step) {
        float rad = deg * 3.1415926f / 180.0f;
        float c = std::cos(rad);
        float s = std::sin(rad);
        int px = static_cast<int>(std::round(cx + c * rr));
        int py = static_cast<int>(std::round(cy + s * rr));
        drawLine(prevX, prevY, px, py, alpha);
        prevX = px;
        prevY = py;
      }
    }
  };

  auto drawBase = [&](float startDeg, float endDeg, float alpha) {
    if (endDeg < startDeg) return;
    float step = baseStepDeg * 2.5f;
    for (float deg = startDeg; deg <= endDeg; deg += step) {
      float rad = deg * 3.1415926f / 180.0f;
      float c = std::cos(rad);
      float s = std::sin(rad);
      for (int t = 0; t < thickness; ++t) {
        float rr = r - static_cast<float>(t);
        int px = static_cast<int>(std::round(cx + c * rr));
        int py = static_cast<int>(std::round(cy + s * rr));
        plot(px, py, alpha);
      }
    }
  };

  drawBase(baseStart, baseEnd, 0.2f);
  drawSegment(baseStart, progEnd, 1.0f, progStepDeg);
}

void HALAstraESP32::drawImageOverlayLine(int y, uint16_t *line, int width) {
  if (!imageOverlay.enabled || !imageOverlay.data || !line) return;
  int oy = imageOverlay.y;
  int oh = imageOverlay.h;
  if (y < oy || y >= oy + oh) return;
  int row = y - oy;
  int ox = imageOverlay.x;
  int ow = imageOverlay.w;
  int srcX = 0;
  int dstX = ox;
  if (dstX < 0) {
    srcX = -dstX;
    dstX = 0;
  }
  int copyW = ow - srcX;
  if (dstX + copyW > width) {
    copyW = width - dstX;
  }
  if (copyW <= 0) return;
  const uint16_t *src = imageOverlay.data + row * ow + srcX;
  memcpy(line + dstX, src, copyW * sizeof(uint16_t));
}

void HALAstraESP32::drawImageOverlayFrame() {
  if (!imageOverlay.enabled || !imageOverlay.data || !framebuf) return;
  int ox = imageOverlay.x;
  int oy = imageOverlay.y;
  int ow = imageOverlay.w;
  int oh = imageOverlay.h;
  if (ow <= 0 || oh <= 0) return;
  int srcX = 0;
  int dstX = ox;
  if (dstX < 0) {
    srcX = -dstX;
    dstX = 0;
  }
  int copyW = ow - srcX;
  if (dstX + copyW > SCREEN_W) {
    copyW = SCREEN_W - dstX;
  }
  if (copyW <= 0) return;
  for (int row = 0; row < oh; ++row) {
    int dstY = oy + row;
    if (dstY < 0 || dstY >= SCREEN_H) continue;
    const uint16_t *src = imageOverlay.data + row * ow + srcX;
    memcpy(framebuf + dstY * SCREEN_W + dstX, src, copyW * sizeof(uint16_t));
  }
}

void HALAstraESP32::drawBarOverlayLine(int y, uint16_t *line, int width) {
  if (!barOverlay.enabled || !line || barOverlay.w <= 0 || barOverlay.h <= 0) return;
  int oy = barOverlay.y;
  int oh = barOverlay.h;
  if (y < oy || y >= oy + oh) return;
  int ox = barOverlay.x;
  int ow = barOverlay.w;
  int startX = std::max(0, ox);
  int endX = std::min(width, ox + ow);
  if (endX <= startX) return;

  int fillW = static_cast<int>(std::round(barOverlay.progress * static_cast<float>(ow)));
  if (fillW < 0) fillW = 0;
  if (fillW > ow) fillW = ow;

  for (int x = startX; x < endX; ++x) {
    int localX = x - ox;
    uint16_t out = barOverlay.bg;
    if (barOverlay.progress > 0.0f && localX < fillW) {
      out = barOverlay.fg;
    }
    line[x] = out;
  }
}

void HALAstraESP32::init_inputs() {
  gpio_config_t in_conf = {
      .pin_bit_mask = (1ULL << PIN_ENC_A) | (1ULL << PIN_ENC_B) | (1ULL << PIN_ENC_SW) | (1ULL << PIN_BTN_BACK),
      .mode = GPIO_MODE_INPUT,
      .pull_up_en = GPIO_PULLUP_ENABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE};
  gpio_config(&in_conf);

  encState = (static_cast<uint8_t>(gpio_get_level(PIN_ENC_A)) << 1) |
             static_cast<uint8_t>(gpio_get_level(PIN_ENC_B));

  xTaskCreatePinnedToCore(encoder_task, "encoder", 2048, this, 1, nullptr, 1);
}

void HALAstraESP32::setArcOverlay(const ArcOverlay &overlay) {
  if (!overlay.enabled) {
    arcOverlay.enabled = false;
    return;
  }
  arcOverlay = overlay;
  arcOverlay.cx = overlay.cx * UI_SCALE;
  arcOverlay.cy = overlay.cy * UI_SCALE;
  arcOverlay.r = overlay.r * UI_SCALE;
  arcOverlay.thickness = std::max(1, overlay.thickness * UI_SCALE);
  if (arcOverlay.aa_samples < 1) arcOverlay.aa_samples = 1;
  if (arcOverlay.aa_samples > 2) arcOverlay.aa_samples = 2;
}

void HALAstraESP32::clearArcOverlay() {
  arcOverlay.enabled = false;
}

void HALAstraESP32::setImageOverlay(const uint16_t *data, int w, int h, int x, int y) {
  if (!data || w <= 0 || h <= 0) {
    imageOverlay.enabled = false;
    imageOverlay.data = nullptr;
    return;
  }
  imageOverlay.enabled = true;
  imageOverlay.data = data;
  imageOverlay.w = w;
  imageOverlay.h = h;
  imageOverlay.x = x;
  imageOverlay.y = y;
}

void HALAstraESP32::clearImageOverlay() {
  imageOverlay.enabled = false;
  imageOverlay.data = nullptr;
}

void HALAstraESP32::setBarOverlay(const BarOverlay &overlay) {
  if (!overlay.enabled || overlay.w <= 0 || overlay.h <= 0) {
    barOverlay.enabled = false;
    return;
  }
  barOverlay = overlay;
}

void HALAstraESP32::clearBarOverlay() {
  barOverlay.enabled = false;
}

uint16_t HALAstraESP32::getBackgroundColor() const {
  return COLOR_BG;
}

void HALAstraESP32::setForegroundColor(uint16_t color) {
  fgColor = color;
}

uint16_t HALAstraESP32::getForegroundColor() const {
  return fgColor;
}

void HALAstraESP32::encoder_task(void *arg) {
  auto *self = static_cast<HALAstraESP32 *>(arg);
  while (self) {
    self->updateEncoder();
    vTaskDelay(pdMS_TO_TICKS(2));
  }
}

void HALAstraESP32::updateEncoder() {
  static const int8_t ENC_TABLE[16] = {
      0, -1, 1, 0,
      1, 0, 0, -1,
      -1, 0, 0, 1,
      0, 1, -1, 0};

  uint8_t a = static_cast<uint8_t>(gpio_get_level(PIN_ENC_A));
  uint8_t b = static_cast<uint8_t>(gpio_get_level(PIN_ENC_B));
  uint8_t state = (a << 1) | b;
  if (state != encState) {
    uint8_t idx = (encState << 2) | state;
    int8_t delta = ENC_TABLE[idx];
    if (delta != 0) {
      portENTER_CRITICAL(&encMux);
      encDelta += delta;
      portEXIT_CRITICAL(&encMux);
    }
    encState = state;
    encLastMoveMs = _millis();
  }
}

int HALAstraESP32::readEncoderSteps() {
  const int stepsPerDetent = 4;
  const int partialThreshold = 2;
  const uint32_t partialTimeoutMs = 60;
  const uint32_t syntheticCooldownMs = 120;
  int16_t delta;
  portENTER_CRITICAL(&encMux);
  delta = encDelta;
  portEXIT_CRITICAL(&encMux);

  int steps = delta / stepsPerDetent;
  if (steps != 0) {
    portENTER_CRITICAL(&encMux);
    encDelta = static_cast<int16_t>(encDelta - steps * stepsPerDetent);
    portEXIT_CRITICAL(&encMux);
    return steps;
  }

  uint32_t now = _millis();
  int absDelta = (delta >= 0 ? delta : -delta);
  if (now < encSyntheticUntilMs && absDelta < stepsPerDetent) {
    return 0;
  }
  if (absDelta >= partialThreshold) {
    if (now - encLastMoveMs >= partialTimeoutMs) {
      int step = (delta > 0) ? 1 : -1;
      portENTER_CRITICAL(&encMux);
      encDelta = 0;
      portEXIT_CRITICAL(&encMux);
      encSyntheticUntilMs = now + syntheticCooldownMs;
      return step;
    }
  }

  return 0;
}

bool HALAstraESP32::readButton(gpio_num_t pin) {
  return gpio_get_level(pin) == 0;
}

void *HALAstraESP32::_getCanvasBuffer() {
  return u8g2_GetBufferPtr(&u8g2);
}

unsigned char HALAstraESP32::_getBufferTileHeight() {
  return static_cast<unsigned char>(tile_height);
}

unsigned char HALAstraESP32::_getBufferTileWidth() {
  return static_cast<unsigned char>(tile_width);
}

void HALAstraESP32::_canvasUpdate() {
  if (!u8g2_buf || !linebuf || !panel) return;

  const uint8_t *buf = u8g2_buf;
  if (displayMutex) xSemaphoreTake(displayMutex, portMAX_DELAY);

  if (displayConfig.use_framebuffer && framebuf) {
    // Render to back buffer if double buffering is enabled
    uint16_t *render_target = (displayConfig.use_double_buffer && backbuf) ? backbuf : framebuf;
    
    for (int ly = 0; ly < LOGICAL_H; ++ly) {
      const int tile_y = ly >> 3;
      const uint8_t mask = static_cast<uint8_t>(1u << (ly & 7));
      const uint8_t *row_tiles = buf + (tile_y * tile_width * 8);
      int filled = 0;
      for (int tile_x = 0; tile_x < tile_width; ++tile_x) {
        const uint8_t *tile = row_tiles + tile_x * 8;
        for (int col = 0; col < 8; ++col) {
          const int lx = tile_x * 8 + col;
          if (lx >= LOGICAL_W) break;
          const int start = lx * UI_SCALE;
          if (start >= SCREEN_W) break;
          const int end = std::min(start + UI_SCALE, SCREEN_W);
          uint16_t color = (tile[col] & mask) ? fgColor : COLOR_BG;
          for (int sx = start; sx < end; ++sx) {
            linebuf[sx] = color;
          }
          filled = end;
        }
      }
      for (int sx = filled; sx < SCREEN_W; ++sx) {
        linebuf[sx] = COLOR_BG;
      }
      const int base_y = ly * UI_SCALE;
      const int y_end = std::min(base_y + UI_SCALE, SCREEN_H);
      for (int y = base_y; y < y_end; ++y) {
        memcpy(render_target + y * SCREEN_W, linebuf, SCREEN_W * sizeof(uint16_t));
      }
    }
    
    uint16_t *saved = framebuf;
    framebuf = render_target;
    drawArcOverlayFrame();
    drawImageOverlayFrame();
    if (barOverlay.enabled) {
      int startY = std::max(0, barOverlay.y);
      int endY = std::min(SCREEN_H, barOverlay.y + barOverlay.h);
      for (int y = startY; y < endY; ++y) {
        drawBarOverlayLine(y, framebuf + y * SCREEN_W, SCREEN_W);
      }
    }
    framebuf = saved;
    
    // Swap buffers if double buffering
    if (displayConfig.use_double_buffer && backbuf) {
      std::swap(framebuf, backbuf);
      current_buffer = 1 - current_buffer;
    }
    
    s_flush_busy = true;
    esp_lcd_panel_draw_bitmap(reinterpret_cast<esp_lcd_panel_handle_t>(panel),
                              0, 0, SCREEN_W, SCREEN_H, framebuf);
    while (s_flush_busy) {
      vTaskDelay(pdMS_TO_TICKS(1));
    }
  } else {
    for (int ly = 0; ly < LOGICAL_H; ++ly) {
      const int tile_y = ly >> 3;
      const uint8_t mask = static_cast<uint8_t>(1u << (ly & 7));
      const uint8_t *row_tiles = buf + (tile_y * tile_width * 8);
      int filled = 0;
      for (int tile_x = 0; tile_x < tile_width; ++tile_x) {
        const uint8_t *tile = row_tiles + tile_x * 8;
        for (int col = 0; col < 8; ++col) {
          const int lx = tile_x * 8 + col;
          if (lx >= LOGICAL_W) break;
          const int start = lx * UI_SCALE;
          if (start >= SCREEN_W) break;
          const int end = std::min(start + UI_SCALE, SCREEN_W);
          uint16_t color = (tile[col] & mask) ? fgColor : COLOR_BG;
          for (int sx = start; sx < end; ++sx) {
            linebuf[sx] = color;
          }
          filled = end;
        }
      }
      for (int sx = filled; sx < SCREEN_W; ++sx) {
        linebuf[sx] = COLOR_BG;
      }
      const int base_y = ly * UI_SCALE;
      const int y_end = std::min(base_y + UI_SCALE, SCREEN_H);
      for (int y = base_y; y < y_end; ++y) {
        drawArcOverlayLine(y, linebuf, SCREEN_W);
        drawImageOverlayLine(y, linebuf, SCREEN_W);
        drawBarOverlayLine(y, linebuf, SCREEN_W);
        s_flush_busy = true;
        esp_lcd_panel_draw_bitmap(reinterpret_cast<esp_lcd_panel_handle_t>(panel),
                                  0, y, SCREEN_W, y + 1, linebuf);
        while (s_flush_busy) {
          vTaskDelay(pdMS_TO_TICKS(1));
        }
      }
    }
  }
  if (displayMutex) xSemaphoreGive(displayMutex);
}

void HALAstraESP32::_canvasClear() {
  if (!u8g2_buf) return;
  memset(u8g2_buf, 0x00, tile_width * tile_height * 8);
}

void HALAstraESP32::_setFont(const unsigned char *_font) {
  u8g2_SetFontMode(&u8g2, 1);
  u8g2_SetFontDirection(&u8g2, 0);
  u8g2_SetFont(&u8g2, _font);
}

unsigned char HALAstraESP32::_getFontWidth(std::string &_text) {
  return u8g2_GetUTF8Width(&u8g2, _text.c_str());
}

unsigned char HALAstraESP32::_getFontHeight() {
  return u8g2_GetMaxCharHeight(&u8g2);
}

void HALAstraESP32::_setDrawType(unsigned char _type) {
  u8g2_SetDrawColor(&u8g2, _type);
}

void HALAstraESP32::_drawPixel(float _x, float _y) {
  u8g2_DrawPixel(&u8g2,
                 static_cast<int16_t>(std::round(_x)),
                 static_cast<int16_t>(std::round(_y)));
}

void HALAstraESP32::_drawCircle(float _x, float _y, float _r) {
  if (_r <= 0.5f) return;
  u8g2_DrawCircle(&u8g2,
                  static_cast<int16_t>(std::round(_x)),
                  static_cast<int16_t>(std::round(_y)),
                  static_cast<int16_t>(std::round(_r)),
                  U8G2_DRAW_ALL);
}

void HALAstraESP32::_drawEnglish(float _x, float _y, const std::string &_text) {
  u8g2_DrawStr(&u8g2,
               static_cast<int16_t>(std::round(_x)),
               static_cast<int16_t>(std::round(_y)),
               _text.c_str());
}

void HALAstraESP32::_drawChinese(float _x, float _y, const std::string &_text) {
  u8g2_DrawUTF8(&u8g2,
                static_cast<int16_t>(std::round(_x)),
                static_cast<int16_t>(std::round(_y)),
                _text.c_str());
}

void HALAstraESP32::_setClipWindow(int _x0, int _y0, int _x1, int _y1) {
  u8g2_SetClipWindow(&u8g2,
                     static_cast<u8g2_uint_t>(_x0),
                     static_cast<u8g2_uint_t>(_y0),
                     static_cast<u8g2_uint_t>(_x1),
                     static_cast<u8g2_uint_t>(_y1));
}

void HALAstraESP32::_clearClipWindow() {
  u8g2_SetMaxClipWindow(&u8g2);
}

void HALAstraESP32::_drawVDottedLine(float _x, float _y, float _h) {
  for (unsigned char i = 0; i < static_cast<unsigned char>(std::round(_h)); i++) {
    if (i % 8 == 0 || (i - 1) % 8 == 0 || (i - 2) % 8 == 0) continue;
    u8g2_DrawPixel(&u8g2,
                   static_cast<int16_t>(std::round(_x)),
                   static_cast<int16_t>(std::round(_y)) + i);
  }
}

void HALAstraESP32::_drawHDottedLine(float _x, float _y, float _l) {
  for (unsigned char i = 0; i < static_cast<unsigned char>(std::round(_l)); i++) {
    if (i % 8 == 0 || (i - 1) % 8 == 0 || (i - 2) % 8 == 0) continue;
    u8g2_DrawPixel(&u8g2,
                   static_cast<int16_t>(std::round(_x)) + i,
                   static_cast<int16_t>(std::round(_y)));
  }
}

void HALAstraESP32::_drawVLine(float _x, float _y, float _h) {
  u8g2_DrawVLine(&u8g2,
                 static_cast<int16_t>(std::round(_x)),
                 static_cast<int16_t>(std::round(_y)),
                 static_cast<int16_t>(std::round(_h)));
}

void HALAstraESP32::_drawHLine(float _x, float _y, float _l) {
  u8g2_DrawHLine(&u8g2,
                 static_cast<int16_t>(std::round(_x)),
                 static_cast<int16_t>(std::round(_y)),
                 static_cast<int16_t>(std::round(_l)));
}

void HALAstraESP32::_drawBMP(float _x, float _y, float _w, float _h, const unsigned char *_bitMap) {
  u8g2_DrawXBMP(&u8g2,
                static_cast<int16_t>(std::round(_x)),
                static_cast<int16_t>(std::round(_y)),
                static_cast<int16_t>(std::round(_w)),
                static_cast<int16_t>(std::round(_h)),
                _bitMap);
}

void HALAstraESP32::_drawBox(float _x, float _y, float _w, float _h) {
  u8g2_DrawBox(&u8g2,
               static_cast<int16_t>(std::round(_x)),
               static_cast<int16_t>(std::round(_y)),
               static_cast<int16_t>(std::round(_w)),
               static_cast<int16_t>(std::round(_h)));
}

void HALAstraESP32::_drawRBox(float _x, float _y, float _w, float _h, float _r) {
  u8g2_DrawRBox(&u8g2,
                static_cast<int16_t>(std::round(_x)),
                static_cast<int16_t>(std::round(_y)),
                static_cast<int16_t>(std::round(_w)),
                static_cast<int16_t>(std::round(_h)),
                static_cast<int16_t>(std::round(_r)));
}

void HALAstraESP32::_drawFrame(float _x, float _y, float _w, float _h) {
  u8g2_DrawFrame(&u8g2,
                 static_cast<int16_t>(std::round(_x)),
                 static_cast<int16_t>(std::round(_y)),
                 static_cast<int16_t>(std::round(_w)),
                 static_cast<int16_t>(std::round(_h)));
}

void HALAstraESP32::_drawRFrame(float _x, float _y, float _w, float _h, float _r) {
  u8g2_DrawRFrame(&u8g2,
                  static_cast<int16_t>(std::round(_x)),
                  static_cast<int16_t>(std::round(_y)),
                  static_cast<int16_t>(std::round(_w)),
                  static_cast<int16_t>(std::round(_h)),
                  static_cast<int16_t>(std::round(_r)));
}

void HALAstraESP32::_delay(unsigned long _mill) {
  vTaskDelay(pdMS_TO_TICKS(_mill));
}

unsigned long HALAstraESP32::_millis() {
  return static_cast<unsigned long>(esp_timer_get_time() / 1000ULL);
}

unsigned long HALAstraESP32::_getTick() {
  return _millis();
}

unsigned long HALAstraESP32::_getRandomSeed() {
  return esp_random();
}

void HALAstraESP32::_screenOn() {
  gpio_set_level(PIN_TFT_BL, 1);
}

void HALAstraESP32::_screenOff() {
  gpio_set_level(PIN_TFT_BL, 0);
}

HALAstraESP32::DisplayConfig HALAstraESP32::getDisplayConfig() const {
  return displayConfig;
}

bool HALAstraESP32::applyDisplayConfig(const DisplayConfig &config) {
  DisplayConfig next = config;
  next.spi_hz = clamp_spi_hz(next.spi_hz);

  bool need_reinit = (next.spi_hz != displayConfig.spi_hz) ||
                     (next.use_dma != displayConfig.use_dma) ||
                     (next.use_framebuffer != displayConfig.use_framebuffer) ||
                     (next.use_psram != displayConfig.use_psram) ||
                     (next.use_double_buffer != displayConfig.use_double_buffer);

  ESP_LOGI("HAL", "applyConfig: SPI %d->%d, DMA %d->%d, FB %d->%d, PSRAM %d->%d, 2xBuf %d->%d, reinit=%d",
           displayConfig.spi_hz/1000000, next.spi_hz/1000000,
           displayConfig.use_dma, next.use_dma,
           displayConfig.use_framebuffer, next.use_framebuffer,
           displayConfig.use_psram, next.use_psram,
           displayConfig.use_double_buffer, next.use_double_buffer,
           need_reinit);

  displayConfig = next;
  adjust_dma_for_buffers();

  if (!need_reinit) {
    return true;
  }

  if (displayMutex) xSemaphoreTake(displayMutex, portMAX_DELAY);
  vTaskDelay(pdMS_TO_TICKS(80));
  if (panel) {
    esp_lcd_panel_disp_on_off(reinterpret_cast<esp_lcd_panel_handle_t>(panel), false);
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  deinit_display();
  bool ok = init_display();
  if (displayMutex) xSemaphoreGive(displayMutex);
  return ok;
}

bool HALAstraESP32::_getKey(key::KEY_INDEX _keyIndex) {
  if (_keyIndex == key::KEY_0) {
    return readButton(PIN_BTN_BACK);
  }
  if (_keyIndex == key::KEY_1) {
    return readButton(PIN_ENC_SW);
  }
  return false;
}

void HALAstraESP32::_keyScan() {
  const uint32_t now = _millis();
  const uint32_t debounce_ms = 20;

  bool backNow = readButton(PIN_BTN_BACK);
  if (backNow != btnBackLast) {
    btnBackLast = backNow;
    btnBackChangeMs = now;
  }
  if ((now - btnBackChangeMs) > debounce_ms && backNow != btnBackState) {
    btnBackState = backNow;
    if (btnBackState) btnBackPressed = true;
  }

  bool selNow = readButton(PIN_ENC_SW);
  if (selNow != btnSelectLast) {
    btnSelectLast = selNow;
    btnSelectChangeMs = now;
  }
  if ((now - btnSelectChangeMs) > debounce_ms && selNow != btnSelectState) {
    btnSelectState = selNow;
    if (btnSelectState) btnSelectPressed = true;
  }

  std::fill(key, key + key::KEY_NUM, key::INVALID);
  keyFlag = key::KEY_NOT_PRESSED;

  if (pendingSteps == 0) {
    pendingSteps = readEncoderSteps();
  }
  if (pendingSteps != 0) {
    if (pendingSteps < 0) {
      key[key::KEY_0] = key::CLICK;
      pendingSteps++;
    } else {
      key[key::KEY_1] = key::CLICK;
      pendingSteps--;
    }
    keyFlag = key::KEY_PRESSED;
    return;
  }

  if (btnBackPressed) {
    key[key::KEY_0] = key::PRESS;
    keyFlag = key::KEY_PRESSED;
    btnBackPressed = false;
    return;
  }
  if (btnSelectPressed) {
    key[key::KEY_1] = key::PRESS;
    keyFlag = key::KEY_PRESSED;
    btnSelectPressed = false;
    return;
  }
}
