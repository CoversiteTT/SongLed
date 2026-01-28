#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "driver/uart.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"

#include "font8x8_basic.h"

// ----------------------------
// Pin mapping (change as needed)
// ----------------------------
static const int PIN_TFT_MOSI = 11;
static const int PIN_TFT_SCLK = 12;
static const int PIN_TFT_CS = 10;
static const int PIN_TFT_DC = 9;
static const int PIN_TFT_RST = 7;
static const int PIN_TFT_BL = 14;

static const int PIN_ENC_A = 15;
static const int PIN_ENC_B = 16;
static const int PIN_ENC_SW = 17;
static const int PIN_BTN_BACK = 18; // K0

// ----------------------------
// Display setup
// ----------------------------
#define SCREEN_W 320
#define SCREEN_H 240
#define HEADER_H 36
#define TFT_SPI_HZ 20000000
#define FORCE_FULL_FLUSH 1
#define USE_FRAMEBUFFER 1
#define LCD_CMD_BITS 8
#define LCD_PARAM_BITS 8
#define MENU_ROW_H 28.0f
#define MENU_LIST_TOP (HEADER_H + 12)
#define MENU_LIST_BOTTOM (SCREEN_H - 10)
#define VOLUME_BAR_X 28
#define VOLUME_BAR_Y 120
#define VOLUME_BAR_W (SCREEN_W - 56)
#define VOLUME_BAR_H 16
#define VOLUME_TEXT_X 28
#define VOLUME_TEXT_Y 160
#define VOLUME_TEXT_W 90
#define VOLUME_TEXT_H 18

#define RGB565(r, g, b) (uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3))
static const uint16_t COLOR_BG = RGB565(12, 16, 24);
static const uint16_t COLOR_PANEL = RGB565(20, 26, 36);
static const uint16_t COLOR_TEXT = RGB565(235, 235, 235);
static const uint16_t COLOR_MUTED = RGB565(140, 150, 160);
static const uint16_t COLOR_ACCENT = RGB565(255, 160, 40);
static const uint16_t COLOR_HILITE = RGB565(40, 120, 200);
static const uint16_t COLOR_ALERT = RGB565(255, 64, 64);

static esp_lcd_panel_handle_t lcd_panel = NULL;
static esp_lcd_panel_io_handle_t lcd_io = NULL;
static uint16_t linebuf[SCREEN_W];
static uint16_t *framebuffer = NULL;
static volatile bool flush_busy = false;

// ----------------------------
// Font (8x8)
// ----------------------------
static const int FONT_W = 8;
static const int FONT_H = 8;

// ----------------------------
// UI structures
// ----------------------------
typedef enum
{
  ITEM_ACTION,
  ITEM_SUBMENU,
  ITEM_LABEL
} ItemType;

typedef struct Menu Menu;

typedef struct
{
  const char *label;
  ItemType type;
  void (*action)(void *item);
  Menu *submenu;
  int value;
} MenuItem;

typedef struct Menu
{
  const char *title;
  MenuItem *items;
  int itemCount;
  Menu *parent;
  int selected;
} Menu;

// ----------------------------
// Menu content
// ----------------------------
static char volumeLabel[20];
static char muteLabel[16];
static char fpsLabel[20];
static char cpuLabel[20];

static void actionOpenVolume(void *item);
static void actionOpenTest(void *item);
static void actionOpenColorTest(void *item);
static void actionToggleFps(void *item);
static void actionToggleCpu(void *item);
static void actionToggleMute(void *item);
static void actionRequestSpeakerList(void *item);
static void actionSelectSpeaker(void *item);

static Menu menuMain;
static Menu menuSpeakers;
static Menu menuTest;

static MenuItem mainItems[4];
static MenuItem testItems[4];

#define MAX_SPEAKERS 12
#define SPEAKER_NAME_LEN 32
static char speakerNames[MAX_SPEAKERS][SPEAKER_NAME_LEN];
static int speakerIds[MAX_SPEAKERS];
static MenuItem speakerItems[MAX_SPEAKERS + 2];
static int speakerCount = 0;
static int speakerCurrentId = -1;
static bool speakersLoading = false;

static bool showFps = false;
static bool showCpu = false;
static float fpsValue = 0.0f;
static int cpuValue = 0;
static uint32_t perfLastMs = 0;
static uint32_t fpsFrameCount = 0;
static uint64_t busyUsAccum = 0;
static uint64_t totalUsAccum = 0;
static uint64_t lastLoopUs = 0;
static uint32_t lastForceRefreshMs = 0;
static bool forceListRefresh = false;

static bool overlayDirty = false;
static bool overlayWasOn = false;
static int overlayX = 0;
static int overlayY = 0;
static int overlayW = 0;
static int overlayH = 0;

// ----------------------------
// Input handling
// ----------------------------
typedef struct
{
  int pin;
  bool state;
  bool lastRead;
  uint32_t lastChangeMs;
  bool pressedEvent;
} DebouncedButton;

static const uint16_t DEBOUNCE_MS = 25;
static DebouncedButton btnSelect = {PIN_ENC_SW, false, false, 0, false};
static DebouncedButton btnBack = {PIN_BTN_BACK, false, false, 0, false};

static uint8_t encState = 0;
static volatile int16_t encDelta = 0;
static portMUX_TYPE encMux = portMUX_INITIALIZER_UNLOCKED;
static const int8_t ENC_TABLE[16] = {
    0, -1, 1, 0,
    1, 0, 0, -1,
    -1, 0, 0, 1,
    0, 1, -1, 0};

// ----------------------------
// Serial protocol state
// ----------------------------
static char serialBuf[128];
static size_t serialLen = 0;

// ----------------------------
// UI state
// ----------------------------
typedef enum
{
  MODE_MENU,
  MODE_VOLUME,
  MODE_TEST,
  MODE_COLOR
} UiMode;

typedef struct
{
  bool active;
  Menu *from;
  Menu *to;
  int direction;
  uint32_t startMs;
} Transition;

typedef struct
{
  UiMode mode;
  Menu *current;
  float selectionPos;
  float selectionFrom;
  float selectionTo;
  uint32_t selectionStartMs;
  bool selectionAnimating;
  float scrollPos;
  Transition transition;
  bool needsRedraw;
  bool fullRedraw;
} UiState;

static UiState ui;

static int volumeValue = 50;
static float volumeDisplay = 50.0f;
static bool muteState = false;
static uint32_t testFlashMs = 0;
static int colorTestIndex = 0;

static const uint32_t FRAME_MS = 16;
static uint32_t lastFrameMs = 0;

// ----------------------------
// Helpers
// ----------------------------
static uint32_t millis(void)
{
  return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

static float clampf(float v, float lo, float hi)
{
  if (v < lo)
    return lo;
  if (v > hi)
    return hi;
  return v;
}

static int clampi(int v, int lo, int hi)
{
  if (v < lo)
    return lo;
  if (v > hi)
    return hi;
  return v;
}

static float easeOutCubic(float t)
{
  float u = 1.0f - t;
  return 1.0f - (u * u * u);
}

static void triggerTestFlash(void)
{
  testFlashMs = millis();
  ui.needsRedraw = true;
}

static void copyTrunc(char *dst, const char *src, size_t dstSize)
{
  if (dstSize == 0)
    return;
  strncpy(dst, src, dstSize - 1);
  dst[dstSize - 1] = '\0';
}

// ----------------------------
// Display helpers
// ----------------------------
static void lcd_fill_rect(int x, int y, int w, int h, uint16_t color)
{
  if (w <= 0 || h <= 0)
    return;
  if (x < 0)
  {
    w += x;
    x = 0;
  }
  if (y < 0)
  {
    h += y;
    y = 0;
  }
  if (x + w > SCREEN_W)
    w = SCREEN_W - x;
  if (y + h > SCREEN_H)
    h = SCREEN_H - y;
  if (w <= 0 || h <= 0)
    return;

  if (framebuffer)
  {
    for (int row = 0; row < h; ++row)
    {
      uint16_t *dst = framebuffer + (y + row) * SCREEN_W + x;
      for (int col = 0; col < w; ++col)
        dst[col] = color;
    }
    return;
  }

  for (int i = 0; i < w; ++i)
    linebuf[i] = color;

  for (int row = 0; row < h; ++row)
  {
    esp_lcd_panel_draw_bitmap(lcd_panel, x, y + row, x + w, y + row + 1, linebuf);
  }
}

static void lcd_fill_rect_direct(int x, int y, int w, int h, uint16_t color)
{
  if (w <= 0 || h <= 0)
    return;
  if (x < 0)
  {
    w += x;
    x = 0;
  }
  if (y < 0)
  {
    h += y;
    y = 0;
  }
  if (x + w > SCREEN_W)
    w = SCREEN_W - x;
  if (y + h > SCREEN_H)
    h = SCREEN_H - y;
  if (w <= 0 || h <= 0)
    return;

  for (int i = 0; i < w; ++i)
    linebuf[i] = color;

  for (int row = 0; row < h; ++row)
  {
    esp_lcd_panel_draw_bitmap(lcd_panel, x, y + row, x + w, y + row + 1, linebuf);
  }
}

static bool lcd_on_color_trans_done(esp_lcd_panel_io_handle_t panel_io,
                                    esp_lcd_panel_io_event_data_t *edata,
                                    void *user_ctx)
{
  (void)panel_io;
  (void)edata;
  (void)user_ctx;
  flush_busy = false;
  return false;
}

static void lcd_draw_rect(int x, int y, int w, int h, uint16_t color)
{
  lcd_fill_rect(x, y, w, 1, color);
  lcd_fill_rect(x, y + h - 1, w, 1, color);
  lcd_fill_rect(x, y, 1, h, color);
  lcd_fill_rect(x + w - 1, y, 1, h, color);
}

static const uint8_t *get_glyph(char c)
{
  uint8_t uc = (uint8_t)c;
  if (uc >= 128)
    uc = '?';
  return font8x8_basic[uc];
}

static void lcd_draw_char(int x, int y, char c, uint16_t color, uint16_t bg, int scale)
{
  int cell_w = (FONT_W + 1) * scale;
  int cell_h = (FONT_H + 1) * scale;
  const uint8_t *rows = get_glyph(c);
  if (framebuffer)
  {
    for (int py = 0; py < cell_h; ++py)
    {
      int dst_y = y + py;
      if (dst_y < 0 || dst_y >= SCREEN_H)
        continue;
      uint16_t *dst = framebuffer + dst_y * SCREEN_W;
      int src_y = py / scale;
      for (int px = 0; px < cell_w; ++px)
      {
        int dst_x = x + px;
        if (dst_x < 0 || dst_x >= SCREEN_W)
          continue;
        int src_x = px / scale;
        bool on = (src_x < FONT_W && src_y < FONT_H) && (((rows[src_y] >> src_x) & 0x01) != 0);
        dst[dst_x] = on ? color : bg;
      }
    }
    return;
  }

  static uint16_t charbuf[32 * 32];
  if (cell_w > 32 || cell_h > 32)
    return;

  for (int py = 0; py < cell_h; ++py)
  {
    for (int px = 0; px < cell_w; ++px)
    {
      int src_x = px / scale;
      int src_y = py / scale;
      bool on = false;
      if (src_x < FONT_W && src_y < FONT_H)
      {
        on = ((rows[src_y] >> src_x) & 0x01) != 0;
      }
      charbuf[py * cell_w + px] = on ? color : bg;
    }
  }

  esp_lcd_panel_draw_bitmap(lcd_panel, x, y, x + cell_w, y + cell_h, charbuf);
}

static void lcd_draw_char_direct(int x, int y, char c, uint16_t color, uint16_t bg, int scale)
{
  int cell_w = (FONT_W + 1) * scale;
  int cell_h = (FONT_H + 1) * scale;
  const uint8_t *rows = get_glyph(c);

  static uint16_t charbuf[32 * 32];
  if (cell_w > 32 || cell_h > 32)
    return;

  for (int py = 0; py < cell_h; ++py)
  {
    for (int px = 0; px < cell_w; ++px)
    {
      int src_x = px / scale;
      int src_y = py / scale;
      bool on = false;
      if (src_x < FONT_W && src_y < FONT_H)
      {
        on = ((rows[src_y] >> src_x) & 0x01) != 0;
      }
      charbuf[py * cell_w + px] = on ? color : bg;
    }
  }

  esp_lcd_panel_draw_bitmap(lcd_panel, x, y, x + cell_w, y + cell_h, charbuf);
}

static void lcd_draw_text(int x, int y, const char *text, uint16_t color, uint16_t bg, int scale)
{
  int cursor = x;
  int step = (FONT_W + 1) * scale;
  while (*text)
  {
    lcd_draw_char(cursor, y, *text, color, bg, scale);
    cursor += step;
    text++;
  }
}

static void lcd_draw_text_direct(int x, int y, const char *text, uint16_t color, uint16_t bg, int scale)
{
  int cursor = x;
  int step = (FONT_W + 1) * scale;
  while (*text)
  {
    lcd_draw_char_direct(cursor, y, *text, color, bg, scale);
    cursor += step;
    text++;
  }
}

static void drawLabelClipped(const char *label, int x, int y, int maxWidth, uint16_t color, uint16_t bg)
{
  int scale = 2;
  int charW = (FONT_W + 1) * scale;
  int maxChars = maxWidth / charW;
  if (maxChars < 4)
    maxChars = 4;

  char buf[40];
  size_t len = strlen(label);
  if ((int)len > maxChars)
  {
    int keep = maxChars - 3;
    if (keep < 1)
      keep = 1;
    strncpy(buf, label, (size_t)keep);
    buf[keep] = '\0';
    strcat(buf, "...");
  }
  else
  {
    copyTrunc(buf, label, sizeof(buf));
  }

  lcd_draw_text(x, y, buf, color, bg, scale);
}

// ----------------------------
// Serial I/O
// ----------------------------
static void sendLine(const char *line)
{
  uart_write_bytes(UART_NUM_0, line, (int)strlen(line));
  uart_write_bytes(UART_NUM_0, "\n", 1);
  triggerTestFlash();
}

static void sendVolumeGet(void)
{
  sendLine("VOL GET");
}

static void sendVolumeSet(void)
{
  char line[24];
  snprintf(line, sizeof(line), "VOL SET %d", volumeValue);
  sendLine(line);
}

static void sendMuteToggle(void)
{
  sendLine("MUTE");
}

static void sendSpeakerListRequest(void)
{
  sendLine("SPK LIST");
}

static void sendSpeakerSet(int id)
{
  char line[24];
  snprintf(line, sizeof(line), "SPK SET %d", id);
  sendLine(line);
}

// ----------------------------
// Menu building
// ----------------------------
static void updateVolumeLabel(void)
{
  snprintf(volumeLabel, sizeof(volumeLabel), "Volume %d%%", volumeValue);
}

static void updateMuteLabel(void)
{
  snprintf(muteLabel, sizeof(muteLabel), "Mute %s", muteState ? "On" : "Off");
}

static void updateMainMenuItems(void)
{
  mainItems[0].label = volumeLabel;
  mainItems[1].label = "Output Device";
  mainItems[2].label = "Test Tools";
  mainItems[3].label = muteLabel;
}

static void updateTestMenuItems(void)
{
  snprintf(fpsLabel, sizeof(fpsLabel), "FPS %s", showFps ? "On" : "Off");
  snprintf(cpuLabel, sizeof(cpuLabel), "CPU %s", showCpu ? "On" : "Off");
  testItems[0].label = "IO Test";
  testItems[1].label = "Color Test";
  testItems[2].label = fpsLabel;
  testItems[3].label = cpuLabel;
}

static void updateSpeakerMenuItems(void)
{
  int idx = 0;
  speakerItems[idx++] = (MenuItem){"Refresh list", ITEM_ACTION, actionRequestSpeakerList, NULL, 0};

  if (speakersLoading)
  {
    speakerItems[idx++] = (MenuItem){"Loading...", ITEM_LABEL, NULL, NULL, 0};
  }
  else if (speakerCount == 0)
  {
    speakerItems[idx++] = (MenuItem){"No devices", ITEM_LABEL, NULL, NULL, 0};
  }
  else
  {
    for (int i = 0; i < speakerCount; ++i)
    {
      speakerItems[idx++] = (MenuItem){speakerNames[i], ITEM_ACTION, actionSelectSpeaker, NULL, speakerIds[i]};
    }
  }

  menuSpeakers.items = speakerItems;
  menuSpeakers.itemCount = idx;
  if (menuSpeakers.selected >= menuSpeakers.itemCount)
    menuSpeakers.selected = menuSpeakers.itemCount - 1;
  if (menuSpeakers.selected < 0)
    menuSpeakers.selected = 0;
  ui.fullRedraw = true;
}

// ----------------------------
// Input helpers
// ----------------------------
static void updateButton(DebouncedButton *btn)
{
  bool reading = (gpio_get_level(btn->pin) == 0);
  if (reading != btn->lastRead)
  {
    btn->lastRead = reading;
    btn->lastChangeMs = millis();
  }

  if ((millis() - btn->lastChangeMs) > DEBOUNCE_MS && reading != btn->state)
  {
    btn->state = reading;
    if (btn->state)
    {
      btn->pressedEvent = true;
    }
  }
}

static bool consumePressed(DebouncedButton *btn)
{
  if (btn->pressedEvent)
  {
    btn->pressedEvent = false;
    return true;
  }
  return false;
}

static void updateEncoder(void)
{
  uint8_t a = (uint8_t)gpio_get_level(PIN_ENC_A);
  uint8_t b = (uint8_t)gpio_get_level(PIN_ENC_B);
  uint8_t state = (a << 1) | b;
  if (state != encState)
  {
    uint8_t idx = (encState << 2) | state;
    int8_t delta = ENC_TABLE[idx];
    if (delta != 0)
    {
      portENTER_CRITICAL(&encMux);
      encDelta += delta;
      portEXIT_CRITICAL(&encMux);
    }
    encState = state;
  }
}

static void encoder_task(void *arg)
{
  (void)arg;
  encState = ((uint8_t)gpio_get_level(PIN_ENC_A) << 1) | (uint8_t)gpio_get_level(PIN_ENC_B);
  while (1)
  {
    updateEncoder();
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

static int readEncoderSteps(void)
{
  const int stepsPerDetent = 4;
  int16_t delta;
  portENTER_CRITICAL(&encMux);
  delta = encDelta;
  encDelta = (int16_t)(encDelta - (delta / stepsPerDetent) * stepsPerDetent);
  portEXIT_CRITICAL(&encMux);

  int steps = delta / stepsPerDetent;
  if (steps != 0)
  {
    // delta already reduced in critical section
  }
  return steps;
}

// ----------------------------
// UI actions
// ----------------------------
static void actionOpenVolume(void *item)
{
  (void)item;
  ui.mode = MODE_VOLUME;
  ui.fullRedraw = true;
  ui.needsRedraw = true;
  sendVolumeGet();
}

static void actionOpenTest(void *item)
{
  (void)item;
  ui.mode = MODE_TEST;
  ui.fullRedraw = true;
  ui.needsRedraw = true;
}

static void actionOpenColorTest(void *item)
{
  (void)item;
  ui.mode = MODE_COLOR;
  ui.fullRedraw = true;
  ui.needsRedraw = true;
}

static void actionToggleFps(void *item)
{
  (void)item;
  showFps = !showFps;
  updateTestMenuItems();
  overlayDirty = true;
  ui.needsRedraw = true;
}

static void actionToggleCpu(void *item)
{
  (void)item;
  showCpu = !showCpu;
  updateTestMenuItems();
  overlayDirty = true;
  ui.needsRedraw = true;
}

static void actionToggleMute(void *item)
{
  (void)item;
  sendMuteToggle();
}

static void actionRequestSpeakerList(void *item)
{
  (void)item;
  speakersLoading = true;
  speakerCount = 0;
  updateSpeakerMenuItems();
  ui.needsRedraw = true;
  sendSpeakerListRequest();
}

static void actionSelectSpeaker(void *item)
{
  MenuItem *menuItem = (MenuItem *)item;
  if (menuItem == NULL)
    return;
  speakerCurrentId = menuItem->value;
  sendSpeakerSet(menuItem->value);
  ui.needsRedraw = true;
}

// ----------------------------
// UI logic
// ----------------------------
static void startSelectionAnim(int newIndex)
{
  ui.selectionFrom = ui.selectionPos;
  ui.selectionTo = (float)newIndex;
  ui.selectionStartMs = millis();
  ui.selectionAnimating = true;
}

static void setMenuSelection(Menu *menu, int newIndex)
{
  if (menu->itemCount <= 0)
    return;
  while (newIndex < 0)
    newIndex += menu->itemCount;
  while (newIndex >= menu->itemCount)
    newIndex -= menu->itemCount;
  if (newIndex == menu->selected)
    return;
  menu->selected = newIndex;
  startSelectionAnim(newIndex);
  ui.needsRedraw = true;
}

static void startTransition(Menu *to, int direction)
{
  if (ui.transition.active)
    return;
  ui.transition.active = true;
  ui.transition.from = ui.current;
  ui.transition.to = to;
  ui.transition.direction = direction;
  ui.transition.startMs = millis();
  ui.fullRedraw = true;
  ui.needsRedraw = true;
}

static void handleSelect(void)
{
  if (ui.transition.active)
    return;
  if (ui.mode == MODE_VOLUME || ui.mode == MODE_TEST || ui.mode == MODE_COLOR)
  {
    ui.mode = MODE_MENU;
    ui.fullRedraw = true;
    ui.needsRedraw = true;
    return;
  }

  MenuItem *item = &ui.current->items[ui.current->selected];
  if (item->type == ITEM_SUBMENU && item->submenu != NULL)
  {
    startTransition(item->submenu, 1);
    if (item->submenu == &menuSpeakers)
    {
      actionRequestSpeakerList(NULL);
    }
  }
  else if (item->type == ITEM_ACTION && item->action != NULL)
  {
    item->action(item);
  }
}

static void handleBack(void)
{
  if (ui.transition.active)
    return;
  if (ui.mode == MODE_VOLUME || ui.mode == MODE_TEST || ui.mode == MODE_COLOR)
  {
    ui.mode = MODE_MENU;
    ui.fullRedraw = true;
    ui.needsRedraw = true;
    return;
  }

  if (ui.current->parent != NULL)
  {
    startTransition(ui.current->parent, -1);
  }
}

// ----------------------------
// Rendering
// ----------------------------
static void drawHeader(const char *title, int xOffset)
{
  lcd_fill_rect(xOffset, 0, SCREEN_W, HEADER_H, COLOR_PANEL);
  lcd_draw_text(xOffset + 12, 8, title, COLOR_TEXT, COLOR_PANEL, 2);
}

static void drawMenuListClipped(Menu *menu, int xOffset, float selectionPos, float scrollPos, int clipY0, int clipY1)
{
  const int leftPad = 16;
  const int rightPad = 16;

  float selY = MENU_LIST_TOP + (selectionPos - scrollPos) * MENU_ROW_H;
  int hiY0 = (int)(selY - 4);
  int hiY1 = (int)(selY + MENU_ROW_H);
  if (hiY1 >= clipY0 && hiY0 <= clipY1)
  {
    int iy0 = hiY0 < clipY0 ? clipY0 : hiY0;
    int iy1 = hiY1 > clipY1 ? clipY1 : hiY1;
    int ih = iy1 - iy0;
    if (ih > 0)
    {
      lcd_fill_rect(xOffset + 8, iy0, SCREEN_W - 16, ih, COLOR_HILITE);
    }
  }

  for (int i = 0; i < menu->itemCount; ++i)
  {
    float y = MENU_LIST_TOP + (i - scrollPos) * MENU_ROW_H;
    if (y < MENU_LIST_TOP - MENU_ROW_H || y > MENU_LIST_BOTTOM)
      continue;
    if ((int)(y + MENU_ROW_H) < clipY0 || (int)y > clipY1)
      continue;

    uint16_t fg = (i == menu->selected) ? COLOR_TEXT : COLOR_MUTED;
    uint16_t bg = (i == menu->selected) ? COLOR_HILITE : COLOR_BG;
    drawLabelClipped(menu->items[i].label, xOffset + leftPad, (int)y + 4, SCREEN_W - leftPad - rightPad, fg, bg);

    if (menu == &menuSpeakers && menu->items[i].type == ITEM_ACTION)
    {
      if (menu->items[i].value == speakerCurrentId)
      {
        int dotY = (int)y + 12;
        if (dotY + 6 >= clipY0 && dotY <= clipY1)
        {
          lcd_fill_rect(xOffset + 10, dotY, 6, 6, COLOR_ACCENT);
        }
      }
    }
  }
}

static void drawMenu(Menu *menu, int xOffset, float selectionPos, float scrollPos)
{
  drawHeader(menu->title, xOffset);
  drawMenuListClipped(menu, xOffset, selectionPos, scrollPos, (int)MENU_LIST_TOP, SCREEN_H);
}

static void drawVolumeScreen(void)
{
  lcd_fill_rect(0, 0, SCREEN_W, SCREEN_H, COLOR_BG);
  drawHeader("Volume", 0);

  lcd_draw_rect(VOLUME_BAR_X, VOLUME_BAR_Y, VOLUME_BAR_W, VOLUME_BAR_H, COLOR_MUTED);
  int fillW = (int)(VOLUME_BAR_W * (volumeDisplay / 100.0f));
  if (fillW < 0)
    fillW = 0;
  if (fillW > VOLUME_BAR_W)
    fillW = VOLUME_BAR_W;
  lcd_fill_rect(VOLUME_BAR_X, VOLUME_BAR_Y, fillW, VOLUME_BAR_H, COLOR_ACCENT);

  char volText[8];
  snprintf(volText, sizeof(volText), "%d%%", volumeValue);
  lcd_draw_text(VOLUME_TEXT_X, VOLUME_TEXT_Y, volText, COLOR_TEXT, COLOR_BG, 2);
}

static void drawVolumeContent(void)
{
  lcd_fill_rect(VOLUME_BAR_X - 2, VOLUME_BAR_Y - 2, VOLUME_BAR_W + 4, VOLUME_BAR_H + 4, COLOR_BG);
  lcd_draw_rect(VOLUME_BAR_X, VOLUME_BAR_Y, VOLUME_BAR_W, VOLUME_BAR_H, COLOR_MUTED);
  int fillW = (int)(VOLUME_BAR_W * (volumeDisplay / 100.0f));
  if (fillW < 0)
    fillW = 0;
  if (fillW > VOLUME_BAR_W)
    fillW = VOLUME_BAR_W;
  lcd_fill_rect(VOLUME_BAR_X, VOLUME_BAR_Y, fillW, VOLUME_BAR_H, COLOR_ACCENT);

  lcd_fill_rect(VOLUME_TEXT_X, VOLUME_TEXT_Y, VOLUME_TEXT_W, VOLUME_TEXT_H, COLOR_BG);
  char volText[8];
  snprintf(volText, sizeof(volText), "%d%%", volumeValue);
  lcd_draw_text(VOLUME_TEXT_X, VOLUME_TEXT_Y, volText, COLOR_TEXT, COLOR_BG, 2);
}

typedef struct
{
  const char *label;
  int pin;
  bool isInput;
} IoItem;

static const IoItem IO_ITEMS[] = {
    {"MOSI", PIN_TFT_MOSI, false},
    {"SCLK", PIN_TFT_SCLK, false},
    {"CS", PIN_TFT_CS, false},
    {"DC", PIN_TFT_DC, false},
    {"RST", PIN_TFT_RST, false},
    {"BL", PIN_TFT_BL, false},
    {"EA", PIN_ENC_A, true},
    {"EB", PIN_ENC_B, true},
    {"ES", PIN_ENC_SW, true},
    {"K0", PIN_BTN_BACK, true},
};

static void drawTestScreen(void)
{
  lcd_fill_rect(0, 0, SCREEN_W, SCREEN_H, COLOR_BG);
  drawHeader("IO Test", 0);

  const int cols = 5;
  const int spacing = 8;
  const int marginX = 10;
  const int top = 52;
  const int squareW = (SCREEN_W - marginX * 2 - spacing * (cols - 1)) / cols;
  const int squareH = 44;

  bool flash = (millis() - testFlashMs) < 140;

  for (int i = 0; i < (int)(sizeof(IO_ITEMS) / sizeof(IO_ITEMS[0])); ++i)
  {
    int row = i / cols;
    int col = i % cols;
    int x = marginX + col * (squareW + spacing);
    int y = top + row * (squareH + spacing);

    uint16_t fill = COLOR_PANEL;
    if (IO_ITEMS[i].isInput)
    {
      bool active = (gpio_get_level(IO_ITEMS[i].pin) == 0);
      if (active)
        fill = COLOR_ACCENT;
    }
    lcd_fill_rect(x, y, squareW, squareH, fill);
    if (flash)
    {
      lcd_draw_rect(x - 1, y - 1, squareW + 2, squareH + 2, COLOR_HILITE);
    }

    int textSize = 2;
    int textW = (int)strlen(IO_ITEMS[i].label) * (FONT_W + 1) * textSize;
    int textH = (FONT_H + 1) * textSize;
    int tx = x + (squareW - textW) / 2;
    int ty = y + (squareH - textH) / 2;
    lcd_draw_text(tx, ty, IO_ITEMS[i].label, COLOR_TEXT, fill, textSize);
  }
}

static void drawColorTestScreen(void)
{
  static const uint16_t colors[] = {
      RGB565(255, 0, 0),
      RGB565(0, 255, 0),
      RGB565(0, 0, 255),
      RGB565(255, 255, 255),
      RGB565(0, 0, 0)};
  static const char *labels[] = {"RED", "GREEN", "BLUE", "WHITE", "BLACK"};
  const int count = (int)(sizeof(colors) / sizeof(colors[0]));
  if (colorTestIndex < 0)
    colorTestIndex = 0;
  if (colorTestIndex >= count)
    colorTestIndex = count - 1;

  uint16_t bg = colors[colorTestIndex];
  uint16_t fg = (colorTestIndex == 3) ? RGB565(0, 0, 0) : COLOR_TEXT;
  lcd_fill_rect_direct(0, 0, SCREEN_W, SCREEN_H, bg);
  lcd_draw_text_direct(10, 10, labels[colorTestIndex], fg, bg, 2);
}

static void drawPerfOverlay(void)
{
  bool overlayOn = (showFps || showCpu);
  if (!overlayOn)
  {
    if (overlayWasOn && overlayW > 0 && overlayH > 0)
    {
      lcd_fill_rect(overlayX - 2, overlayY - 1, overlayW + 4, overlayH + 2, COLOR_BG);
    }
    overlayWasOn = false;
    return;
  }

  char buf[32];
  if (showFps && showCpu)
  {
    snprintf(buf, sizeof(buf), "FPS %d CPU %d", (int)fpsValue, cpuValue);
  }
  else if (showFps)
  {
    snprintf(buf, sizeof(buf), "FPS %d", (int)fpsValue);
  }
  else
  {
    snprintf(buf, sizeof(buf), "CPU %d", cpuValue);
  }

  int scale = 1;
  int textW = (int)strlen(buf) * (FONT_W + 1) * scale;
  int textH = (FONT_H + 1) * scale;
  int x = SCREEN_W - textW - 4;
  int y = 2;
  if (x < 0)
    x = 0;

  int oldX = overlayX;
  int oldY = overlayY;
  int oldW = overlayW;
  int oldH = overlayH;

  overlayX = x;
  overlayY = y;
  overlayW = textW;
  overlayH = textH;
  overlayWasOn = true;

  int clearX = overlayX - 2;
  int clearY = overlayY - 1;
  int clearW = overlayW + 4;
  int clearH = overlayH + 2;
  if (oldW > 0 && oldH > 0)
  {
    int oldX0 = oldX - 2;
    int oldY0 = oldY - 1;
    int oldX1 = oldX0 + oldW + 4;
    int oldY1 = oldY0 + oldH + 2;
    int newX1 = clearX + clearW;
    int newY1 = clearY + clearH;
    if (oldX0 < clearX)
      clearX = oldX0;
    if (oldY0 < clearY)
      clearY = oldY0;
    if (oldX1 > newX1)
      newX1 = oldX1;
    if (oldY1 > newY1)
      newY1 = oldY1;
    clearW = newX1 - clearX;
    clearH = newY1 - clearY;
  }

  lcd_fill_rect(clearX, clearY, clearW, clearH, COLOR_BG);
  lcd_draw_text(overlayX, overlayY, buf, COLOR_ALERT, COLOR_BG, scale);
}

static void lcd_flush_blocking(int x0, int y0, int x1, int y1)
{
  if (!framebuffer)
    return;

  if (x0 < 0)
    x0 = 0;
  if (y0 < 0)
    y0 = 0;
  if (x1 > SCREEN_W)
    x1 = SCREEN_W;
  if (y1 > SCREEN_H)
    y1 = SCREEN_H;
  if (x1 <= x0 || y1 <= y0)
    return;

  int width = x1 - x0;
  if (x0 == 0 && width == SCREEN_W)
  {
    flush_busy = true;
    esp_lcd_panel_draw_bitmap(lcd_panel, x0, y0, x1, y1,
                              framebuffer + y0 * SCREEN_W + x0);
    while (flush_busy)
    {
      vTaskDelay(pdMS_TO_TICKS(1));
    }
    return;
  }

  for (int row = y0; row < y1; ++row)
  {
    memcpy(linebuf, framebuffer + row * SCREEN_W + x0, (size_t)width * sizeof(uint16_t));
    flush_busy = true;
    esp_lcd_panel_draw_bitmap(lcd_panel, x0, row, x1, row + 1, linebuf);
    while (flush_busy)
    {
      vTaskDelay(pdMS_TO_TICKS(1));
    }
  }
}

static void render(void)
{
  static float lastSelPos = 0.0f;
  static float lastScrollPos = 0.0f;

  bool isMenu = (ui.mode == MODE_MENU);
  bool isVolume = (ui.mode == MODE_VOLUME);
  bool isTest = (ui.mode == MODE_TEST);
  bool isColor = (ui.mode == MODE_COLOR);

  if (isColor)
  {
    drawColorTestScreen();
    if (ui.fullRedraw)
      ui.fullRedraw = false;
    if (forceListRefresh)
      forceListRefresh = false;
    overlayDirty = false;
    fpsFrameCount++;
    return;
  }

  bool fullScreen = ui.transition.active || ui.fullRedraw || isTest || isColor;
  bool scrollMoving = fabsf(ui.scrollPos - lastScrollPos) > 0.02f;
  bool selectionMoving = fabsf(ui.selectionPos - lastSelPos) > 0.01f;
  bool usePartial = (isMenu && !fullScreen && !forceListRefresh && !scrollMoving && selectionMoving);

  int dirtyY0 = (int)MENU_LIST_TOP;
  int dirtyY1 = SCREEN_H;
  if (usePartial)
  {
    float lastSelY = MENU_LIST_TOP + (lastSelPos - lastScrollPos) * MENU_ROW_H;
    float curSelY = MENU_LIST_TOP + (ui.selectionPos - ui.scrollPos) * MENU_ROW_H;
    float minY = fminf(lastSelY, curSelY) - 8.0f;
    float maxY = fmaxf(lastSelY, curSelY) + MENU_ROW_H + 8.0f;
    dirtyY0 = (int)minY;
    dirtyY1 = (int)maxY;
    if (dirtyY0 < (int)MENU_LIST_TOP)
      dirtyY0 = (int)MENU_LIST_TOP;
    if (dirtyY1 > SCREEN_H)
      dirtyY1 = SCREEN_H;
  }

  if (fullScreen)
  {
    lcd_fill_rect(0, 0, SCREEN_W, SCREEN_H, COLOR_BG);
  }
  else if (isVolume)
  {
    int volY0 = VOLUME_BAR_Y - 4;
    int volY1 = VOLUME_TEXT_Y + VOLUME_TEXT_H + 2;
    if (volY0 < 0)
      volY0 = 0;
    if (volY1 > SCREEN_H)
      volY1 = SCREEN_H;
    lcd_fill_rect(0, volY0, SCREEN_W, volY1 - volY0, COLOR_BG);
  }
  else if (usePartial)
  {
    lcd_fill_rect(0, dirtyY0, SCREEN_W, dirtyY1 - dirtyY0, COLOR_BG);
  }
  else
  {
    lcd_fill_rect(0, HEADER_H, SCREEN_W, SCREEN_H - HEADER_H, COLOR_BG);
  }

  if (ui.transition.active)
  {
    float t = (millis() - ui.transition.startMs) / 220.0f;
    t = clampf(t, 0.0f, 1.0f);
    float eased = easeOutCubic(t);
    int offset = (int)(eased * SCREEN_W);
    int fromX = (ui.transition.direction == 1) ? -offset : offset;
    int toX = (ui.transition.direction == 1) ? (SCREEN_W - offset) : (-SCREEN_W + offset);

    drawMenu(ui.transition.from, fromX, ui.transition.from->selected, ui.scrollPos);
    drawMenu(ui.transition.to, toX, ui.transition.to->selected, ui.scrollPos);
  }
  else if (isVolume)
  {
    if (fullScreen)
    {
      drawVolumeScreen();
    }
    else
    {
      drawVolumeContent();
    }
  }
  else if (isTest)
  {
    drawTestScreen();
  }
  else if (isColor)
  {
    drawColorTestScreen();
  }
  else
  {
    if (fullScreen)
    {
      drawMenu(ui.current, 0, ui.selectionPos, ui.scrollPos);
    }
    else if (usePartial)
    {
      drawMenuListClipped(ui.current, 0, ui.selectionPos, ui.scrollPos, dirtyY0, dirtyY1);
    }
    else
    {
      drawMenuListClipped(ui.current, 0, ui.selectionPos, ui.scrollPos, (int)MENU_LIST_TOP, SCREEN_H);
    }
  }

  drawPerfOverlay();

  int flushX0 = 0;
  int flushY0 = 0;
  int flushX1 = SCREEN_W;
  int flushY1 = SCREEN_H;
  if (!fullScreen)
  {
    if (isVolume)
    {
      flushY0 = VOLUME_BAR_Y - 4;
      flushY1 = VOLUME_TEXT_Y + VOLUME_TEXT_H + 2;
      if (flushY0 < 0)
        flushY0 = 0;
      if (flushY1 > SCREEN_H)
        flushY1 = SCREEN_H;
    }
    else
    {
      flushY0 = HEADER_H;
    }
  }
  if (usePartial)
  {
    flushY0 = dirtyY0;
    flushY1 = dirtyY1;
  }

#if FORCE_FULL_FLUSH
  flushX0 = 0;
  flushY0 = 0;
  flushX1 = SCREEN_W;
  flushY1 = SCREEN_H;
#endif

  if (overlayDirty && overlayW > 0 && overlayH > 0)
  {
    int ox = overlayX - 2;
    int oy = overlayY - 1;
    int ow = overlayW + 4;
    int oh = overlayH + 2;
    if (ox < 0)
      ox = 0;
    if (oy < 0)
      oy = 0;
    int ox1 = ox + ow;
    int oy1 = oy + oh;
    if (ox1 > SCREEN_W)
      ox1 = SCREEN_W;
    if (oy1 > SCREEN_H)
      oy1 = SCREEN_H;
    if (oy < flushY0)
      flushY0 = oy;
    if (oy1 > flushY1)
      flushY1 = oy1;
    if (ox < flushX0)
      flushX0 = ox;
    if (ox1 > flushX1)
      flushX1 = ox1;
  }

  if (framebuffer)
  {
    lcd_flush_blocking(flushX0, flushY0, flushX1, flushY1);
  }

  if (fullScreen)
    ui.fullRedraw = false;
  if (forceListRefresh)
    forceListRefresh = false;
  overlayDirty = false;

  if (isMenu)
  {
    lastSelPos = ui.selectionPos;
    lastScrollPos = ui.scrollPos;
  }

  fpsFrameCount++;
}

// ----------------------------
// Serial parsing
// ----------------------------
static void handleLine(char *line)
{
  triggerTestFlash();
  if (strncmp(line, "VOL ", 4) == 0)
  {
    int v = atoi(line + 4);
    volumeValue = clampi(v, 0, 100);
    updateVolumeLabel();
    updateMainMenuItems();
    ui.fullRedraw = true;
    ui.needsRedraw = true;
  }
  else if (strncmp(line, "MUTE ", 5) == 0)
  {
    int m = atoi(line + 5);
    muteState = (m != 0);
    updateMuteLabel();
    updateMainMenuItems();
    ui.fullRedraw = true;
    ui.needsRedraw = true;
  }
  else if (strcmp(line, "SPK BEGIN") == 0)
  {
    speakersLoading = true;
    speakerCount = 0;
    updateSpeakerMenuItems();
    ui.needsRedraw = true;
  }
  else if (strncmp(line, "SPK ITEM ", 9) == 0)
  {
    if (speakerCount >= MAX_SPEAKERS)
      return;
    const char *p = line + 9;
    int id = atoi(p);
    const char *name = strchr(p, ' ');
    if (name)
    {
      name++;
    }
    else
    {
      name = "";
    }
    speakerIds[speakerCount] = id;
    copyTrunc(speakerNames[speakerCount], name, SPEAKER_NAME_LEN);
    speakerCount++;
    updateSpeakerMenuItems();
    ui.needsRedraw = true;
  }
  else if (strcmp(line, "SPK END") == 0)
  {
    speakersLoading = false;
    updateSpeakerMenuItems();
    ui.needsRedraw = true;
  }
  else if (strncmp(line, "SPK CUR ", 8) == 0)
  {
    speakerCurrentId = atoi(line + 8);
    ui.needsRedraw = true;
  }
}

static void readSerial(void)
{
  uint8_t data[64];
  int len = uart_read_bytes(UART_NUM_0, data, sizeof(data), 0);
  for (int i = 0; i < len; ++i)
  {
    char c = (char)data[i];
    if (c == '\n')
    {
      serialBuf[serialLen] = '\0';
      if (serialLen > 0)
      {
        handleLine(serialBuf);
      }
      serialLen = 0;
    }
    else if (c != '\r')
    {
      if (serialLen < sizeof(serialBuf) - 1)
      {
        serialBuf[serialLen++] = c;
      }
    }
  }
}

// ----------------------------
// Hardware init
// ----------------------------
static void init_display(void)
{
  size_t fb_size = SCREEN_W * SCREEN_H * sizeof(uint16_t);

#if USE_FRAMEBUFFER
  framebuffer = (uint16_t *)heap_caps_malloc(fb_size, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
#else
  framebuffer = NULL;
#endif

  gpio_config_t bk_conf = {
      .pin_bit_mask = (1ULL << PIN_TFT_BL),
      .mode = GPIO_MODE_OUTPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE};
  gpio_config(&bk_conf);
  gpio_set_level(PIN_TFT_BL, 1);

  spi_bus_config_t buscfg = {
      .sclk_io_num = PIN_TFT_SCLK,
      .mosi_io_num = PIN_TFT_MOSI,
      .miso_io_num = -1,
      .quadwp_io_num = -1,
      .quadhd_io_num = -1,
      .max_transfer_sz = SCREEN_W * SCREEN_H * 2};
  spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);

  esp_lcd_panel_io_spi_config_t io_config = {
      .dc_gpio_num = PIN_TFT_DC,
      .cs_gpio_num = PIN_TFT_CS,
      .pclk_hz = TFT_SPI_HZ,
      .lcd_cmd_bits = LCD_CMD_BITS,
      .lcd_param_bits = LCD_PARAM_BITS,
      .spi_mode = 0,
      .trans_queue_depth = 1,
      .on_color_trans_done = lcd_on_color_trans_done,
      .user_ctx = NULL};
  esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &io_config, &lcd_io);

  esp_lcd_panel_dev_config_t panel_config = {
      .reset_gpio_num = PIN_TFT_RST,
      .color_space = ESP_LCD_COLOR_SPACE_RGB,
      .bits_per_pixel = 16};
  esp_lcd_new_panel_st7789(lcd_io, &panel_config, &lcd_panel);
  esp_lcd_panel_reset(lcd_panel);
  vTaskDelay(pdMS_TO_TICKS(20));
  esp_lcd_panel_init(lcd_panel);
  esp_lcd_panel_disp_on_off(lcd_panel, true);
  esp_lcd_panel_invert_color(lcd_panel, true);
  esp_lcd_panel_swap_xy(lcd_panel, true);
  esp_lcd_panel_mirror(lcd_panel, true, false);
}

static void init_inputs(void)
{
  gpio_config_t in_conf = {
      .pin_bit_mask = (1ULL << PIN_ENC_A) | (1ULL << PIN_ENC_B) | (1ULL << PIN_ENC_SW) | (1ULL << PIN_BTN_BACK),
      .mode = GPIO_MODE_INPUT,
      .pull_up_en = GPIO_PULLUP_ENABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE};
  gpio_config(&in_conf);

  encState = ((uint8_t)gpio_get_level(PIN_ENC_A) << 1) | (uint8_t)gpio_get_level(PIN_ENC_B);
}

static void init_uart(void)
{
  const uart_config_t uart_config = {
      .baud_rate = 115200,
      .data_bits = UART_DATA_8_BITS,
      .parity = UART_PARITY_DISABLE,
      .stop_bits = UART_STOP_BITS_1,
      .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
      .source_clk = UART_SCLK_DEFAULT};
  uart_driver_install(UART_NUM_0, 2048, 0, 0, NULL, 0);
  uart_param_config(UART_NUM_0, &uart_config);
  uart_set_pin(UART_NUM_0, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
}

// ----------------------------
// Main
// ----------------------------
void app_main(void)
{
  init_display();
  init_inputs();
  init_uart();

  updateVolumeLabel();
  updateMuteLabel();

  menuMain = (Menu){"Main", mainItems, 4, NULL, 0};
  menuSpeakers = (Menu){"Output", speakerItems, 0, &menuMain, 0};
  menuTest = (Menu){"Test", testItems, 4, &menuMain, 0};

  mainItems[0] = (MenuItem){volumeLabel, ITEM_ACTION, actionOpenVolume, NULL, 0};
  mainItems[1] = (MenuItem){"Output Device", ITEM_SUBMENU, NULL, &menuSpeakers, 0};
  mainItems[2] = (MenuItem){"Test Tools", ITEM_SUBMENU, NULL, &menuTest, 0};
  mainItems[3] = (MenuItem){muteLabel, ITEM_ACTION, actionToggleMute, NULL, 0};

  testItems[0] = (MenuItem){"IO Test", ITEM_ACTION, actionOpenTest, NULL, 0};
  testItems[1] = (MenuItem){"Color Test", ITEM_ACTION, actionOpenColorTest, NULL, 0};
  testItems[2] = (MenuItem){fpsLabel, ITEM_ACTION, actionToggleFps, NULL, 0};
  testItems[3] = (MenuItem){cpuLabel, ITEM_ACTION, actionToggleCpu, NULL, 0};

  updateMainMenuItems();
  updateTestMenuItems();
  updateSpeakerMenuItems();

  ui.mode = MODE_MENU;
  ui.current = &menuMain;
  ui.selectionPos = (float)ui.current->selected;
  ui.selectionAnimating = false;
  ui.scrollPos = 0.0f;
  ui.transition.active = false;
  ui.needsRedraw = true;
  ui.fullRedraw = true;

  sendVolumeGet();
  sendSpeakerListRequest();

  xTaskCreatePinnedToCore(encoder_task, "encoder", 2048, NULL, 4, NULL, 0);

  while (1)
  {
    uint64_t loopStartUs = esp_timer_get_time();
    if (lastLoopUs == 0)
      lastLoopUs = loopStartUs;
    totalUsAccum += (loopStartUs - lastLoopUs);
    lastLoopUs = loopStartUs;

    updateButton(&btnSelect);
    updateButton(&btnBack);
    readSerial();

    int steps = readEncoderSteps();
    if (steps != 0)
    {
      triggerTestFlash();
      if (ui.mode == MODE_VOLUME)
      {
        volumeValue = clampi(volumeValue + steps * 2, 0, 100);
        updateVolumeLabel();
        updateMainMenuItems();
        sendVolumeSet();
        ui.needsRedraw = true;
      }
      else if (ui.mode == MODE_MENU)
      {
        setMenuSelection(ui.current, ui.current->selected + steps);
      }
      else if (ui.mode == MODE_COLOR)
      {
        colorTestIndex += steps;
        ui.needsRedraw = true;
      }
    }

    if (consumePressed(&btnSelect))
    {
      triggerTestFlash();
      handleSelect();
    }
    if (consumePressed(&btnBack))
    {
      triggerTestFlash();
      handleBack();
    }

    if (ui.selectionAnimating)
    {
      float t = (millis() - ui.selectionStartMs) / 160.0f;
      t = clampf(t, 0.0f, 1.0f);
      float eased = easeOutCubic(t);
      ui.selectionPos = ui.selectionFrom + (ui.selectionTo - ui.selectionFrom) * eased;
      if (t >= 1.0f)
      {
        ui.selectionPos = ui.selectionTo;
        ui.selectionAnimating = false;
      }
    }
    else
    {
      ui.selectionPos = (float)ui.current->selected;
    }

    if (ui.transition.active)
    {
      float t = (millis() - ui.transition.startMs) / 220.0f;
      if (t >= 1.0f)
      {
        ui.transition.active = false;
        ui.current = ui.transition.to;
        ui.selectionPos = (float)ui.current->selected;
        ui.selectionAnimating = false;
        ui.fullRedraw = true;
        ui.needsRedraw = true;
      }
    }

    if (ui.mode == MODE_VOLUME)
    {
      volumeDisplay += (volumeValue - volumeDisplay) * 0.2f;
    }
    else
    {
      volumeDisplay = (float)volumeValue;
    }

    const int visibleCount = 6;
    float maxScroll = (float)((ui.current->itemCount > visibleCount) ? (ui.current->itemCount - visibleCount) : 0);
    float scrollTarget = clampf(ui.selectionPos - (visibleCount - 1) * 0.5f, 0.0f, maxScroll);
    ui.scrollPos += (scrollTarget - ui.scrollPos) * 0.25f;

    bool animating = ui.selectionAnimating ||
                     ui.transition.active ||
                     (ui.mode == MODE_VOLUME && fabsf(volumeDisplay - (float)volumeValue) > 0.2f) ||
                     (ui.mode == MODE_TEST && (millis() - testFlashMs) < 160) ||
                     fabsf(scrollTarget - ui.scrollPos) > 0.01f;

    uint32_t nowMs = millis();
    if (nowMs - lastForceRefreshMs >= 500)
    {
      forceListRefresh = true;
      ui.needsRedraw = true;
      lastForceRefreshMs = nowMs;
    }
    if (nowMs - lastFrameMs >= FRAME_MS && (ui.needsRedraw || animating))
    {
      render();
      ui.needsRedraw = false;
      lastFrameMs = nowMs;
    }

    uint64_t loopEndUs = esp_timer_get_time();
    busyUsAccum += (loopEndUs - loopStartUs);

    if (perfLastMs == 0)
      perfLastMs = nowMs;
    if (nowMs - perfLastMs >= 1000)
    {
      uint32_t dtMs = nowMs - perfLastMs;
      if (dtMs > 0)
      {
        fpsValue = (float)fpsFrameCount * 1000.0f / (float)dtMs;
      }
      if (totalUsAccum > 0)
      {
        cpuValue = (int)((busyUsAccum * 100) / totalUsAccum);
        if (cpuValue < 0)
          cpuValue = 0;
        if (cpuValue > 100)
          cpuValue = 100;
      }
      fpsFrameCount = 0;
      busyUsAccum = 0;
      totalUsAccum = 0;
      perfLastMs = nowMs;
      if (showFps || showCpu)
      {
        overlayDirty = true;
        ui.needsRedraw = true;
      }
    }

    vTaskDelay(pdMS_TO_TICKS(2));
  }
}
