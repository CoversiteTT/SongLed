#pragma once

#include <cstdint>
#include "hal/hal.h"
#include "u8g2.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"

class HALAstraESP32 final : public HAL {
public:
  struct DisplayConfig {
    int spi_hz;
    bool use_dma;
    bool use_framebuffer;
    bool use_psram;
    bool use_double_buffer;
  };

  struct ArcOverlay {
    bool enabled;
    float cx;
    float cy;
    float r;
    float start_deg;
    float sweep_deg;
    float progress;
    int thickness;
    uint8_t aa_samples; // 1 = no supersampling, 2 = 2x2
  };

  struct ImageOverlay {
    bool enabled;
    int x;
    int y;
    int w;
    int h;
    const uint16_t *data;
  };

  struct BarOverlay {
    bool enabled;
    int x;
    int y;
    int w;
    int h;
    float progress;    // 0..1
    float angle_deg;   // gradient angle
    uint16_t fg;
    uint16_t bg;
  };

  HALAstraESP32();
  ~HALAstraESP32() override;

  std::string type() override { return "ESP32S3"; }

  void init() override;
  void lcdFill(uint16_t color);

  void *_getCanvasBuffer() override;
  unsigned char _getBufferTileHeight() override;
  unsigned char _getBufferTileWidth() override;
  void _canvasUpdate() override;
  void _canvasClear() override;
  void _setFont(const unsigned char *_font) override;
  unsigned char _getFontWidth(std::string &_text) override;
  unsigned char _getFontHeight() override;
  void _setDrawType(unsigned char _type) override;
  void _drawPixel(float _x, float _y) override;
  void _drawCircle(float _x, float _y, float _r) override;
  void _drawEnglish(float _x, float _y, const std::string &_text) override;
  void _drawChinese(float _x, float _y, const std::string &_text) override;
  void _setClipWindow(int _x0, int _y0, int _x1, int _y1) override;
  void _clearClipWindow() override;
  void _drawVDottedLine(float _x, float _y, float _h) override;
  void _drawHDottedLine(float _x, float _y, float _l) override;
  void _drawVLine(float _x, float _y, float _h) override;
  void _drawHLine(float _x, float _y, float _l) override;
  void _drawBMP(float _x, float _y, float _w, float _h, const unsigned char *_bitMap) override;
  void _drawBox(float _x, float _y, float _w, float _h) override;
  void _drawRBox(float _x, float _y, float _w, float _h, float _r) override;
  void _drawFrame(float _x, float _y, float _w, float _h) override;
  void _drawRFrame(float _x, float _y, float _w, float _h, float _r) override;

  void _delay(unsigned long _mill) override;
  unsigned long _millis() override;
  unsigned long _getTick() override;
  unsigned long _getRandomSeed() override;

  void _screenOn() override;
  void _screenOff() override;

  bool _getKey(key::KEY_INDEX _keyIndex) override;
  void _keyScan() override;

  DisplayConfig getDisplayConfig() const;
  bool applyDisplayConfig(const DisplayConfig &config);
  void setArcOverlay(const ArcOverlay &overlay);
  void clearArcOverlay();
  void setImageOverlay(const uint16_t *data, int w, int h, int x, int y);
  void clearImageOverlay();
  void setBarOverlay(const BarOverlay &overlay);
  void clearBarOverlay();
  uint16_t getBackgroundColor() const;
  void setForegroundColor(uint16_t color);
  uint16_t getForegroundColor() const;

private:
  bool init_display();
  void deinit_display();
  bool init_buffers_once();
  void adjust_dma_for_buffers();
  int clamp_spi_hz(int hz) const;
  void init_inputs();
  void updateEncoder();
  int readEncoderSteps();
  bool readButton(gpio_num_t pin);
  void drawArcOverlayLine(int y, uint16_t *line, int width);
  void drawArcOverlayFrame();
  void drawArcOverlayFrameFast();
  void drawImageOverlayLine(int y, uint16_t *line, int width);
  void drawImageOverlayFrame();
  void drawBarOverlayLine(int y, uint16_t *line, int width);
  uint16_t blend565(uint16_t bg, uint16_t fg, float alpha) const;
  float ditherAlpha(float alpha, int x, int y) const;

  static void encoder_task(void *arg);

private:
  void *panel = nullptr;
  void *panel_io = nullptr;

  uint16_t *linebuf = nullptr;
  uint16_t *framebuf = nullptr;
  uint16_t *backbuf = nullptr;  // Second buffer for double buffering
  uint8_t current_buffer = 0;   // 0 or 1
  bool linebuf_dma = false;
  bool framebuf_dma = false;

  u8g2_t u8g2 {};
  uint8_t *u8g2_buf = nullptr;
  uint16_t tile_width = 0;
  uint16_t tile_height = 0;

  DisplayConfig displayConfig {80000000, true, true, false, true};  // 80MHz SPI, DMA+FB+Double Buffer ON
  SemaphoreHandle_t displayMutex = nullptr;
  ArcOverlay arcOverlay {false, 0, 0, 0, 0, 0, 0, 1, 1};
  ImageOverlay imageOverlay {false, 0, 0, 0, 0, nullptr};
  BarOverlay barOverlay {false, 0, 0, 0, 0, 0.0f, 0.0f, 0, 0};
  uint16_t fgColor = 0;

  uint8_t encState = 0;
  volatile int16_t encDelta = 0;
  portMUX_TYPE encMux = portMUX_INITIALIZER_UNLOCKED;
  int pendingSteps = 0;
  uint32_t encLastMoveMs = 0;
  uint32_t encSyntheticUntilMs = 0;
  uint32_t btnBackChangeMs = 0;
  uint32_t btnSelectChangeMs = 0;
  bool btnBackState = false;
  bool btnSelectState = false;
  bool btnBackLast = false;
  bool btnSelectLast = false;
  bool btnBackPressed = false;
  bool btnSelectPressed = false;

  static HALAstraESP32 *s_instance;
};
