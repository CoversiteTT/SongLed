#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_chip_info.h"
#include "esp_heap_caps.h"
#include "esp_flash.h"

#include "hal_astra_esp32.h"
#include "astra/ui/launcher.h"
#include "astra/ui/item/menu/menu.h"
#include "astra/ui/item/widget/widget.h"
#include "astra/config/config.h"
#include "ble_service.h"

using namespace astra;

namespace {
constexpr bool SIMPLE_DISPLAY_ONLY = false;
constexpr int UART_BAUD = 115200;
constexpr const char *NVS_NAMESPACE = "songled";

// Version Information
constexpr const char *FIRMWARE_VERSION = "v0.0.2";
constexpr const char *BUILD_DATE = __DATE__;
constexpr const char *BUILD_TIME = __TIME__;
constexpr const char *CHANGELOG[] = {
  "v0.0.2:",
  "  USB/BLE/Auto (USB first)",
  "  Now Playing window",
  "  Cover + lyric + progress",
  "  Output + Mic menu",
  "  Popup/UI fixes",
  "v0.0.1:",
  "  Initial release",
  nullptr
};

struct AdjustStepState {
  uint32_t lastStepMs = 0;
  uint16_t fastMs = 60;
  uint16_t midMs = 140;
  float timeScale = 1.0f; // 加速度敏感度倍率(越小越敏感)
  int fastStep = 5;
  int midStep = 2;
  int slowStep = 1;
  int baseStep = 1; // 基础速度倍率
};

AdjustStepState g_adjustStep;

int getAdjustStep(uint32_t nowMs) {
  if (g_adjustStep.lastStepMs == 0) {
    g_adjustStep.lastStepMs = nowMs;
    return g_adjustStep.slowStep * g_adjustStep.baseStep;
  }
  uint32_t dt = nowMs - g_adjustStep.lastStepMs;
  g_adjustStep.lastStepMs = nowMs;
  uint32_t fastMs = static_cast<uint32_t>(g_adjustStep.fastMs * g_adjustStep.timeScale);
  uint32_t midMs = static_cast<uint32_t>(g_adjustStep.midMs * g_adjustStep.timeScale);
  if (fastMs < 10) fastMs = 10;
  if (midMs < fastMs + 10) midMs = fastMs + 10;
  if (dt <= fastMs) return g_adjustStep.fastStep * g_adjustStep.baseStep;
  if (dt <= midMs) return g_adjustStep.midStep * g_adjustStep.baseStep;
  return g_adjustStep.slowStep * g_adjustStep.baseStep;
}

void setAdjustBaseStep(int baseStep) {
  if (baseStep < 1) baseStep = 1;
  if (baseStep > 5) baseStep = 5;
  g_adjustStep.baseStep = baseStep;
}

void setAdjustTimeScale(float scale) {
  if (scale < 0.25f) scale = 0.25f;
  if (scale > 2.0f) scale = 2.0f;
  g_adjustStep.timeScale = scale;
}

// Communication mode
enum CommMode {
  COMM_USB,   // USB Serial
  COMM_BLE,   // Bluetooth LE
  COMM_AUTO   // Auto-detect
};
CommMode commMode = COMM_AUTO;
bool bleEnabled = false;

HALAstraESP32 hal;
Launcher launcher;

List *menuMain = nullptr;
List *menuVolume = nullptr;
List *menuOutput = nullptr;
List *menuInput = nullptr;
List *menuTest = nullptr;
List *menuMute = nullptr;
List *menuSettings = nullptr;
List *menuAbout = nullptr;
List *menuBluetooth = nullptr;

List *outputRefreshItem = nullptr;
List *inputRefreshItem = nullptr;
List *testIoItem = nullptr;
List *testFpsItem = nullptr;
List *testCpuItem = nullptr;
List *testSpiItem = nullptr;
List *testDmaItem = nullptr;
List *testFbItem = nullptr;
List *testPsramItem = nullptr;
List *testDoubleBufferItem = nullptr;
List *test4bitItem = nullptr;
List *testUpItem = nullptr;
List *testDownItem = nullptr;
List *testLogItem = nullptr;
List *uiSpeedItem = nullptr;
List *selSpeedItem = nullptr;
List *wrapPauseItem = nullptr;
List *fontColorItem = nullptr;
List *scrollTimeItem = nullptr;
List *lyricScrollItem = nullptr;
List *npAutoCloseItem = nullptr;
List *cfgMsgAutoCloseItem = nullptr;
List *bleEnableItem = nullptr;
List *bleAdvItem = nullptr;
List *bleDisconnectItem = nullptr;
List *bleStatusItem = nullptr;
List *bleRouteItem = nullptr;

Slider *volumeSlider = nullptr;
CheckBox *muteCheck = nullptr;
CheckBox *fpsCheck = nullptr;
CheckBox *cpuCheck = nullptr;
CheckBox *upCheck = nullptr;
CheckBox *downCheck = nullptr;
CheckBox *logCheck = nullptr;
Slider *spiSlider = nullptr;
CheckBox *dmaCheck = nullptr;
CheckBox *fbCheck = nullptr;
CheckBox *psramCheck = nullptr;
CheckBox *doubleBufferCheck = nullptr;
CheckBox *bleEnableCheck = nullptr;
Slider *uiSpeedSlider = nullptr;
Slider *selSpeedSlider = nullptr;
Slider *wrapPauseSlider = nullptr;
Slider *fontColorSlider = nullptr;
Slider *scrollTimeSlider = nullptr;
Slider *lyricScrollSlider = nullptr;
Slider *npAutoCloseSlider = nullptr;
Slider *cfgMsgAutoCloseSlider = nullptr;

uint8_t volumeValue = 50;
bool muteState = false;
bool showFps = false;
bool showCpu = false;
bool showUp = false;
bool showDown = false;
bool mcuLogEnabled = false;
uint8_t uiSpeedValue = 13;
uint8_t uiSpeedPending = 13;
bool uiSpeedDirty = false;
uint8_t selSpeedValue = 25;
uint8_t selSpeedPending = 25;
bool selSpeedDirty = false;
uint8_t wrapPauseValue = 15;
uint8_t wrapPausePending = 15;
bool wrapPauseDirty = false;
uint8_t fontColorValue = 17;
uint8_t fontColorPending = 17;
bool fontColorDirty = false;
uint8_t scrollTimeValue = 30;
uint8_t scrollTimePending = 30;
bool scrollTimeDirty = false;
uint8_t lyricScrollCpsValue = 8;
uint8_t lyricScrollCpsPending = 8;
bool lyricScrollCpsDirty = false;
uint8_t npAutoCloseSecValue = 8;      // 0..60: 0=0ms, 60=NEVER, others=seconds
uint8_t npAutoCloseSecPending = 8;
bool npAutoCloseSecDirty = false;
uint16_t cfgMsgAutoCloseMs = 5000;
uint8_t cfgMsgAutoCloseValue = 26;
uint8_t cfgMsgAutoClosePending = 26;
bool cfgMsgAutoCloseDirty = false;

bool speakersLoading = false;
int speakerCurrentId = -1;
bool speakersRequested = false;
bool microphonesLoading = false;
int microphoneCurrentId = -1;
bool microphonesRequested = false;

uint8_t spiMHz = 80;
uint8_t spiPending = 80;
bool spiDirty = false;
bool dmaEnabled = true;
bool fbEnabled = true;
bool psramEnabled = false;  // PSRAM too slow for framebuffer
bool doubleBufferEnabled = true;
bool liveHeartbeat = true;
bool handshakeOk = false;
bool syncedAfterHandshake = false;
uint32_t lastHelloMs = 0;
uint32_t lastRxMs = 0; // 最后一次收到消息的时间
uint32_t lastUsbRxMs = 0;
constexpr uint32_t HELLO_INTERVAL_MS = 3000;
constexpr uint32_t HANDSHAKE_TIMEOUT_MS = 30000;
constexpr uint32_t LINK_READY_STALE_MS = 3500;

uint32_t txBytes = 0;
uint32_t rxBytes = 0;
int upBps = 0;
int downBps = 0;

// Lyrics data
struct LyricLine {
  char text[128];
  bool active;
};
LyricLine currentLyric = {"", false};
LyricLine nextLyric = {"", false};
int lyricScrollOffset = 0;
uint32_t lyricLastUpdateMs = 0;

constexpr int NP_COVER_W = 40;
constexpr int NP_COVER_H = 40;
constexpr int NP_COVER_PIXELS = NP_COVER_W * NP_COVER_H;

struct NowPlaying {
  bool active;
  char title[64];
  char artist[64];
  int32_t posMs;
  int32_t durMs;
  bool coverValid;
  bool coverReceiving;
  int coverIndex;
  int coverTotal;
  uint32_t lastUpdateMs;
  uint16_t coverFront[NP_COVER_PIXELS];
  uint16_t coverBack[NP_COVER_PIXELS];
};

NowPlaying nowPlaying = {};

struct SpeakerEntry {
  int id;
  std::string name;
};

std::vector<SpeakerEntry> speakers;
std::vector<std::pair<Menu *, int>> speakerMenuMap;
std::vector<SpeakerEntry> microphones;
std::vector<std::pair<Menu *, int>> microphoneMenuMap;

enum AppMode {
  MODE_NORMAL = 0,
  MODE_VOLUME_ADJUST,
  MODE_SPI_ADJUST,
  MODE_UI_ADJUST,
  MODE_SEL_ADJUST,
  MODE_WRAP_ADJUST,
  MODE_COLOR_ADJUST,
  MODE_SCROLL_ADJUST,
  MODE_LYRIC_SCROLL_ADJUST,
  MODE_NP_CLOSE_ADJUST,
  MODE_CFG_CLOSE_ADJUST,
  MODE_TEST_PATTERN,
  MODE_ABOUT_INFO,
};

AppMode appMode = MODE_NORMAL;
bool adjustActive = false;

enum AdjustTarget {
  ADJ_NONE = 0,
  ADJ_VOLUME,
  ADJ_SPI,
  ADJ_UI_SPEED,
  ADJ_SEL_SPEED,
  ADJ_WRAP_PAUSE,
  ADJ_FONT_COLOR,
  ADJ_SCROLL_TIME,
  ADJ_LYRIC_SCROLL_CPS,
  ADJ_NP_CLOSE_SEC,
  ADJ_CFG_CLOSE,
};

AdjustTarget adjustTarget = ADJ_NONE;

char serialBuf[1024];
size_t serialLen = 0;

float mapUiSpeedScale(uint8_t value);
uint8_t mapSelSpeedFromMs(float ms);
float mapSelSpeedMs(uint8_t value);
uint8_t mapWrapPauseFromMs(float ms);
float mapWrapPauseMs(uint8_t value);
uint8_t mapScrollFromMs(float ms);
float mapScrollMs(uint8_t value);
void boostUiSpeed(float sliderValue);
void applySelectorSpeed();
void applyWrapPause();
void applyScrollDuration();
void applyLyricScrollSpeed();
void applyNowPlayingAutoClose();
uint8_t mapNpCloseFromMs(uint16_t ms);
uint16_t mapNpCloseMs(uint8_t value);
uint8_t mapCfgCloseFromMs(uint16_t ms);
uint16_t mapCfgCloseMs(uint8_t value);
uint16_t getPopupDurationMs(uint16_t fallbackMs);
void popInfoGlobal(const std::string &text, uint16_t fallbackMs = 800);
float mapHueDeg(uint8_t value);
uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b);
uint16_t hueToRgb565(uint8_t value);
void applyFontColor();
void renderTestPattern();
void renderAboutInfo();
void renderPopupBackground();
void renderNowPlayingOverlay();
void rebuildInputMenu();
void clearNowPlayingState(bool clearLyrics);
bool loadSettings();
void saveSettings();
void resetSettings();
bool checkSafeReset();
void bleDebugForward(const char *line);
bool isUsbLinkReady();
bool isBleBasicLinkReady();
bool isBleClientLinkReady();
bool canUseControlFeatures();
const char *commModeLabel(CommMode mode);
void updateControlMenuAvailability();
void updateBluetoothMenuItems();

void init_uart() {
  uart_config_t uart_config = {};
  uart_config.baud_rate = UART_BAUD;
  uart_config.data_bits = UART_DATA_8_BITS;
  uart_config.parity = UART_PARITY_DISABLE;
  uart_config.stop_bits = UART_STOP_BITS_1;
  uart_config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
  uart_config.source_clk = UART_SCLK_DEFAULT;
  // Larger RX buffer to avoid overflow when streaming cover data.
  uart_driver_install(UART_NUM_0, 8192, 0, 0, nullptr, 0);
  uart_param_config(UART_NUM_0, &uart_config);
  uart_set_pin(UART_NUM_0, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
}

bool isUsbLinkReady() {
  if (!handshakeOk) return false;
  if (lastUsbRxMs == 0) return false;
  uint32_t nowMs = static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
  return (nowMs - lastUsbRxMs) <= LINK_READY_STALE_MS;
}

bool isBleBasicLinkReady() { return bleEnabled && ble_is_connected(); }

bool isBleClientLinkReady() { return bleEnabled && ble_is_notify_ready(); }

const char *commModeLabel(CommMode mode) {
  switch (mode) {
    case COMM_USB: return "Wired";
    case COMM_BLE: return "BLE Client";
    case COMM_AUTO:
    default: return "Auto";
  }
}

static CommMode pickCommandMode() {
  bool usbReady = isUsbLinkReady();
  bool bleClientReady = isBleClientLinkReady();
  if (commMode == COMM_USB) {
    if (usbReady) return COMM_USB;
    if (bleClientReady) return COMM_BLE;  // fallback
    return COMM_AUTO;
  }
  if (commMode == COMM_BLE) {
    if (bleClientReady) return COMM_BLE;
    if (usbReady) return COMM_USB;  // fallback
    return COMM_AUTO;
  }
  // AUTO mode: always prefer wired link when both are ready.
  if (usbReady && bleClientReady) return COMM_USB;
  if (usbReady) return COMM_USB;
  if (bleClientReady) return COMM_BLE;
  return COMM_AUTO;
}

bool canUseControlFeatures() {
  return pickCommandMode() != COMM_AUTO;
}

void updateBluetoothMenuItems() {
  if (!bleStatusItem || !bleRouteItem) return;
  bool usbReady = isUsbLinkReady();
  bool bleBasicReady = isBleBasicLinkReady();
  bool bleClientReady = isBleClientLinkReady();

  std::string status;
  if (usbReady && bleClientReady) {
    status = "Status: Wired + BLE Client";
  } else if (usbReady && bleBasicReady) {
    status = "Status: Wired + BLE Basic";
  } else if (usbReady) {
    status = "Status: Wired";
  } else if (bleClientReady) {
    status = "Status: BLE Client";
  } else if (bleBasicReady) {
    status = "Status: BLE Basic";
  } else {
    status = "Status: Offline";
  }
  CommMode activeMode = pickCommandMode();
  if (activeMode == COMM_AUTO) {
    status += " -> N/A";
  } else {
    status += std::string(" -> ") + commModeLabel(activeMode);
  }
  bleStatusItem->title = status;
  bleRouteItem->title = std::string("Prefer: ") + commModeLabel(commMode);
  updateControlMenuAvailability();
}

void updateControlMenuAvailability() {
  if (!menuVolume || !menuOutput || !menuInput || !menuMute) return;
  bool enabled = canUseControlFeatures();
  const char *suffix = enabled ? "" : " [LOCK]";
  menuVolume->title = std::string("Volume") + suffix;
  menuOutput->title = std::string("Output Device") + suffix;
  menuInput->title = std::string("Microphone") + suffix;
  menuMute->title = std::string("Mute") + suffix;
}

void sendLine(const char *line) {
  size_t len = strlen(line);
  CommMode route = pickCommandMode();
  if (route == COMM_BLE && ble_send_line(line)) {
    txBytes += static_cast<uint32_t>(len + 1);
    return;
  }
  // AUTO mode fallback: keep sending HELLO/commands over UART for discovery.
  uart_write_bytes(UART_NUM_0, line, static_cast<int>(len));
  uart_write_bytes(UART_NUM_0, "\n", 1);
  txBytes += static_cast<uint32_t>(len + 1);
}

void sendVolumeGet() { sendLine("VOL GET"); }

void sendVolumeSet() {
  char line[24];
  snprintf(line, sizeof(line), "VOL SET %d", volumeValue);
  sendLine(line);
}

void sendMuteToggle() { sendLine("MUTE"); }

void sendSpeakerListRequest() { sendLine("SPK LIST"); }

void sendSpeakerSet(int id) {
  char line[24];
  snprintf(line, sizeof(line), "SPK SET %d", id);
  sendLine(line);
}

void sendMicListRequest() { sendLine("MIC LIST"); }

void sendMicSet(int id) {
  char line[24];
  snprintf(line, sizeof(line), "MIC SET %d", id);
  sendLine(line);
}

void sendHello() { sendLine("HELLO"); }

void rebuildOutputMenu() {
  // 临时调试：打印speakers数组状态
  char dbg[64];
  snprintf(dbg, sizeof(dbg), "[REBUILD] speakers.size=%d, loading=%d", (int)speakers.size(), speakersLoading ? 1 : 0);
  sendLine(dbg);
  
  for (auto *item : menuOutput->childMenu) {
    if (item == outputRefreshItem) continue;
    delete item;
  }
  menuOutput->childMenu.clear();
  menuOutput->childWidget.clear();
  speakerMenuMap.clear();

  menuOutput->addItem(outputRefreshItem);

  if (speakersLoading) {
    auto *loading = new List("Loading...");
    menuOutput->addItem(loading);
    sendLine("[REBUILD] Showing loading");
  } else if (speakers.empty()) {
    auto *none = new List("No devices");
    menuOutput->addItem(none);
    sendLine("[REBUILD] Showing no devices");
  } else {
    snprintf(dbg, sizeof(dbg), "[REBUILD] Adding %d devices", (int)speakers.size());
    sendLine(dbg);
    for (const auto &spk : speakers) {
      std::string title = spk.name;
      if (spk.id == speakerCurrentId) title = "* " + title;
      auto *item = new List(title);
      menuOutput->addItem(item);
      speakerMenuMap.push_back({item, spk.id});
    }
  }

  menuOutput->forePosInit();
  if (launcher.getCamera()) {
    menuOutput->childPosInit(launcher.getCamera()->getPosition());
  }
  if (launcher.getCurrentMenu() == menuOutput && launcher.getSelector()) {
    launcher.getSelector()->inject(menuOutput);
  }
}

void updateVolumeWidgets() {
  if (volumeSlider) {
    volumeSlider->value = volumeValue;
    volumeSlider->init();
  }
}

void updateMuteWidgets() {
  if (muteCheck) {
    muteCheck->value = muteState;
    muteCheck->init();
  }
}

void updatePerfWidgets() {
  if (fpsCheck) {
    fpsCheck->value = showFps;
    fpsCheck->init();
  }
  if (cpuCheck) {
    cpuCheck->value = showCpu;
    cpuCheck->init();
  }
  if (upCheck) {
    upCheck->value = showUp;
    upCheck->init();
  }
  if (downCheck) {
    downCheck->value = showDown;
    downCheck->init();
  }
  if (logCheck) {
    logCheck->value = mcuLogEnabled;
    logCheck->init();
  }
}

void bleDebugForward(const char *line) {
  if (!mcuLogEnabled || !line || line[0] == '\0') return;
  char msg[220];
  snprintf(msg, sizeof(msg), "DEBUG: %s", line);
  sendLine(msg);
}

void updateDisplayWidgets() {
  if (spiSlider) {
    spiSlider->value = spiPending;
    spiSlider->init();
  }
  if (dmaCheck) {
    dmaCheck->value = dmaEnabled;
    dmaCheck->init();
  }
  if (fbCheck) {
    fbCheck->value = fbEnabled;
    fbCheck->init();
  }
}

void updateSettingsWidgets() {
  if (uiSpeedSlider) {
    uiSpeedSlider->value = uiSpeedPending;
    uiSpeedSlider->init();
  }
  if (selSpeedSlider) {
    selSpeedSlider->value = selSpeedPending;
    selSpeedSlider->init();
  }
  if (wrapPauseSlider) {
    wrapPauseSlider->value = wrapPausePending;
    wrapPauseSlider->init();
  }
  if (fontColorSlider) {
    fontColorSlider->value = fontColorPending;
    fontColorSlider->init();
  }
  if (scrollTimeSlider) {
    scrollTimeSlider->value = scrollTimePending;
    scrollTimeSlider->init();
  }
  if (lyricScrollSlider) {
    lyricScrollSlider->value = lyricScrollCpsPending;
    lyricScrollSlider->init();
  }
  if (npAutoCloseSlider) {
    npAutoCloseSlider->value = npAutoCloseSecPending;
    npAutoCloseSlider->init();
  }
  if (cfgMsgAutoCloseSlider) {
    cfgMsgAutoCloseSlider->value = cfgMsgAutoClosePending;
    cfgMsgAutoCloseSlider->init();
  }
  if (bleEnableCheck) {
    bleEnableCheck->value = bleEnabled;
    bleEnableCheck->init();
  }
  updateBluetoothMenuItems();
}

void applyDisplayConfig() {
  HALAstraESP32::DisplayConfig cfg = hal.getDisplayConfig();
  cfg.spi_hz = spiMHz * 1000000;
  cfg.use_dma = dmaEnabled;
  cfg.use_framebuffer = fbEnabled;
  cfg.use_psram = psramEnabled;
  cfg.use_double_buffer = doubleBufferEnabled;
  
  // Debug output to verify settings are applied
  ESP_LOGI("CFG", "SPI=%dMHz DMA=%d FB=%d PSRAM=%d 2xBuf=%d",
           spiMHz, dmaEnabled, fbEnabled, psramEnabled, doubleBufferEnabled);
  
  hal.applyDisplayConfig(cfg);
  updateDisplayWidgets();
}

extern "C" void handleLine(char *line) {
  lastRxMs = static_cast<uint32_t>(esp_timer_get_time() / 1000ULL); // 更新接收时间
  
  // Debug: log all received lines
  ESP_LOGI("SERIAL", "RX: %s", line);
  
  if (strncmp(line, "HELLO", 5) == 0) {
    sendLine("HELLO OK");
    handshakeOk = true;
    if (!syncedAfterHandshake) {
      syncedAfterHandshake = true;
      sendVolumeGet();
    }
    return;
  }
  if (strncmp(line, "HELLO OK", 8) == 0 || strncmp(line, "HELLO ACK", 9) == 0) {
    handshakeOk = true;
    if (!syncedAfterHandshake) {
      syncedAfterHandshake = true;
      sendVolumeGet();
    }
    return;
  }
  if (strncmp(line, "VOL ", 4) == 0) {
    int v = atoi(line + 4);
    if (v < 0) v = 0;
    if (v > 100) v = 100;
    // 只有在不处于音量调节模式时才接受PC端的音量更新
    if (appMode != MODE_VOLUME_ADJUST) {
      volumeValue = static_cast<uint8_t>(v);
      updateVolumeWidgets();
    }
  } else if (strncmp(line, "MUTE ", 5) == 0) {
    int m = atoi(line + 5);
    muteState = (m != 0);
    updateMuteWidgets();
  } else if (strcmp(line, "SPK BEGIN") == 0) {
    speakersLoading = true;
    speakers.clear();
    rebuildOutputMenu();
  } else if (strncmp(line, "SPK ITEM ", 9) == 0) {
    const char *p = line + 9;
    int id = atoi(p);
    const char *name = strchr(p, ' ');
    if (name) {
      name++;
    } else {
      name = "";
    }
    speakers.push_back({id, name});
  } else if (strcmp(line, "SPK END") == 0) {
    speakersLoading = false;
    rebuildOutputMenu();
  } else if (strncmp(line, "SPK CUR ", 8) == 0) {
    speakerCurrentId = atoi(line + 8);
    rebuildOutputMenu();
  } else if (strcmp(line, "MIC BEGIN") == 0) {
    microphonesLoading = true;
    microphones.clear();
    rebuildInputMenu();
  } else if (strncmp(line, "MIC ITEM ", 9) == 0) {
    const char *p = line + 9;
    int id = atoi(p);
    const char *name = strchr(p, ' ');
    if (name) {
      name++;
    } else {
      name = "";
    }
    microphones.push_back({id, name});
  } else if (strcmp(line, "MIC END") == 0) {
    microphonesLoading = false;
    rebuildInputMenu();
  } else if (strncmp(line, "MIC CUR ", 8) == 0) {
    microphoneCurrentId = atoi(line + 8);
    rebuildInputMenu();
  } else if (strncmp(line, "LRC CUR ", 8) == 0) {
    // Current lyric line
    const char *text = line + 8;
    strncpy(currentLyric.text, text, sizeof(currentLyric.text) - 1);
    currentLyric.text[sizeof(currentLyric.text) - 1] = '\0';
    currentLyric.active = true;
    lyricScrollOffset = 0;
    lyricLastUpdateMs = esp_timer_get_time() / 1000;
    
    // Debug: show bytes
    ESP_LOGI("LYRIC", "Received: %s", text);
    ESP_LOGI("LYRIC", "Length: %d bytes", strlen(text));
    for(int i = 0; i < strlen(text) && i < 20; i++) {
      ESP_LOG_BUFFER_HEX("LYRIC", (uint8_t*)&text[i], 1);
    }
  } else if (strncmp(line, "LRC NXT ", 8) == 0) {
    // Next lyric line (preview)
    const char *text = line + 8;
    strncpy(nextLyric.text, text, sizeof(nextLyric.text) - 1);
    nextLyric.text[sizeof(nextLyric.text) - 1] = '\0';
    nextLyric.active = true;
  } else if (strcmp(line, "LRC CLR") == 0) {
    // Clear lyrics
    currentLyric.active = false;
    nextLyric.active = false;
    lyricScrollOffset = 0;
    lyricLastUpdateMs = 0;
  } else if (strncmp(line, "NP META ", 8) == 0) {
    // Treat any NP message as a valid handshake to stop HELLO spam.
    handshakeOk = true;
    if (!syncedAfterHandshake) {
      syncedAfterHandshake = true;
      sendVolumeGet();
    }
    const char *p = line + 8;
    const char *sep = strchr(p, '\t');
    if (!sep) sep = strchr(p, '|');
    size_t titleLen = sep ? static_cast<size_t>(sep - p) : strlen(p);
    if (titleLen >= sizeof(nowPlaying.title)) titleLen = sizeof(nowPlaying.title) - 1;
    char newTitle[64];
    strncpy(newTitle, p, titleLen);
    newTitle[titleLen] = '\0';

    char newArtist[64];
    if (sep) {
      const char *artist = sep + 1;
      size_t artistLen = strlen(artist);
      if (artistLen >= sizeof(newArtist)) artistLen = sizeof(newArtist) - 1;
      strncpy(newArtist, artist, artistLen);
      newArtist[artistLen] = '\0';
    } else {
      newArtist[0] = '\0';
    }

    bool sameMeta = (strcmp(nowPlaying.title, newTitle) == 0) &&
                    (strcmp(nowPlaying.artist, newArtist) == 0);
    strncpy(nowPlaying.title, newTitle, sizeof(nowPlaying.title) - 1);
    nowPlaying.title[sizeof(nowPlaying.title) - 1] = '\0';
    strncpy(nowPlaying.artist, newArtist, sizeof(nowPlaying.artist) - 1);
    nowPlaying.artist[sizeof(nowPlaying.artist) - 1] = '\0';
    nowPlaying.active = true;
    nowPlaying.lastUpdateMs = static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
    if (!sameMeta) {
      nowPlaying.coverValid = false;
      nowPlaying.coverReceiving = false;
      nowPlaying.coverIndex = 0;
      nowPlaying.coverTotal = NP_COVER_PIXELS;
      memset(nowPlaying.coverFront, 0, sizeof(nowPlaying.coverFront));
      memset(nowPlaying.coverBack, 0, sizeof(nowPlaying.coverBack));
    }
    sendLine("APP RX NP META");
  } else if (strncmp(line, "NP PROG ", 8) == 0) {
    handshakeOk = true;
    long pos = 0;
    long dur = 0;
    if (sscanf(line + 8, "%ld %ld", &pos, &dur) == 2) {
      nowPlaying.posMs = static_cast<int32_t>(pos);
      nowPlaying.durMs = static_cast<int32_t>(dur);
      nowPlaying.active = true;
      nowPlaying.lastUpdateMs = static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
    }
  } else if (strncmp(line, "NP CLR", 6) == 0) {
    handshakeOk = true;
    clearNowPlayingState(true);
  } else if (strncmp(line, "NP COV BEGIN", 12) == 0) {
    handshakeOk = true;
    nowPlaying.coverReceiving = true;
    nowPlaying.coverIndex = 0;
    nowPlaying.coverTotal = NP_COVER_PIXELS;
    nowPlaying.lastUpdateMs = static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
    int w = 0;
    int h = 0;
    if (sscanf(line + 12, "%d %d", &w, &h) == 2 && w > 0 && h > 0) {
      int total = w * h;
      if (total > 0 && total < NP_COVER_PIXELS) {
        nowPlaying.coverTotal = total;
      } else {
        nowPlaying.coverTotal = NP_COVER_PIXELS;
      }
    }
    memset(nowPlaying.coverBack, 0, sizeof(nowPlaying.coverBack));
    sendLine("APP RX NP COV BEGIN");
  } else if (strncmp(line, "NP COV DATA ", 12) == 0) {
    handshakeOk = true;
    nowPlaying.lastUpdateMs = static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
    const char *hex = line + 12;
    size_t len = strlen(hex);
    int coverLimit = nowPlaying.coverTotal > 0 ? nowPlaying.coverTotal : NP_COVER_PIXELS;
    if (coverLimit > NP_COVER_PIXELS) coverLimit = NP_COVER_PIXELS;
    for (size_t i = 0; i + 3 < len && nowPlaying.coverIndex < coverLimit; i += 4) {
      auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
        if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
        return -1;
      };
      int n0 = nibble(hex[i]);
      int n1 = nibble(hex[i + 1]);
      int n2 = nibble(hex[i + 2]);
      int n3 = nibble(hex[i + 3]);
      if (n0 < 0 || n1 < 0 || n2 < 0 || n3 < 0) continue;
      uint16_t value = static_cast<uint16_t>((n0 << 12) | (n1 << 8) | (n2 << 4) | n3);
      // Cover data is transferred as hex; swap bytes to match panel endian.
      value = static_cast<uint16_t>((value >> 8) | (value << 8));
      nowPlaying.coverBack[nowPlaying.coverIndex++] = value;
    }
  } else if (strncmp(line, "NP COV END", 10) == 0) {
    handshakeOk = true;
    nowPlaying.lastUpdateMs = static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
    int coverLimit = nowPlaying.coverTotal > 0 ? nowPlaying.coverTotal : NP_COVER_PIXELS;
    if (coverLimit > NP_COVER_PIXELS) coverLimit = NP_COVER_PIXELS;
    if (nowPlaying.coverIndex >= coverLimit) {
      nowPlaying.coverReceiving = false;
      memcpy(nowPlaying.coverFront, nowPlaying.coverBack, sizeof(nowPlaying.coverFront));
      nowPlaying.coverValid = true;
    } else {
      nowPlaying.coverReceiving = false;
      nowPlaying.coverValid = false;
    }
    ESP_LOGI("COVER", "COV END count=%d", nowPlaying.coverIndex);
    char msg[64];
    snprintf(msg, sizeof(msg), "APP RX NP COV END %d", nowPlaying.coverIndex);
    sendLine(msg);
  }
}

void readSerial() {
  uint8_t data[64];
  int len = uart_read_bytes(UART_NUM_0, data, sizeof(data), 0);
  if (len > 0) {
    rxBytes += static_cast<uint32_t>(len);
    lastUsbRxMs = static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
  }
  for (int i = 0; i < len; ++i) {
    char c = static_cast<char>(data[i]);
    if (c == '\n') {
      serialBuf[serialLen] = '\0';
      if (serialLen > 0) handleLine(serialBuf);
      serialLen = 0;
    } else if (c != '\r') {
      if (serialLen < sizeof(serialBuf) - 1) {
        serialBuf[serialLen++] = c;
      }
    }
  }
}

int speakerIdForMenu(Menu *item) {
  for (const auto &pair : speakerMenuMap) {
    if (pair.first == item) return pair.second;
  }
  return -1;
}

int micIdForMenu(Menu *item) {
  for (const auto &pair : microphoneMenuMap) {
    if (pair.first == item) return pair.second;
  }
  return -1;
}

void handleConfirm(Menu *current) {
  if (!current) return;
  Menu *selected = current->childMenu.empty() ? nullptr : current->childMenu[current->selectIndex];

  if (current == menuMain) {
    if (selected == menuVolume) {
      if (!canUseControlFeatures()) {
        popInfoGlobal("Link Unavailable", 700);
        return;
      }
      appMode = MODE_VOLUME_ADJUST;
      adjustActive = true;
      adjustTarget = ADJ_VOLUME;
      return;
    }
    if (selected == menuMute) {
      if (!canUseControlFeatures()) {
        popInfoGlobal("Link Unavailable", 700);
        return;
      }
      muteState = muteCheck->toggle();
      updateMuteWidgets();
      sendMuteToggle();
      return;
    }
    if (selected == menuOutput) {
      if (!canUseControlFeatures()) {
        popInfoGlobal("Need Wired/BLE Client", 800);
        return;
      }
      launcher.open();
      // 首次进入时自动刷新设备列表
      if (!speakersRequested && !speakersLoading) {
        speakersRequested = true;
        speakersLoading = true;
        sendSpeakerListRequest();
        rebuildOutputMenu();
      }
      return;
    }
    if (selected == menuInput) {
      if (!canUseControlFeatures()) {
        popInfoGlobal("Need Wired/BLE Client", 800);
        return;
      }
      launcher.open();
      if (!microphonesRequested && !microphonesLoading) {
        microphonesRequested = true;
        microphonesLoading = true;
        sendMicListRequest();
        rebuildInputMenu();
      }
      return;
    }
    if (selected == menuTest) {
      launcher.open();
      return;
    }
    if (selected == menuSettings) {
      launcher.open();
      return;
    }
    return;
  }

  if (current == menuOutput) {
    if (!canUseControlFeatures()) {
      popInfoGlobal("Need Wired/BLE Client", 800);
      return;
    }
    if (selected == outputRefreshItem) {
      speakersLoading = true;
      sendSpeakerListRequest();
      rebuildOutputMenu();
      return;
    }
    int id = speakerIdForMenu(selected);
    if (id >= 0) {
      speakerCurrentId = id;
      sendSpeakerSet(id);
      rebuildOutputMenu();
    }
    return;
  }

  if (current == menuInput) {
    if (!canUseControlFeatures()) {
      popInfoGlobal("Need Wired/BLE Client", 800);
      return;
    }
    if (selected == inputRefreshItem) {
      microphonesLoading = true;
      sendMicListRequest();
      rebuildInputMenu();
      return;
    }
    int id = micIdForMenu(selected);
    if (id >= 0) {
      microphoneCurrentId = id;
      sendMicSet(id);
      rebuildInputMenu();
    }
    return;
  }

  if (current == menuTest) {
    if (selected == testIoItem) {
      popInfoGlobal("IO Test", 600);
      return;
    }
    if (selected == test4bitItem) {
      appMode = MODE_TEST_PATTERN;
      adjustActive = true;
      adjustTarget = ADJ_NONE;
      return;
    }
    if (selected == testSpiItem) {
      appMode = MODE_SPI_ADJUST;
      spiPending = spiMHz;
      spiDirty = false;
      updateDisplayWidgets();
      adjustActive = true;
      adjustTarget = ADJ_SPI;
      return;
    }
    if (selected == testDmaItem) {
      dmaEnabled = dmaCheck->toggle();
      applyDisplayConfig();
      return;
    }
    if (selected == testFbItem) {
      fbEnabled = fbCheck->toggle();
      applyDisplayConfig();
      return;
    }
    if (selected == testPsramItem) {
      psramEnabled = psramCheck->toggle();
      applyDisplayConfig();
      return;
    }
    if (selected == testDoubleBufferItem) {
      doubleBufferEnabled = doubleBufferCheck->toggle();
      applyDisplayConfig();
      return;
    }
    if (selected == testFpsItem) {
      showFps = fpsCheck->toggle();
      updatePerfWidgets();
      return;
    }
    if (selected == testCpuItem) {
      showCpu = cpuCheck->toggle();
      updatePerfWidgets();
      return;
    }
    if (selected == testUpItem) {
      showUp = upCheck->toggle();
      updatePerfWidgets();
      return;
    }
    if (selected == testDownItem) {
      showDown = downCheck->toggle();
      updatePerfWidgets();
      return;
    }
    if (selected == testLogItem) {
      mcuLogEnabled = logCheck->toggle();
      updatePerfWidgets();
      popInfoGlobal(mcuLogEnabled ? "Log On" : "Log Off", 500);
      return;
    }
  }

  if (current == menuSettings) {
    if (selected == uiSpeedItem) {
      appMode = MODE_UI_ADJUST;
      uiSpeedPending = uiSpeedValue;
      uiSpeedDirty = false;
      updateSettingsWidgets();
      adjustActive = true;
      adjustTarget = ADJ_UI_SPEED;
      return;
    }
    if (selected == selSpeedItem) {
      appMode = MODE_SEL_ADJUST;
      selSpeedPending = selSpeedValue;
      selSpeedDirty = false;
      updateSettingsWidgets();
      adjustActive = true;
      adjustTarget = ADJ_SEL_SPEED;
      return;
    }
    if (selected == wrapPauseItem) {
      appMode = MODE_WRAP_ADJUST;
      wrapPausePending = wrapPauseValue;
      wrapPauseDirty = false;
      updateSettingsWidgets();
      adjustActive = true;
      adjustTarget = ADJ_WRAP_PAUSE;
      return;
    }
    if (selected == fontColorItem) {
      appMode = MODE_COLOR_ADJUST;
      fontColorPending = fontColorValue;
      fontColorDirty = false;
      updateSettingsWidgets();
      adjustActive = true;
      adjustTarget = ADJ_FONT_COLOR;
      return;
    }
    if (selected == scrollTimeItem) {
      appMode = MODE_SCROLL_ADJUST;
      scrollTimePending = scrollTimeValue;
      scrollTimeDirty = false;
      updateSettingsWidgets();
      adjustActive = true;
      adjustTarget = ADJ_SCROLL_TIME;
      return;
    }
    if (selected == lyricScrollItem) {
      appMode = MODE_LYRIC_SCROLL_ADJUST;
      lyricScrollCpsPending = lyricScrollCpsValue;
      lyricScrollCpsDirty = false;
      updateSettingsWidgets();
      adjustActive = true;
      adjustTarget = ADJ_LYRIC_SCROLL_CPS;
      return;
    }
    if (selected == npAutoCloseItem) {
      appMode = MODE_NP_CLOSE_ADJUST;
      npAutoCloseSecPending = npAutoCloseSecValue;
      npAutoCloseSecDirty = false;
      updateSettingsWidgets();
      adjustActive = true;
      adjustTarget = ADJ_NP_CLOSE_SEC;
      return;
    }
    if (selected == cfgMsgAutoCloseItem) {
      appMode = MODE_CFG_CLOSE_ADJUST;
      cfgMsgAutoClosePending = cfgMsgAutoCloseValue;
      cfgMsgAutoCloseDirty = false;
      updateSettingsWidgets();
      adjustActive = true;
      adjustTarget = ADJ_CFG_CLOSE;
      return;
    }
    if (selected == menuBluetooth) {
      launcher.open();
      return;
    }
    if (selected == menuAbout) {
      // Display About info screen
      appMode = MODE_ABOUT_INFO;
      setAdjustBaseStep(5);
      setAdjustTimeScale(0.25f);
      adjustActive = true;
      adjustTarget = ADJ_NONE;
      return;
    }
  }

  if (current == menuBluetooth) {
    if (selected == bleStatusItem) {
      updateBluetoothMenuItems();
      popInfoGlobal(bleStatusItem->title, 800);
      return;
    }
    if (selected == bleRouteItem) {
      if (commMode == COMM_AUTO) commMode = COMM_USB;
      else if (commMode == COMM_USB) commMode = COMM_BLE;
      else commMode = COMM_AUTO;
      updateBluetoothMenuItems();
      popInfoGlobal(bleRouteItem->title, 700);
      saveSettings();
      return;
    }
    if (selected == bleEnableItem) {
      bool next = bleEnableCheck->toggle();
      if (next && !bleEnabled) {
        ble_set_debug_callback(bleDebugForward);
        if (ble_service_init("SongLed")) {
          bleEnabled = true;
          popInfoGlobal("BLE", 600);
        } else {
          bleEnabled = false;
          bleEnableCheck->value = false;
          popInfoGlobal("BLE Init Fail", 800);
        }
      } else if (!next && bleEnabled) {
        ble_service_deinit();
        bleEnabled = false;
        popInfoGlobal("BLE Off", 600);
      }
      updateSettingsWidgets();
      return;
    }
    if (selected == bleAdvItem) {
      if (bleEnabled) {
        ble_restart_advertising();
        popInfoGlobal("Adv Restart", 600);
      } else {
        popInfoGlobal("BLE Off", 600);
      }
      return;
    }
    if (selected == bleDisconnectItem) {
      if (bleEnabled) {
        ble_disconnect();
        popInfoGlobal("BLE Disc", 600);
      } else {
        popInfoGlobal("BLE Off", 600);
      }
      return;
    }
  }
}

void handleKeyEvents() {
  if (*HAL::getKeyFlag() != key::KEY_PRESSED) return;

  auto *keys = HAL::getKeyMap();
  Menu *current = launcher.getCurrentMenu();
  bool adjustVolume = (appMode == MODE_VOLUME_ADJUST);
  bool adjustSpi = (appMode == MODE_SPI_ADJUST);
  bool adjustUi = (appMode == MODE_UI_ADJUST);
  bool adjustSel = (appMode == MODE_SEL_ADJUST);
  bool adjustWrap = (appMode == MODE_WRAP_ADJUST);
  bool adjustColor = (appMode == MODE_COLOR_ADJUST);
  bool adjustScroll = (appMode == MODE_SCROLL_ADJUST);
  bool adjustLyricScroll = (appMode == MODE_LYRIC_SCROLL_ADJUST);
  bool adjustNpClose = (appMode == MODE_NP_CLOSE_ADJUST);
  bool adjustCfgClose = (appMode == MODE_CFG_CLOSE_ADJUST);
  bool adjustTest = (appMode == MODE_TEST_PATTERN);
  bool adjustAbout = (appMode == MODE_ABOUT_INFO);
  bool adjustMode = adjustActive && (adjustVolume || adjustSpi || adjustUi || adjustSel || adjustWrap || adjustColor || adjustScroll || adjustLyricScroll || adjustNpClose || adjustCfgClose || adjustTest || adjustAbout);
  auto consumeQueuedMenuSteps = [&]() {
    if (adjustMode || !launcher.getSelector()) return;
    int queued = hal.consumeQueuedEncoderSteps();
    if (queued == 0) return;
    int steps = (queued > 0) ? queued : -queued;
    if (steps > 18) steps = 18;  // avoid accidental long jump from noisy burst
    if (queued > 0) {
      while (steps-- > 0) launcher.getSelector()->goNext();
    } else {
      while (steps-- > 0) launcher.getSelector()->goPreview();
    }
  };

  if (keys[key::KEY_0] == key::CLICK) {
    if (adjustMode) {
      uint32_t nowMs = static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
      int step = getAdjustStep(nowMs);
      if (adjustVolume) {
        if (volumeValue >= step) volumeValue = static_cast<uint8_t>(volumeValue - step);
        updateVolumeWidgets();
        sendVolumeSet();
      } else if (adjustSpi) {
        if (spiPending > step) spiPending = static_cast<uint8_t>(spiPending - step);
        else spiPending = 1;
        spiDirty = true;
        updateDisplayWidgets();
      } else if (adjustUi) {
        if (uiSpeedPending > step) uiSpeedPending = static_cast<uint8_t>(uiSpeedPending - step);
        else uiSpeedPending = 1;
        uiSpeedDirty = true;
        updateSettingsWidgets();
      } else if (adjustSel) {
        if (selSpeedPending > 1 + step) selSpeedPending = static_cast<uint8_t>(selSpeedPending - step);
        else selSpeedPending = 1;
        selSpeedDirty = true;
        astra::getUIConfig().selectorListDurationMs = mapSelSpeedMs(selSpeedPending);
        updateSettingsWidgets();
      } else if (adjustWrap) {
        if (wrapPausePending > step) wrapPausePending = static_cast<uint8_t>(wrapPausePending - step);
        else wrapPausePending = 0;
        wrapPauseDirty = true;
        updateSettingsWidgets();
      } else if (adjustColor) {
        if (fontColorPending > step) fontColorPending = static_cast<uint8_t>(fontColorPending - step);
        else fontColorPending = 0;
        fontColorDirty = true;
        updateSettingsWidgets();
      } else if (adjustScroll) {
        if (scrollTimePending > step) scrollTimePending = static_cast<uint8_t>(scrollTimePending - step);
        else scrollTimePending = 1;
        scrollTimeDirty = true;
        updateSettingsWidgets();
      } else if (adjustLyricScroll) {
        if (lyricScrollCpsPending > step) lyricScrollCpsPending = static_cast<uint8_t>(lyricScrollCpsPending - step);
        else lyricScrollCpsPending = 1;
        lyricScrollCpsDirty = true;
        updateSettingsWidgets();
      } else if (adjustNpClose) {
        if (npAutoCloseSecPending > step) npAutoCloseSecPending = static_cast<uint8_t>(npAutoCloseSecPending - step);
        else npAutoCloseSecPending = 0;
        npAutoCloseSecDirty = true;
        updateSettingsWidgets();
      } else if (adjustCfgClose) {
        if (cfgMsgAutoClosePending > step) cfgMsgAutoClosePending = static_cast<uint8_t>(cfgMsgAutoClosePending - step);
        else cfgMsgAutoClosePending = 0;
        cfgMsgAutoCloseDirty = true;
        updateSettingsWidgets();
      } else if (adjustAbout) {
        // Scroll up
        extern int aboutScrollOffset;
        extern int aboutScrollTarget;
        if (aboutScrollOffset > 0) {
          aboutScrollTarget -= step * 2;
          if (aboutScrollTarget < 0) aboutScrollTarget = 0;
        }
      }
    } else if (launcher.getSelector()) {
      launcher.getSelector()->goPreview();
      consumeQueuedMenuSteps();
    }
  }
  if (keys[key::KEY_1] == key::CLICK) {
    if (adjustMode) {
      uint32_t nowMs = static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
      int step = getAdjustStep(nowMs);
      if (adjustVolume) {
        if (volumeValue + step <= 100) volumeValue = static_cast<uint8_t>(volumeValue + step);
        else volumeValue = 100;
        updateVolumeWidgets();
        sendVolumeSet();
      } else if (adjustSpi) {
        if (spiPending + step <= 80) spiPending = static_cast<uint8_t>(spiPending + step);
        else spiPending = 80;
        spiDirty = true;
        updateDisplayWidgets();
      } else if (adjustUi) {
        if (uiSpeedPending + step <= 50) uiSpeedPending = static_cast<uint8_t>(uiSpeedPending + step);
        else uiSpeedPending = 50;
        uiSpeedDirty = true;
        updateSettingsWidgets();
      } else if (adjustSel) {
        if (selSpeedPending + step <= 50) selSpeedPending = static_cast<uint8_t>(selSpeedPending + step);
        else selSpeedPending = 50;
        selSpeedDirty = true;
        astra::getUIConfig().selectorListDurationMs = mapSelSpeedMs(selSpeedPending);
        updateSettingsWidgets();
      } else if (adjustWrap) {
        if (wrapPausePending + step <= 50) wrapPausePending = static_cast<uint8_t>(wrapPausePending + step);
        else wrapPausePending = 50;
        wrapPauseDirty = true;
        updateSettingsWidgets();
      } else if (adjustColor) {
        if (fontColorPending + step <= 50) fontColorPending = static_cast<uint8_t>(fontColorPending + step);
        else fontColorPending = 50;
        fontColorDirty = true;
        updateSettingsWidgets();
      } else if (adjustScroll) {
        if (scrollTimePending + step <= 50) scrollTimePending = static_cast<uint8_t>(scrollTimePending + step);
        else scrollTimePending = 50;
        scrollTimeDirty = true;
        updateSettingsWidgets();
      } else if (adjustLyricScroll) {
        if (lyricScrollCpsPending + step <= 30) lyricScrollCpsPending = static_cast<uint8_t>(lyricScrollCpsPending + step);
        else lyricScrollCpsPending = 30;
        lyricScrollCpsDirty = true;
        updateSettingsWidgets();
      } else if (adjustNpClose) {
        if (npAutoCloseSecPending + step <= 60) npAutoCloseSecPending = static_cast<uint8_t>(npAutoCloseSecPending + step);
        else npAutoCloseSecPending = 60;
        npAutoCloseSecDirty = true;
        updateSettingsWidgets();
      } else if (adjustCfgClose) {
        if (cfgMsgAutoClosePending + step <= 60) cfgMsgAutoClosePending = static_cast<uint8_t>(cfgMsgAutoClosePending + step);
        else cfgMsgAutoClosePending = 60;
        cfgMsgAutoCloseDirty = true;
        updateSettingsWidgets();
      } else if (adjustAbout) {
        // Scroll down
        extern int aboutScrollOffset;
        extern int aboutMaxScroll;
        extern int aboutScrollTarget;
        if (aboutScrollOffset < aboutMaxScroll) {
          aboutScrollTarget += step * 2;
          if (aboutScrollTarget > aboutMaxScroll) aboutScrollTarget = aboutMaxScroll;
        }
      }
    } else if (launcher.getSelector()) {
      launcher.getSelector()->goNext();
      consumeQueuedMenuSteps();
    }
  }

  if (keys[key::KEY_0] == key::PRESS) {
    if (adjustMode) {
      if (adjustSpi && spiDirty) {
        spiMHz = spiPending;
        applyDisplayConfig();
      }
      if (adjustUi && uiSpeedDirty) {
        uiSpeedValue = uiSpeedPending;
        boostUiSpeed(static_cast<float>(uiSpeedValue));
        saveSettings();
      }
      if (adjustSel && selSpeedDirty) {
        selSpeedValue = selSpeedPending;
        applySelectorSpeed();
        saveSettings();
      }
      if (adjustWrap && wrapPauseDirty) {
        wrapPauseValue = wrapPausePending;
        applyWrapPause();
        saveSettings();
      }
      if (adjustColor && fontColorDirty) {
        fontColorValue = fontColorPending;
        applyFontColor();
        saveSettings();
      }
      if (adjustScroll && scrollTimeDirty) {
        scrollTimeValue = scrollTimePending;
        applyScrollDuration();
        saveSettings();
      }
      if (adjustLyricScroll && lyricScrollCpsDirty) {
        lyricScrollCpsValue = lyricScrollCpsPending;
        applyLyricScrollSpeed();
        saveSettings();
      }
      if (adjustNpClose && npAutoCloseSecDirty) {
        npAutoCloseSecValue = npAutoCloseSecPending;
        applyNowPlayingAutoClose();
        saveSettings();
      }
      if (adjustCfgClose && cfgMsgAutoCloseDirty) {
        cfgMsgAutoCloseValue = cfgMsgAutoClosePending;
        cfgMsgAutoCloseMs = mapCfgCloseMs(cfgMsgAutoCloseValue);
        saveSettings();
      }
      if (adjustAbout) {
        // exit about info, reset scroll
        extern int aboutScrollOffset;
        extern int aboutScrollTarget;
        extern float aboutScrollPos;
        aboutScrollOffset = 0;
        aboutScrollTarget = 0;
        aboutScrollPos = 0.0f;
        setAdjustBaseStep(1);
        setAdjustTimeScale(1.0f);
      }
      if (adjustTest) {
        // exit test pattern
      }
      appMode = MODE_NORMAL;
      adjustActive = false;
      adjustTarget = ADJ_NONE;
    } else {
      if (current && current != menuMain && current->getPreview() != nullptr) {
        launcher.close();
      }
    }
  }
  if (keys[key::KEY_1] == key::PRESS) {
    if (adjustMode) {
      if (adjustSpi && spiDirty) {
        spiMHz = spiPending;
        applyDisplayConfig();
      }
      if (adjustUi && uiSpeedDirty) {
        uiSpeedValue = uiSpeedPending;
        boostUiSpeed(static_cast<float>(uiSpeedValue));
        saveSettings();
      }
      if (adjustSel && selSpeedDirty) {
        selSpeedValue = selSpeedPending;
        applySelectorSpeed();
        saveSettings();
      }
      if (adjustWrap && wrapPauseDirty) {
        wrapPauseValue = wrapPausePending;
        applyWrapPause();
        saveSettings();
      }
      if (adjustColor && fontColorDirty) {
        fontColorValue = fontColorPending;
        applyFontColor();
        saveSettings();
      }
      if (adjustScroll && scrollTimeDirty) {
        scrollTimeValue = scrollTimePending;
        applyScrollDuration();
        saveSettings();
      }
      if (adjustLyricScroll && lyricScrollCpsDirty) {
        lyricScrollCpsValue = lyricScrollCpsPending;
        applyLyricScrollSpeed();
        saveSettings();
      }
      if (adjustNpClose && npAutoCloseSecDirty) {
        npAutoCloseSecValue = npAutoCloseSecPending;
        applyNowPlayingAutoClose();
        saveSettings();
      }
      if (adjustCfgClose && cfgMsgAutoCloseDirty) {
        cfgMsgAutoCloseValue = cfgMsgAutoClosePending;
        cfgMsgAutoCloseMs = mapCfgCloseMs(cfgMsgAutoCloseValue);
        saveSettings();
      }
      if (adjustTest) {
        // exit test pattern
      }
      if (adjustAbout) {
        // exit about info, reset scroll
        extern int aboutScrollOffset;
        extern int aboutScrollTarget;
        extern float aboutScrollPos;
        aboutScrollOffset = 0;
        aboutScrollTarget = 0;
        aboutScrollPos = 0.0f;
        setAdjustBaseStep(1);
        setAdjustTimeScale(1.0f);
      }
      appMode = MODE_NORMAL;
      adjustActive = false;
      adjustTarget = ADJ_NONE;
    } else {
      handleConfirm(current);
    }
  }

  std::fill(keys, keys + key::KEY_NUM, key::INVALID);
  *HAL::getKeyFlag() = key::KEY_NOT_PRESSED;
}

void buildMenus() {
  auto cfg = hal.getDisplayConfig();
  int spiInit = cfg.spi_hz / 1000000;
  if (spiInit < 1) spiInit = 1;
  if (spiInit > 80) spiInit = 80;
  spiMHz = static_cast<uint8_t>(spiInit);
  spiPending = spiMHz;
  dmaEnabled = cfg.use_dma;
  fbEnabled = cfg.use_framebuffer;
  psramEnabled = cfg.use_psram;
  doubleBufferEnabled = cfg.use_double_buffer;
  uiSpeedPending = uiSpeedValue;
  selSpeedPending = selSpeedValue;
  wrapPausePending = wrapPauseValue;
  fontColorPending = fontColorValue;
  scrollTimePending = scrollTimeValue;
  lyricScrollCpsPending = lyricScrollCpsValue;
  npAutoCloseSecPending = npAutoCloseSecValue;

  menuMain = new List("Main");
  menuVolume = new List("Volume");
  menuOutput = new List("Output Device");
  menuInput = new List("Microphone");
  menuTest = new List("Test Tools");
  menuMute = new List("Mute");
  menuSettings = new List("Settings");

  volumeSlider = new Slider("Volume", 0, 100, 2, volumeValue);
  muteCheck = new CheckBox(muteState);

  menuMain->addItem(menuVolume, volumeSlider);
  menuMain->addItem(menuOutput);
  menuMain->addItem(menuInput);
  menuMain->addItem(menuTest);
  menuMain->addItem(menuSettings);
  menuMain->addItem(menuMute, muteCheck);

  outputRefreshItem = new List("Refresh List");
  menuOutput->addItem(outputRefreshItem);
  inputRefreshItem = new List("Refresh List");
  menuInput->addItem(inputRefreshItem);

  testIoItem = new List("IO Test");
  test4bitItem = new List("4bit Test");
  testSpiItem = new List("SPI");
  testDmaItem = new List("DMA");
  testFbItem = new List("FB");
  testPsramItem = new List("PSRAM");
  testDoubleBufferItem = new List("Double Buf");
  testFpsItem = new List("FPS");
  testCpuItem = new List("CPU");
  testUpItem = new List("UP");
  testDownItem = new List("DOWN");
  testLogItem = new List("MCU Log");
  fpsCheck = new CheckBox(showFps);
  cpuCheck = new CheckBox(showCpu);
  upCheck = new CheckBox(showUp);
  downCheck = new CheckBox(showDown);
  logCheck = new CheckBox(mcuLogEnabled);
  spiSlider = new Slider("SPI", 1, 80, 1, spiMHz);
  dmaCheck = new CheckBox(dmaEnabled);
  fbCheck = new CheckBox(fbEnabled);
  psramCheck = new CheckBox(psramEnabled);
  doubleBufferCheck = new CheckBox(doubleBufferEnabled);
  menuTest->addItem(testIoItem);
  menuTest->addItem(test4bitItem);
  menuTest->addItem(testSpiItem, spiSlider);
  menuTest->addItem(testDmaItem, dmaCheck);
  menuTest->addItem(testFbItem, fbCheck);
  menuTest->addItem(testPsramItem, psramCheck);
  menuTest->addItem(testDoubleBufferItem, doubleBufferCheck);
  menuTest->addItem(testFpsItem, fpsCheck);
  menuTest->addItem(testCpuItem, cpuCheck);
  menuTest->addItem(testUpItem, upCheck);
  menuTest->addItem(testDownItem, downCheck);
  menuTest->addItem(testLogItem, logCheck);

  uiSpeedItem = new List("UI Speed");
  uiSpeedSlider = new Slider("Speed", 1, 50, 1, uiSpeedValue);
  menuSettings->addItem(uiSpeedItem, uiSpeedSlider);
  selSpeedItem = new List("Sel Speed");
  selSpeedSlider = new Slider("Sel", 1, 50, 1, selSpeedValue);
  menuSettings->addItem(selSpeedItem, selSpeedSlider);
  wrapPauseItem = new List("Wrap Pause");
  wrapPauseSlider = new Slider("Pause", 0, 50, 1, wrapPauseValue);
  menuSettings->addItem(wrapPauseItem, wrapPauseSlider);
  fontColorItem = new List("Font Color");
  fontColorSlider = new Slider("Color", 0, 50, 1, fontColorValue);
  menuSettings->addItem(fontColorItem, fontColorSlider);
  scrollTimeItem = new List("Scroll Time");
  scrollTimeSlider = new Slider("Scroll", 1, 50, 1, scrollTimeValue);
  menuSettings->addItem(scrollTimeItem, scrollTimeSlider);
  lyricScrollItem = new List("Lyric Speed");
  lyricScrollSlider = new Slider("CPS", 1, 30, 1, lyricScrollCpsValue);
  menuSettings->addItem(lyricScrollItem, lyricScrollSlider);
  npAutoCloseItem = new List("Music Close");
  npAutoCloseSlider = new Slider("Close", 0, 60, 1, npAutoCloseSecValue);
  menuSettings->addItem(npAutoCloseItem, npAutoCloseSlider);
  
  cfgMsgAutoCloseItem = new List("Auto Close Msg");
  cfgMsgAutoCloseSlider = new Slider("Close", 0, 60, 1, cfgMsgAutoCloseValue);
  menuSettings->addItem(cfgMsgAutoCloseItem, cfgMsgAutoCloseSlider);

  // Bluetooth menu
  menuBluetooth = new List("Bluetooth");
  bleStatusItem = new List("Status: Offline");
  bleRouteItem = new List("Prefer: Auto");
  bleEnableItem = new List("Enable BLE");
  bleAdvItem = new List("Restart Adv");
  bleDisconnectItem = new List("Disconnect");
  bleEnableCheck = new CheckBox(bleEnabled);
  menuBluetooth->addItem(bleStatusItem);
  menuBluetooth->addItem(bleRouteItem);
  menuBluetooth->addItem(bleEnableItem, bleEnableCheck);
  menuBluetooth->addItem(bleAdvItem);
  menuBluetooth->addItem(bleDisconnectItem);
  menuSettings->addItem(menuBluetooth);
  
  // About menu
  menuAbout = new List("About");
  menuSettings->addItem(menuAbout);

  updateVolumeWidgets();
  updateMuteWidgets();
  updatePerfWidgets();
  updateDisplayWidgets();
  updateSettingsWidgets();
  rebuildOutputMenu();
  rebuildInputMenu();
}

float mapUiSpeedScale(uint8_t value) {
  if (value < 1) value = 1;
  if (value > 50) value = 50;
  return static_cast<float>(value);
}

uint8_t mapSelSpeedFromMs(float ms) {
  float maxMs = 900.0f;
  float minMs = 20.0f;
  if (ms < minMs) ms = minMs;
  if (ms > maxMs) ms = maxMs;
  float base = 1.08f;
  float value = 1.0f + std::log(maxMs / ms) / std::log(base);
  if (value < 1.0f) value = 1.0f;
  if (value > 50.0f) value = 50.0f;
  return static_cast<uint8_t>(std::round(value));
}

float mapSelSpeedMs(uint8_t value) {
  if (value < 1) value = 1;
  if (value > 50) value = 50;
  float maxMs = 900.0f;
  float base = 1.08f;
  float ms = maxMs / std::pow(base, static_cast<float>(value) - 1.0f);
  if (ms < 20.0f) ms = 20.0f;
  if (ms > 900.0f) ms = 900.0f;
  return ms;
}

uint8_t mapWrapPauseFromMs(float ms) {
  if (ms < 0.0f) ms = 0.0f;
  if (ms > 1000.0f) ms = 1000.0f;
  return static_cast<uint8_t>(std::round(ms / 20.0f));
}

float mapWrapPauseMs(uint8_t value) {
  if (value > 50) value = 50;
  return static_cast<float>(value) * 20.0f;
}

uint8_t mapScrollFromMs(float ms) {
  if (ms < 100.0f) ms = 100.0f;
  if (ms > 15000.0f) ms = 15000.0f;
  float value = 1.0f + std::round((ms - 100.0f) * 49.0f / 14900.0f);
  if (value < 1.0f) value = 1.0f;
  if (value > 50.0f) value = 50.0f;
  return static_cast<uint8_t>(value);
}

float mapScrollMs(uint8_t value) {
  if (value < 1) value = 1;
  if (value > 50) value = 50;
  return 100.0f + (static_cast<float>(value) - 1.0f) * (14900.0f / 49.0f);
}

float mapHueDeg(uint8_t value) {
  if (value > 50) value = 50;
  return static_cast<float>(value) * 360.0f / 50.0f;
}

uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

uint16_t dim565(uint16_t color, float factor) {
  if (factor < 0.0f) factor = 0.0f;
  if (factor > 1.0f) factor = 1.0f;
  uint8_t r = static_cast<uint8_t>((color >> 11) & 0x1F);
  uint8_t g = static_cast<uint8_t>((color >> 5) & 0x3F);
  uint8_t b = static_cast<uint8_t>(color & 0x1F);
  r = static_cast<uint8_t>(std::round(r * factor));
  g = static_cast<uint8_t>(std::round(g * factor));
  b = static_cast<uint8_t>(std::round(b * factor));
  return static_cast<uint16_t>((r << 11) | (g << 5) | b);
}

uint16_t hueToRgb565(uint8_t value) {
  float h = mapHueDeg(value);
  float c = 1.0f;
  float x = c * (1.0f - std::fabs(std::fmod(h / 60.0f, 2.0f) - 1.0f));
  float r = 0.0f, g = 0.0f, b = 0.0f;
  if (h < 60.0f) {
    r = c; g = x; b = 0.0f;
  } else if (h < 120.0f) {
    r = x; g = c; b = 0.0f;
  } else if (h < 180.0f) {
    r = 0.0f; g = c; b = x;
  } else if (h < 240.0f) {
    r = 0.0f; g = x; b = c;
  } else if (h < 300.0f) {
    r = x; g = 0.0f; b = c;
  } else {
    r = c; g = 0.0f; b = x;
  }
  uint8_t rr = static_cast<uint8_t>(std::round(r * 255.0f));
  uint8_t gg = static_cast<uint8_t>(std::round(g * 255.0f));
  uint8_t bb = static_cast<uint8_t>(std::round(b * 255.0f));
  return rgb565(rr, gg, bb);
}

uint8_t mapCfgCloseFromMs(uint16_t ms) {
  if (ms >= 65535) return 60;
  if (ms == 0) return 0;
  if (ms < 200) return 1;
  if (ms > 11800) return 59;
  return static_cast<uint8_t>(1 + ms / 200);
}

void rebuildInputMenu() {
  for (auto *item : menuInput->childMenu) {
    if (item == inputRefreshItem) continue;
    delete item;
  }
  menuInput->childMenu.clear();
  menuInput->childWidget.clear();
  microphoneMenuMap.clear();

  menuInput->addItem(inputRefreshItem);

  if (microphonesLoading) {
    auto *loading = new List("Loading...");
    menuInput->addItem(loading);
  } else if (microphones.empty()) {
    auto *none = new List("No devices");
    menuInput->addItem(none);
  } else {
    for (const auto &mic : microphones) {
      std::string title = mic.name;
      if (mic.id == microphoneCurrentId) title = "* " + title;
      auto *item = new List(title);
      menuInput->addItem(item);
      microphoneMenuMap.push_back({item, mic.id});
    }
  }

  menuInput->forePosInit();
  if (launcher.getCamera()) {
    menuInput->childPosInit(launcher.getCamera()->getPosition());
  }
  if (launcher.getCurrentMenu() == menuInput && launcher.getSelector()) {
    launcher.getSelector()->inject(menuInput);
  }
}

uint16_t mapCfgCloseMs(uint8_t value) {
  if (value == 0) return 0;
  if (value >= 60) return 65535;
  return static_cast<uint16_t>((value - 1) * 200 + 200);
}

uint8_t mapNpCloseFromMs(uint16_t ms) {
  if (ms >= 65535) return 60;
  if (ms == 0) return 0;
  if (ms < 1000) return 1;
  if (ms > 59000) return 59;
  return static_cast<uint8_t>(ms / 1000);
}

uint16_t mapNpCloseMs(uint8_t value) {
  if (value == 0) return 0;
  if (value >= 60) return 65535;
  return static_cast<uint16_t>(value * 1000);
}

uint16_t getPopupDurationMs(uint16_t fallbackMs) {
  if (cfgMsgAutoCloseMs == 0) return 1;
  if (cfgMsgAutoCloseMs >= 65535) return 65535;
  return cfgMsgAutoCloseMs;
}

void popInfoGlobal(const std::string &text, uint16_t fallbackMs) {
  launcher.popInfo(text, getPopupDurationMs(fallbackMs));
}

int countUtf8Glyphs(const std::string &text) {
  int count = 0;
  for (unsigned char c : text) {
    if ((c & 0xC0) != 0x80) count++;
  }
  return count;
}

void boostUiSpeed(float sliderValue) {
  uint8_t v = static_cast<uint8_t>(std::round(sliderValue));
  float scale = mapUiSpeedScale(v);
  if (scale < 0.1f) scale = 0.1f;
  if (scale > 50.0f) scale = 50.0f;
  astra::g_anim_speed_scale = scale;
}

void applySelectorSpeed() {
  astra::getUIConfig().selectorListDurationMs = mapSelSpeedMs(selSpeedValue);
}

void applyWrapPause() {
  astra::getUIConfig().wrapPauseMs = mapWrapPauseMs(wrapPauseValue);
}

void applyScrollDuration() {
  astra::getUIConfig().listScrollLoopMs = mapScrollMs(scrollTimeValue);
}

void applyLyricScrollSpeed() {
  if (lyricScrollCpsValue < 1) lyricScrollCpsValue = 1;
  if (lyricScrollCpsValue > 30) lyricScrollCpsValue = 30;
}

void applyNowPlayingAutoClose() {
  if (npAutoCloseSecValue > 60) npAutoCloseSecValue = 60;
}

void applyFontColor() {
  hal.setForegroundColor(hueToRgb565(fontColorValue));
}

void clearNowPlayingState(bool clearLyrics) {
  nowPlaying.active = false;
  nowPlaying.coverValid = false;
  nowPlaying.coverReceiving = false;
  nowPlaying.coverIndex = 0;
  nowPlaying.coverTotal = NP_COVER_PIXELS;
  nowPlaying.lastUpdateMs = 0;
  memset(nowPlaying.coverFront, 0, sizeof(nowPlaying.coverFront));
  memset(nowPlaying.coverBack, 0, sizeof(nowPlaying.coverBack));
  hal.clearImageOverlay();
  hal.clearBarOverlay();
  if (clearLyrics) {
    currentLyric.active = false;
    nextLyric.active = false;
    lyricScrollOffset = 0;
    lyricLastUpdateMs = 0;
  }
}

bool loadSettings() {
  nvs_handle_t handle;
  if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
    return false;
  }
  uint8_t val = 0;
  if (nvs_get_u8(handle, "ui_speed", &val) == ESP_OK) {
    if (val >= 1 && val <= 50) uiSpeedValue = val;
  }
  if (nvs_get_u8(handle, "sel_speed", &val) == ESP_OK) {
    if (val >= 1 && val <= 50) {
      selSpeedValue = val;
    } else if (val >= 60) {
      selSpeedValue = mapSelSpeedFromMs(static_cast<float>(val));
    }
  }
  if (nvs_get_u8(handle, "wrap_pause", &val) == ESP_OK) {
    if (val <= 50) {
      wrapPauseValue = val;
    } else {
      wrapPauseValue = mapWrapPauseFromMs(static_cast<float>(val));
    }
  }
  if (nvs_get_u8(handle, "font_hue", &val) == ESP_OK) {
    if (val <= 50) fontColorValue = val;
  }
  if (nvs_get_u8(handle, "scroll_ms", &val) == ESP_OK) {
    if (val >= 1 && val <= 50) {
      scrollTimeValue = val;
    } else if (val > 50) {
      scrollTimeValue = mapScrollFromMs(static_cast<float>(val));
    }
  }
  if (nvs_get_u8(handle, "lyric_cps", &val) == ESP_OK) {
    if (val >= 1 && val <= 30) {
      lyricScrollCpsValue = val;
    }
  }
  uint16_t npCloseVal = 0;
  if (nvs_get_u16(handle, "np_close_ms", &npCloseVal) == ESP_OK) {
    npAutoCloseSecValue = mapNpCloseFromMs(npCloseVal);
  } else if (nvs_get_u8(handle, "np_close_s", &val) == ESP_OK) {
    // Backward compatibility with old seconds-based setting.
    if (val <= 120) {
      uint16_t ms = (val == 0) ? 0 : static_cast<uint16_t>(std::min<int>(val * 1000, 59000));
      npAutoCloseSecValue = mapNpCloseFromMs(ms);
    }
  }
  uint16_t closeVal = 0;
  if (nvs_get_u16(handle, "cfg_close", &closeVal) == ESP_OK) {
    if (closeVal == 0 || closeVal >= 65535 || closeVal <= 11800) {
      cfgMsgAutoCloseMs = closeVal;
      cfgMsgAutoCloseValue = mapCfgCloseFromMs(cfgMsgAutoCloseMs);
      cfgMsgAutoClosePending = cfgMsgAutoCloseValue;
    }
  }
  if (nvs_get_u8(handle, "comm_mode", &val) == ESP_OK) {
    if (val <= static_cast<uint8_t>(COMM_AUTO)) {
      commMode = static_cast<CommMode>(val);
    }
  }
  nvs_close(handle);
  return true;
}

void renderLyrics() {
  if (!currentLyric.active) return;

  auto &sys = HAL::getSystemConfig();
  const int margin = 8;

  hal._setFont(u8g2_font_zpix);
  hal._setDrawType(0);

  const int fontH = HAL::getFontHeight();
  const int boxHeight = fontH * 2 + 8;
  int boxY = sys.screenHeight - boxHeight;
  if (boxY < 0) boxY = 0;
  hal._drawBox(0, boxY, sys.screenWeight, boxHeight);

  hal._setDrawType(1);

  auto drawScrolling = [&](const std::string &text, int baselineY, bool enableScroll) {
    if (text.empty()) return;
    std::string tmp = text;
    const int textW = HAL::getFontWidth(tmp);
    const int avail = sys.screenWeight - margin * 2;
    if (!enableScroll || textW <= avail) {
      hal._drawChinese(margin, baselineY, text);
      return;
    }

    const int gap = fontH * 2;
    const int cycle = textW + gap;
    float loopMs = mapScrollMs(scrollTimeValue);
    if (loopMs < 300.0f) loopMs = 300.0f;
    uint32_t nowMs = static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
    uint32_t elapsed = nowMs - lyricLastUpdateMs;
    if (elapsed < 300) {
      hal._drawChinese(margin, baselineY, text);
      return;
    }
    uint32_t phase = static_cast<uint32_t>(elapsed % static_cast<uint32_t>(loopMs));
    int offset = static_cast<int>(std::round(static_cast<float>(phase) * cycle / loopMs));
    int x = margin - offset;
    hal._drawChinese(x, baselineY, text);
    if (x + cycle < sys.screenWeight) {
      hal._drawChinese(x + cycle, baselineY, text);
    }
  };

  int baseY = sys.screenHeight - 4;
  int nextY = baseY - fontH - 4;

  drawScrolling(currentLyric.text, baseY, true);
  if (nextLyric.active && nextY > 0) {
    drawScrolling(nextLyric.text, nextY, false);
  }
}

void renderNowPlayingOverlay() {
  if (!nowPlaying.active) {
    hal.clearImageOverlay();
    hal.clearBarOverlay();
    return;
  }

  auto &sys = HAL::getSystemConfig();
  int padding = 6;
  int cover = NP_COVER_W;
  int fontH = HAL::getFontHeight();
  int textLines = 3;
  int textBlock = fontH * textLines + 4;
  int panelH = std::max(cover + padding * 2, textBlock + padding * 2 + 4);
  int panelW = sys.screenWeight - 16;
  if (panelW < cover + padding * 3 + 40) panelW = sys.screenWeight - 4;
  int panelX = (sys.screenWeight - panelW) / 2;
  int panelY = sys.screenHeight - panelH - 4;
  if (panelY < 0) panelY = 0;
  int radius = 6;

  HAL::setDrawType(0);
  HAL::drawRBox(panelX, panelY, panelW, panelH, radius);
  HAL::setDrawType(1);
  HAL::drawRFrame(panelX, panelY, panelW, panelH, radius);

  int coverX = panelX + padding;
  int coverY = panelY + padding;
  if (nowPlaying.coverValid) {
    hal.setImageOverlay(nowPlaying.coverFront, NP_COVER_W, NP_COVER_H, coverX, coverY);
  } else {
    hal.clearImageOverlay();
    HAL::drawRFrame(coverX, coverY, cover, cover, 4);
    float ratio = 0.0f;
    int coverTotal = nowPlaying.coverTotal > 0 ? nowPlaying.coverTotal : NP_COVER_PIXELS;
    if (coverTotal > NP_COVER_PIXELS) coverTotal = NP_COVER_PIXELS;
    if (nowPlaying.coverIndex > 0) {
      ratio = std::min(1.0f, std::max(0.0f,
               static_cast<float>(nowPlaying.coverIndex) / static_cast<float>(coverTotal)));
    }
    int barW = cover - 4;
    int barH = 2;
    int barX = coverX + 2;
    int barY = coverY + cover - barH - 2;
    uint16_t fg = hal.getForegroundColor();
    uint16_t dim = dim565(fg, 0.25f);
    hal.setForegroundColor(dim);
    HAL::drawBox(barX, barY, barW, barH);
    hal.setForegroundColor(fg);
    if (ratio > 0.0f) {
      int fill = static_cast<int>(barW * ratio);
      if (fill > 0) {
        HAL::drawBox(barX, barY, fill, barH);
      }
    } else if (nowPlaying.coverReceiving) {
      int pulse = static_cast<int>((HAL::millis() / 120) % barW);
      HAL::drawBox(barX + pulse, barY, 2, barH);
    }
  }

  int textX = coverX + cover + padding;
  int textMaxW = panelX + panelW - padding - textX;
  int line1Y = coverY + fontH - 5;
  int line2Y = line1Y + fontH + 2;
  int line3Y = line2Y + fontH + 2;

  auto drawScroll = [&](const std::string &text, int x, int y, int maxW, int idx) {
    std::string tmp = text;
    int textW = HAL::getFontWidth(tmp);
    if (textW <= maxW) {
      int clipX0 = x;
      int clipY0 = y - fontH;
      int clipX1 = x + maxW;
      int clipY1 = y + 2;
      HAL::setClipWindow(clipX0, clipY0, clipX1, clipY1);
      HAL::drawChinese(x, y, text);
      HAL::clearClipWindow();
      return;
    }
    float gap = std::max(8.0f, maxW * 0.2f);
    float cyclePx = textW + gap;
    int glyphs = countUtf8Glyphs(text);
    float avgPx = glyphs > 0 ? static_cast<float>(textW) / static_cast<float>(glyphs) : static_cast<float>(textW);
    float cps = static_cast<float>(lyricScrollCpsValue);
    if (cps < 1.0f) cps = 1.0f;
    if (cps > 30.0f) cps = 30.0f;
    float speedPxPerMs = (avgPx * cps) / 1000.0f;
    if (speedPxPerMs < 0.02f) speedPxPerMs = 0.02f;
    float phaseMs = static_cast<float>(HAL::millis()) + idx * 200.0f;
    float offset = std::fmod(phaseMs * speedPxPerMs, cyclePx);
    float baseX = x - offset;
    int clipX0 = x;
    int clipY0 = y - fontH;
    int clipX1 = x + maxW;
    int clipY1 = y + 2;
    HAL::setClipWindow(clipX0, clipY0, clipX1, clipY1);
    HAL::drawChinese(baseX, y, text);
    HAL::drawChinese(baseX + cyclePx, y, text);
    HAL::clearClipWindow();
  };

  std::string title = nowPlaying.title[0] ? nowPlaying.title : std::string("Unknown");
  std::string artist = nowPlaying.artist[0] ? nowPlaying.artist : std::string("");
  drawScroll(title, textX, line1Y, textMaxW, 0);
  if (!artist.empty()) {
    drawScroll(artist, textX, line2Y, textMaxW, 1);
  }

  if (currentLyric.active) {
    std::string lyric = currentLyric.text;
    drawScroll(lyric, textX, line3Y, textMaxW, 2);
  }

  int barY = panelY + panelH - padding - 2;
  int barX = textX;
  int barW = panelX + panelW - padding - barX;
  if (barW > 4) {
    float ratio = 0.0f;
    if (nowPlaying.durMs > 0) ratio = std::min(1.0f, std::max(0.0f, (float)nowPlaying.posMs / (float)nowPlaying.durMs));
    if (nowPlaying.durMs > 0) {
      int remainMs = std::max<int32_t>(0, nowPlaying.durMs - nowPlaying.posMs);
      int totalSec = remainMs / 1000;
      int min = totalSec / 60;
      int sec = totalSec % 60;
      char remainText[16];
      snprintf(remainText, sizeof(remainText), "-%d:%02d", min, sec);
      std::string remainStr = remainText;
      // int remainW = HAL::getFontWidth(remainStr);  // Currently unused
      int remainY = coverY + cover + HAL::getFontHeight();
      int maxY = barY - 2;
      if (remainY > maxY) remainY = maxY;
      if (remainY > coverY + HAL::getFontHeight()) {
        HAL::drawEnglish(coverX, remainY, remainStr);
      }
    }
    if (barW <= 4) {
      hal.clearBarOverlay();
      return;
    }
    HALAstraESP32::BarOverlay bar {};
    bar.enabled = true;
    bar.x = barX;
    bar.y = barY;
    bar.w = barW;
    bar.h = 2;
    bar.progress = ratio;
    bar.fg = hal.getForegroundColor();
    bar.bg = dim565(bar.fg, 0.25f);
    hal.setBarOverlay(bar);
  } else {
    hal.clearBarOverlay();
  }
}

void saveSettings() {
  nvs_handle_t handle;
  if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) {
    return;
  }
  nvs_set_u8(handle, "ui_speed", uiSpeedValue);
  nvs_set_u8(handle, "sel_speed", selSpeedValue);
  nvs_set_u8(handle, "wrap_pause", wrapPauseValue);
  nvs_set_u8(handle, "font_hue", fontColorValue);
  nvs_set_u8(handle, "scroll_ms", scrollTimeValue);
  nvs_set_u8(handle, "lyric_cps", lyricScrollCpsValue);
  nvs_set_u16(handle, "np_close_ms", mapNpCloseMs(npAutoCloseSecValue));
  nvs_set_u16(handle, "cfg_close", cfgMsgAutoCloseMs);
  nvs_set_u8(handle, "comm_mode", static_cast<uint8_t>(commMode));
  nvs_commit(handle);
  nvs_close(handle);
}

void resetSettings() {
  nvs_handle_t handle;
  if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) {
    return;
  }
  nvs_erase_all(handle);
  nvs_commit(handle);
  nvs_close(handle);
}

bool checkSafeReset() {
  const uint32_t startMs = static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
  const uint32_t gateMs = 500;
  const uint32_t holdMs = 5000;
  bool held = true;

  while (static_cast<uint32_t>(esp_timer_get_time() / 1000ULL) - startMs < gateMs) {
    if (!HAL::getKey(key::KEY_0)) {
      held = false;
      break;
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }

  if (!held) return false;

  const uint32_t holdStart = static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
  while (static_cast<uint32_t>(esp_timer_get_time() / 1000ULL) - holdStart < holdMs) {
    if (!HAL::getKey(key::KEY_0)) {
      return false;
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  resetSettings();
  return true;
}

void scaleUi(float factor) {
  if (factor < 0.1f) factor = 0.1f;
  if (factor > 20.0f) factor = 20.0f;
  auto &cfg = astra::getUIConfig();
  auto scale = [&](float &v) { v = std::max(1.0f, v * factor); };

  scale(cfg.listBarWeight);
  scale(cfg.listTextHeight);
  scale(cfg.listTextMargin);
  scale(cfg.listLineHeight);
  scale(cfg.selectorRadius);
  scale(cfg.selectorMargin);
  scale(cfg.selectorTopMargin);

  scale(cfg.tilePicWidth);
  scale(cfg.tilePicHeight);
  scale(cfg.tilePicMargin);
  scale(cfg.tilePicTopMargin);
  scale(cfg.tileArrowWidth);
  scale(cfg.tileArrowMargin);
  scale(cfg.tileDottedLineBottomMargin);
  scale(cfg.tileArrowBottomMargin);
  scale(cfg.tileTextBottomMargin);
  scale(cfg.tileBarHeight);
  scale(cfg.tileSelectBoxLineLength);
  scale(cfg.tileSelectBoxMargin);
  cfg.tileSelectBoxWidth = cfg.tileSelectBoxMargin * 2 + cfg.tilePicWidth;
  cfg.tileSelectBoxHeight = cfg.tileSelectBoxMargin * 2 + cfg.tilePicHeight;
  scale(cfg.tileTitleHeight);
  scale(cfg.tileBtnMargin);

  scale(cfg.popMargin);
  scale(cfg.popRadius);

  scale(cfg.logoStarLength);
  scale(cfg.logoTextHeight);
  scale(cfg.logoCopyRightHeight);

  scale(cfg.checkBoxWidth);
  scale(cfg.checkBoxHeight);
  scale(cfg.checkBoxTopMargin);
  scale(cfg.checkBoxRightMargin);
  scale(cfg.checkBoxRadius);
}

void renderAdjustScreen() {
  HAL::canvasClear();
  auto &sys = HAL::getSystemConfig();
  auto &ui = astra::getUIConfig();
  float cx = sys.screenWeight * 0.5f;
  float cy = sys.screenHeight * 0.55f;
  float r = std::min(sys.screenWeight, sys.screenHeight) * 0.35f;

  static float shownValue = 0.0f;
  static AdjustTarget lastTarget = ADJ_NONE;
  static float enterProgress = 1.0f;

  float targetValue = 0.0f;
  float minValue = 0.0f;
  float maxValue = 1.0f;

  std::string label;
  std::string valueText;
  std::string minText;
  std::string maxText;
  if (adjustTarget == ADJ_VOLUME) {
    targetValue = static_cast<float>(volumeValue);
    minValue = 0.0f;
    maxValue = 100.0f;
    label = "VOLUME";
    valueText = std::to_string(static_cast<int>(std::round(targetValue))) + "%";
    minText = "0%";
    maxText = "100%";
  } else if (adjustTarget == ADJ_SPI) {
    targetValue = static_cast<float>(spiPending);
    minValue = 1.0f;
    maxValue = 80.0f;
    label = "SPI MHz";
    valueText = std::to_string(static_cast<int>(std::round(targetValue))) + " MHz";
    minText = "1 MHz";
    maxText = "80 MHz";
  } else if (adjustTarget == ADJ_UI_SPEED) {
    float mappedValue = mapUiSpeedScale(uiSpeedPending);
    targetValue = mappedValue;
    minValue = mapUiSpeedScale(1);
    maxValue = mapUiSpeedScale(50);
    label = "UI SPEED";
    valueText = std::to_string(static_cast<int>(std::round(mappedValue))) + "x";
    minText = "1x";
    maxText = "50x";
  } else if (adjustTarget == ADJ_SEL_SPEED) {
    float mappedValue = mapSelSpeedMs(selSpeedPending);
    targetValue = mappedValue;
    minValue = mapSelSpeedMs(1);
    maxValue = mapSelSpeedMs(50);
    label = "SEL SPEED";
    valueText = std::to_string(static_cast<int>(std::round(mappedValue))) + " ms";
    minText = "900 ms";
    maxText = "20 ms";
  } else if (adjustTarget == ADJ_WRAP_PAUSE) {
    float mappedValue = mapWrapPauseMs(wrapPausePending);
    targetValue = mappedValue;
    minValue = mapWrapPauseMs(0);
    maxValue = mapWrapPauseMs(50);
    label = "WRAP PAUSE";
    valueText = std::to_string(static_cast<int>(std::round(mappedValue))) + " ms";
    minText = "0 ms";
    maxText = "1000 ms";
  } else if (adjustTarget == ADJ_FONT_COLOR) {
    float mappedValue = mapHueDeg(fontColorPending);
    targetValue = mappedValue;
    minValue = mapHueDeg(0);
    maxValue = mapHueDeg(50);
    label = "FONT COLOR";
    valueText = std::to_string(static_cast<int>(std::round(mappedValue))) + " deg";
    minText = "0 deg";
    maxText = "360 deg";
  } else if (adjustTarget == ADJ_SCROLL_TIME) {
    float mappedValue = mapScrollMs(scrollTimePending);
    targetValue = mappedValue;
    minValue = mapScrollMs(1);
    maxValue = mapScrollMs(50);
    label = "SCROLL TIME";
    valueText = std::to_string(static_cast<int>(std::round(mappedValue))) + " ms";
    minText = "100 ms";
    maxText = "15000 ms";
  } else if (adjustTarget == ADJ_LYRIC_SCROLL_CPS) {
    targetValue = static_cast<float>(lyricScrollCpsPending);
    minValue = 1.0f;
    maxValue = 30.0f;
    label = "LYRIC SPEED";
    valueText = std::to_string(static_cast<int>(std::round(targetValue))) + " cps";
    minText = "1 cps";
    maxText = "30 cps";
  } else if (adjustTarget == ADJ_NP_CLOSE_SEC) {
    targetValue = static_cast<float>(npAutoCloseSecPending);
    minValue = 0.0f;
    maxValue = 60.0f;
    label = "MUSIC CLOSE";
    if (npAutoCloseSecPending == 0) {
      valueText = "0 ms";
    } else if (npAutoCloseSecPending >= 60) {
      valueText = "NEVER";
    } else {
      valueText = std::to_string(mapNpCloseMs(npAutoCloseSecPending)) + " ms";
    }
    minText = "0 ms";
    maxText = "NEVER";
  } else if (adjustTarget == ADJ_CFG_CLOSE) {
    targetValue = static_cast<float>(cfgMsgAutoClosePending);
    minValue = 0.0f;
    maxValue = 60.0f;
    label = "AUTO CLOSE MSG";
    if (cfgMsgAutoClosePending == 0) {
      valueText = "0 ms";
    } else if (cfgMsgAutoClosePending >= 60) {
      valueText = "NEVER";
    } else {
      valueText = std::to_string(mapCfgCloseMs(cfgMsgAutoClosePending)) + " ms";
    }
    minText = "0 ms";
    maxText = "NEVER";
  }

  if (adjustTarget != lastTarget) {
    shownValue = targetValue;
    lastTarget = adjustTarget;
    enterProgress = 0.0f;
  }
  Animation::move(&shownValue, targetValue, ui.listAnimationSpeed);
  Animation::move(&enterProgress, 1.0f, ui.listAnimationSpeed);
  float progress = 0.0f;
  if (maxValue > minValue) {
    progress = (shownValue - minValue) / (maxValue - minValue);
  } else if (maxValue < minValue) {
    progress = (minValue - shownValue) / (minValue - maxValue);
  }
  if (progress < 0.0f) progress = 0.0f;
  if (progress > 1.0f) progress = 1.0f;
  if (adjustTarget == ADJ_VOLUME) {
    valueText = std::to_string(static_cast<int>(std::round(shownValue))) + "%";
  } else if (adjustTarget == ADJ_SPI) {
    valueText = std::to_string(static_cast<int>(std::round(shownValue))) + " MHz";
  } else if (adjustTarget == ADJ_UI_SPEED) {
    valueText = std::to_string(static_cast<int>(std::round(shownValue))) + "x";
  } else if (adjustTarget == ADJ_SEL_SPEED) {
    valueText = std::to_string(static_cast<int>(std::round(shownValue))) + " ms";
  } else if (adjustTarget == ADJ_WRAP_PAUSE) {
    valueText = std::to_string(static_cast<int>(std::round(shownValue))) + " ms";
  } else if (adjustTarget == ADJ_FONT_COLOR) {
    valueText = std::to_string(static_cast<int>(std::round(shownValue))) + " deg";
  } else if (adjustTarget == ADJ_SCROLL_TIME) {
    valueText = std::to_string(static_cast<int>(std::round(shownValue))) + " ms";
  } else if (adjustTarget == ADJ_LYRIC_SCROLL_CPS) {
    valueText = std::to_string(static_cast<int>(std::round(shownValue))) + " cps";
  } else if (adjustTarget == ADJ_NP_CLOSE_SEC) {
    int level = static_cast<int>(std::round(shownValue));
    if (level <= 0) {
      valueText = "0 ms";
    } else if (level >= 60) {
      valueText = "NEVER";
    } else {
      valueText = std::to_string(mapNpCloseMs(static_cast<uint8_t>(level))) + " ms";
    }
  } else if (adjustTarget == ADJ_CFG_CLOSE) {
    int level = static_cast<int>(std::round(shownValue));
    if (level <= 0) {
      valueText = "0 ms";
    } else if (level >= 60) {
      valueText = "NEVER";
    } else {
      valueText = std::to_string(mapCfgCloseMs(static_cast<uint8_t>(level))) + " ms";
    }
  }

  float enterOffset = (1.0f - enterProgress) * 18.0f;
  HAL::setDrawType(1);
  HALAstraESP32::ArcOverlay overlay {};
  overlay.enabled = true;
  overlay.cx = cx;
  overlay.cy = cy;
  overlay.r = r;
  overlay.start_deg = 225.0f;
  overlay.sweep_deg = 270.0f;
  overlay.progress = progress;
  overlay.thickness = 2;
  overlay.aa_samples = 1;
  hal.setArcOverlay(overlay);

  int labelW = HAL::getFontWidth(label);
  HAL::drawEnglish(cx - labelW * 0.5f, cy - r - 2 + enterOffset, label);
  int valW = HAL::getFontWidth(valueText);
  HAL::drawEnglish(cx - valW * 0.5f, cy + HAL::getFontHeight() + enterOffset, valueText);

  float startDeg = 225.0f;
  float endDeg = 225.0f + 270.0f;
  auto drawTickLabel = [&](float deg, std::string text) {
    float rad = deg * 3.1415926f / 180.0f;
    float tx = cx + std::cos(rad) * (r + 4.0f);
    float ty = cy + std::sin(rad) * (r + 4.0f);
    int tw = HAL::getFontWidth(text);
    HAL::drawEnglish(tx - tw * 0.5f, ty + HAL::getFontHeight() * 0.5f + enterOffset, text);
  };
  drawTickLabel(startDeg, minText);
  drawTickLabel(endDeg, maxText);
  std::string help1;
  std::string help2;
  if (adjustTarget == ADJ_UI_SPEED) {
    help1 = "\u754c\u9762\u6574\u4f53\u52a8\u753b\u901f\u5ea6";
    help2 = "\u6570\u503c\u8d8a\u5927\u8d8a\u5feb";
  } else if (adjustTarget == ADJ_SEL_SPEED) {
    help1 = "\u9009\u62e9\u6846\u8ddf\u968f\u901f\u5ea6";
    help2 = "\u6570\u503c\u8d8a\u5927\u8d8a\u5feb";
  } else if (adjustTarget == ADJ_WRAP_PAUSE) {
    help1 = "\u5faa\u73af\u8df3\u8f6c\u505c\u987f";
    help2 = "\u9632\u6b62\u5feb\u901f\u8bef\u8f6c";
  } else if (adjustTarget == ADJ_FONT_COLOR) {
    help1 = "\u8bbe\u7f6e\u5168\u5c40\u5b57\u4f53\u989c\u8272";
    help2 = "\u5f71\u54cd\u6240\u6709\u6587\u5b57";
  } else if (adjustTarget == ADJ_SCROLL_TIME) {
    help1 = "\u83dc\u5355\u957f\u6587\u672c\u6eda\u52a8\u5468\u671f";
    help2 = "\u6570\u503c\u8d8a\u5927\u8d8a\u6162";
  } else if (adjustTarget == ADJ_LYRIC_SCROLL_CPS) {
    help1 = "\u6b4c\u8bcd\u6eda\u52a8\u901f\u5ea6";
    help2 = "\u5355\u4f4d: \u5b57/\u79d2";
  } else if (adjustTarget == ADJ_NP_CLOSE_SEC) {
    help1 = "\u97f3\u4e50\u6d6e\u7a97\u8d85\u65f6\u5173\u95ed";
    help2 = "0ms=\u7acb\u5373  NEVER=\u4e0d\u81ea\u52a8\u5173\u95ed";
  } else if (adjustTarget == ADJ_CFG_CLOSE) {
    help1 = "\u914d\u7f6e\u5bfc\u5165\u4fe1\u606f\u81ea\u52a8\u5173\u95ed";
    help2 = "0ms=\u7acb\u5373  NEVER=\u4e0d\u81ea\u52a8\u5173\u95ed";
  }

  if (!help1.empty()) {
    int fontH = HAL::getFontHeight();
    int baseY = static_cast<int>(cy);
    HAL::drawChinese(6, baseY + enterOffset, help1);
    if (!help2.empty()) {
      HAL::drawChinese(6, baseY + fontH + 2 + enterOffset, help2);
    }
  }
}

void renderPerfOverlay(int fpsValue, int cpuValue) {
  if (!showFps && !showCpu && !showUp && !showDown) return;
  std::string label;
  if (showFps) {
    label += "FPS " + std::to_string(fpsValue);
  }
  if (showCpu) {
    if (!label.empty()) label += " ";
    label += "CPU " + std::to_string(cpuValue);
  }
  if (showUp) {
    if (!label.empty()) label += " ";
    int kbps = upBps / 1024;
    label += "UP " + std::to_string(kbps) + "K";
  }
  if (showDown) {
    if (!label.empty()) label += " ";
    int kbps = downBps / 1024;
    label += "DN " + std::to_string(kbps) + "K";
  }
  if (!label.empty()) {
    HAL::setDrawType(1);
    int textW = HAL::getFontWidth(label);
    int x = HAL::getSystemConfig().screenWeight - textW - 2;
    if (x < 0) x = 0;
    HAL::drawEnglish(static_cast<float>(x), HAL::getFontHeight(), label);
  }
}

void renderLinkIndicator(uint32_t nowMs) {
  if (handshakeOk) return;
  if ((nowMs / 700) % 2 == 0) return;
  HAL::setDrawType(1);
  auto &sys = HAL::getSystemConfig();
  int x = sys.screenWeight - 4;
  int y = sys.screenHeight - 4;
  if (x < 0 || y < 0) return;
  HAL::drawBox(x, y, 3, 3);
}

void renderPopupBackground() {
  if (nowPlaying.active) {
    renderNowPlayingOverlay();
  } else {
    hal.clearImageOverlay();
    hal.clearBarOverlay();
  }
}

void renderStatusBar(uint32_t nowMs, int fpsValue, int cpuValue) {
  std::string text;
  bool usbReady = isUsbLinkReady();
  bool bleClientReady = isBleClientLinkReady();
  bool bleBasicReady = isBleBasicLinkReady();
  bool linkReady = usbReady || bleClientReady;

  if (usbReady && bleClientReady) text = "U+BC";
  else if (usbReady && bleBasicReady) text = "U+BB";
  else if (usbReady) text = "USB";
  else if (bleClientReady) text = "BLEC";
  else if (bleBasicReady) text = "BLEB";
  else text = "OFF";

  std::string perf;
  if (showFps) perf += "F:" + std::to_string(fpsValue);
  if (showCpu) {
    if (!perf.empty()) perf += " ";
    perf += "C:" + std::to_string(cpuValue);
  }
  if (showUp) {
    if (!perf.empty()) perf += " ";
    perf += "U:" + std::to_string(upBps / 1024) + "K";
  }
  if (showDown) {
    if (!perf.empty()) perf += " ";
    perf += "D:" + std::to_string(downBps / 1024) + "K";
  }
  if (!perf.empty()) {
    text += "  ";
    text += perf;
  }

  bool blink = (!linkReady) && (((nowMs / 700) % 2) != 0);
  hal.setStatusBar(text, linkReady, blink);
}

void renderTestPattern() {
  HAL::canvasClear();
  auto &sys = HAL::getSystemConfig();
  int w = sys.screenWeight;
  int h = sys.screenHeight;
  if (w <= 0 || h <= 0) return;
  HAL::setDrawType(1);
  
  static const uint8_t BAYER4[4][4] = {
      {0, 8, 2, 10},
      {12, 4, 14, 6},
      {3, 11, 1, 9},
      {15, 7, 13, 5}};
  
  static uint32_t waveTime = 0;
  waveTime++;
  
  // Draw full screen with improved grayscale noise
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      // Multiple independent hash streams with different weights
      uint32_t h1 = (x * 73 + y * 97);
      h1 = h1 ^ (waveTime * (h1 >> 16));
      h1 = (h1 * 1664525U + 1013904223U);
      
      uint32_t h2 = (x * 131 + y * 181) ^ ((waveTime >> 2) * 7919);
      h2 = (h2 * 1103515245U + 12345U);
      
      uint32_t h3 = ((x ^ (y * 3)) * 37 + waveTime * 11);
      h3 = (h3 * 1103515245U + 12345U);
      
      uint32_t h4 = ((x * 19) ^ (y * 23)) ^ ((waveTime * 17) >> 1);
      h4 = (h4 * 1664525U + 1013904223U);
      
      // Small region-based motion to break up grid
      int smallRegionX = x >> 3;
      int smallRegionY = y >> 3;
      uint32_t regionDrift = ((smallRegionX * 7) ^ (smallRegionY * 11)) * waveTime;
      
      // Combine all sources for full 16-level grayscale
      uint32_t combined = 0;
      combined += (h1 & 0x0F);
      combined += ((h2 >> 4) & 0x0F);
      combined += ((h3 >> 8) & 0x0F);
      combined += ((h4 >> 12) & 0x0F);
      combined += ((regionDrift >> 2) & 0x0F);
      
      // Average to get grayscale in 0-15 range
      int level = ((combined >> 2) + (combined >> 3)) & 0x0F;
      
      // Additional per-pixel dithering
      int ditherThreshold = BAYER4[y & 3][x & 3];
      if ((level << 1) > ditherThreshold) {
        HAL::drawPixel(static_cast<float>(x), static_cast<float>(y));
      }
    }
  }
  
  HAL::drawEnglish(2, HAL::getFontHeight(), "4BIT WAVE");
}

// Scroll state for About menu
int aboutScrollOffset = 0;
int aboutMaxScroll = 0;
int aboutScrollTarget = 0;
float aboutScrollPos = 0.0f;

void renderAboutInfo() {
  HAL::canvasClear();
  auto &sys = HAL::getSystemConfig();
  auto &astraConfig = getUIConfig();
  int w = sys.screenWeight;
  int h = sys.screenHeight;
  if (w <= 0 || h <= 0) return;
  
  HAL::setDrawType(1); // Use anti-aliased rendering like adjust screen
  
  aboutScrollOffset = static_cast<int>(std::round(aboutScrollPos));
  int lineHeight = HAL::getFontHeight() + 1;
  int startY = 4 - aboutScrollOffset;
  int y = startY;
  int contentX = 2;
  float scrollBarW = astraConfig.listBarWeight;
  if (scrollBarW < 1.0f) scrollBarW = 1.0f;
  float scrollBarX = static_cast<float>(w) - scrollBarW;
  int footerHeight = lineHeight + 4;
  int viewHeight = h - footerHeight;

  auto drawLine = [&](const std::string &text, int extraGap = 0) {
    if (y >= -lineHeight && y < h) {
      HAL::drawEnglish(contentX, y, text);
    }
    y += lineHeight + extraGap;
  };

  auto chipModelName = [](esp_chip_model_t model) -> const char * {
    switch (model) {
      case CHIP_ESP32: return "ESP32";
      case CHIP_ESP32S2: return "ESP32-S2";
      case CHIP_ESP32S3: return "ESP32-S3";
      case CHIP_ESP32C3: return "ESP32-C3";
      case CHIP_ESP32C2: return "ESP32-C2";
      case CHIP_ESP32C6: return "ESP32-C6";
      case CHIP_ESP32H2: return "ESP32-H2";
      default: return "ESP32";
    }
  };
  
  // Title
  drawLine("SongLed", 2);

  // Version
  char buf[96];
  snprintf(buf, sizeof(buf), "Ver: %s", FIRMWARE_VERSION);
  drawLine(buf);

  // Build date/time
  snprintf(buf, sizeof(buf), "Built: %s %s", BUILD_DATE, BUILD_TIME);
  drawLine(buf);

  // Connection status
  bool usbReady = isUsbLinkReady();
  bool bleBasicReady = isBleBasicLinkReady();
  bool bleClientReady = isBleClientLinkReady();
  CommMode activeMode = pickCommandMode();
  if (usbReady && bleClientReady) {
    drawLine("Conn: Wired + BLE Client");
  } else if (usbReady && bleBasicReady) {
    drawLine("Conn: Wired + BLE Basic");
  } else if (usbReady) {
    drawLine("Conn: Wired");
  } else if (bleClientReady) {
    drawLine("Conn: BLE Client");
  } else if (bleBasicReady) {
    drawLine("Conn: BLE Basic");
  } else {
    drawLine("Conn: None");
  }
  y += 3;

  // System info
  drawLine("System:");
  esp_chip_info_t chipInfo;
  esp_chip_info(&chipInfo);
  snprintf(buf, sizeof(buf), "Chip: %s r%d", chipModelName(chipInfo.model), chipInfo.revision);
  drawLine(buf);
  snprintf(buf, sizeof(buf), "Cores: %d", chipInfo.cores);
  drawLine(buf);
  const char *wifiFlag = (chipInfo.features & CHIP_FEATURE_WIFI_BGN) ? "WiFi" : "-";
  const char *btFlag = (chipInfo.features & CHIP_FEATURE_BT) ? "BT" : "-";
  const char *bleFlag = (chipInfo.features & CHIP_FEATURE_BLE) ? "BLE" : "-";
  snprintf(buf, sizeof(buf), "Feat: %s/%s/%s", wifiFlag, btFlag, bleFlag);
  drawLine(buf);
  snprintf(buf, sizeof(buf), "IDF: %s", esp_get_idf_version());
  drawLine(buf);
  snprintf(buf, sizeof(buf), "Display: %dx%d", w, h);
  drawLine(buf);

  uint32_t flashSize = 0;
  if (esp_flash_get_size(nullptr, &flashSize) == ESP_OK) {
    snprintf(buf, sizeof(buf), "Flash: %uMB", static_cast<unsigned>(flashSize / (1024 * 1024)));
  } else {
    snprintf(buf, sizeof(buf), "Flash: N/A");
  }
  drawLine(buf);

  uint32_t freeHeap = esp_get_free_heap_size();
  uint32_t minHeap = heap_caps_get_minimum_free_size(MALLOC_CAP_DEFAULT);
  snprintf(buf, sizeof(buf), "Heap: %u KB", static_cast<unsigned>(freeHeap / 1024));
  drawLine(buf);
  snprintf(buf, sizeof(buf), "Heap Min: %u KB", static_cast<unsigned>(minHeap / 1024));
  drawLine(buf);
  uint32_t psramFree = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  snprintf(buf, sizeof(buf), "PSRAM: %u KB", static_cast<unsigned>(psramFree / 1024));
  drawLine(buf);
  const char *modeName = commModeLabel(commMode);
  snprintf(buf, sizeof(buf), "Prefer: %s", modeName);
  drawLine(buf);
  snprintf(buf, sizeof(buf), "Active: %s", activeMode == COMM_AUTO ? "N/A" : commModeLabel(activeMode));
  drawLine(buf);
  snprintf(buf, sizeof(buf), "BLE Ena: %s", bleEnabled ? "Yes" : "No");
  drawLine(buf);
  snprintf(buf, sizeof(buf), "BLE Basic: %s", bleBasicReady ? "Yes" : "No");
  drawLine(buf);
  snprintf(buf, sizeof(buf), "BLE Client: %s", bleClientReady ? "Yes" : "No");
  drawLine(buf);
  snprintf(buf, sizeof(buf), "USB Link: %s", usbReady ? "OK" : "None");
  drawLine(buf, 2);
  
  // Divider line
  if (y >= 0 && y < h) {
    for (int x = contentX; x < w - 6; x++) {
      HAL::drawPixel(static_cast<float>(x), static_cast<float>(y));
    }
  }
  y += 4;
  
  // Changelog title
  drawLine("Changelog:");
  
  // Display all changelog entries
  for (int i = 0; CHANGELOG[i] != nullptr; i++) {
    drawLine(CHANGELOG[i]);
  }
  
  y += 4;
  
  // Calculate max scroll
  int totalContentHeight = y - startY;
  aboutMaxScroll = totalContentHeight - viewHeight;
  if (aboutMaxScroll < 0) aboutMaxScroll = 0;
  if (aboutScrollTarget > aboutMaxScroll) aboutScrollTarget = aboutMaxScroll;

  float target = static_cast<float>(aboutScrollTarget);
  float delta = target - aboutScrollPos;
  aboutScrollPos += delta * 0.45f;
  if (std::fabs(delta) < 0.5f) aboutScrollPos = target;
  if (aboutScrollPos < 0.0f) aboutScrollPos = 0.0f;
  if (aboutScrollPos > aboutMaxScroll) aboutScrollPos = static_cast<float>(aboutMaxScroll);
  aboutScrollOffset = static_cast<int>(std::round(aboutScrollPos));
  
  // Draw scroll bar (similar to menu bar)
  if (aboutMaxScroll > 0 && totalContentHeight > 0) {
    float barX = scrollBarX;
    float barW = scrollBarW;
    int barAreaH = viewHeight;
    if (barAreaH < 1) barAreaH = h;

    // Calculate bar height based on scroll progress (menu-style)
    float scrollProgress = static_cast<float>(aboutScrollOffset + viewHeight) / static_cast<float>(totalContentHeight);
    int barHeight = static_cast<int>(scrollProgress * barAreaH);
    if (barHeight < 8) barHeight = 8;
    if (barHeight > barAreaH) barHeight = barAreaH;

    // Draw indicator lines (same as menu)
    HAL::drawHLine(scrollBarX, 0, scrollBarW);
    HAL::drawHLine(scrollBarX, barAreaH - 1, scrollBarW);
    HAL::drawVLine(static_cast<float>(w) - std::ceil(scrollBarW / 2.0f), 0, barAreaH);

    // Draw bar
    HAL::drawBox(barX, 0, barW, barHeight);
  }
  
  // Footer
  HAL::setDrawType(1);
  int footerY = h - lineHeight - 2;
  HAL::setDrawType(0);
  HAL::drawBox(0, footerY - 2, w - 6, lineHeight + 4);
  HAL::setDrawType(1);
  HAL::drawEnglish(contentX, footerY, "K0:Up K1:Down K2:Exit");
}

extern "C" void app_main(void) {
  HAL::inject(&hal);

  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    nvs_flash_erase();
    nvs_flash_init();
  }

  if (checkSafeReset()) {
    uiSpeedValue = 13;
    selSpeedValue = 25;
    wrapPauseValue = 15;
    fontColorValue = 17;
    scrollTimeValue = 30;
    lyricScrollCpsValue = 8;
    npAutoCloseSecValue = 8;
    cfgMsgAutoCloseMs = 5000;
    commMode = COMM_AUTO;
  }

  loadSettings();
  cfgMsgAutoCloseValue = mapCfgCloseFromMs(cfgMsgAutoCloseMs);
  cfgMsgAutoClosePending = cfgMsgAutoCloseValue;
  applySelectorSpeed();
  applyWrapPause();
  applyScrollDuration();
  applyLyricScrollSpeed();
  applyNowPlayingAutoClose();
  applyFontColor();

  if (SIMPLE_DISPLAY_ONLY) {
    while (true) {
      hal.lcdFill(0xF800);
      vTaskDelay(pdMS_TO_TICKS(500));
      hal.lcdFill(0x07E0);
      vTaskDelay(pdMS_TO_TICKS(500));
      hal.lcdFill(0x001F);
      vTaskDelay(pdMS_TO_TICKS(500));
    }
  }

  init_uart();
  uart_flush(UART_NUM_0); // 清空UART缓冲区
  lastRxMs = static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
  lastHelloMs = lastRxMs;
  
  // Initialize BLE service
  ESP_LOGI("MAIN", "Initializing BLE service...");
  ble_set_debug_callback(bleDebugForward);
  if (ble_service_init("SongLed")) {
    bleEnabled = true;
    ESP_LOGI("MAIN", "BLE service initialized successfully");
    ble_set_connection_callback([](bool connected) {
      if (connected) {
        ESP_LOGI("MAIN", "BLE client connected");
        handshakeOk = false;
        syncedAfterHandshake = false;
        sendHello();
      } else {
        ESP_LOGI("MAIN", "BLE client disconnected");
        handshakeOk = false;
      }
    });
  } else {
    ESP_LOGI("MAIN", "BLE initialization failed, using USB only");
    commMode = COMM_USB;
  }
  
  if (liveHeartbeat) {
    sendLine("APP START");
  }

  boostUiSpeed(static_cast<float>(uiSpeedValue));
  scaleUi(2.0f);
  buildMenus();
  launcher.init(menuMain);
  launcher.setPopRenderHook(renderPopupBackground);

  sendVolumeGet();

  uint32_t lastPerfMs = 0;
  uint32_t lastLiveMs = 0;
  uint32_t frameCount = 0;
  int fpsValue = 0;
  int cpuValue = 0;
  uint64_t busyUsAccum = 0;
  uint64_t totalUsAccum = 0;
  uint64_t lastLoopUs = 0;
  uint32_t lastBtUiUpdateMs = 0;
  uint32_t lastLockUiUpdateMs = 0;

  while (true) {
    uint64_t loopStartUs = esp_timer_get_time();
    if (lastLoopUs == 0) lastLoopUs = loopStartUs;
    totalUsAccum += (loopStartUs - lastLoopUs);
    lastLoopUs = loopStartUs;

    HAL::keyScan();
    handleKeyEvents();
    readSerial();

    uint32_t nowMs = static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
    if (nowMs - lastLockUiUpdateMs >= 120) {
      lastLockUiUpdateMs = nowMs;
      updateControlMenuAvailability();
    }
    if (nowMs - lastBtUiUpdateMs >= 500) {
      lastBtUiUpdateMs = nowMs;
      updateBluetoothMenuItems();
    }
    
    // 心跳超时检测 - 10秒未收到任何消息则重置连接
    if (handshakeOk && lastRxMs > 0 && (nowMs - lastRxMs > HANDSHAKE_TIMEOUT_MS)) {
      ESP_LOGI("SERIAL", "Connection timeout, resetting");
      handshakeOk = false;
      syncedAfterHandshake = false;
      lastRxMs = nowMs;
    }
    
    if (!handshakeOk && (nowMs - lastHelloMs >= HELLO_INTERVAL_MS)) {
      lastHelloMs = nowMs;
      sendHello();
    }

    uint16_t npCloseMs = mapNpCloseMs(npAutoCloseSecValue);
    if (npCloseMs < 65535) {
      uint32_t npTimeoutMs = static_cast<uint32_t>(npCloseMs);
      if (nowPlaying.active && nowPlaying.lastUpdateMs > 0 &&
          (nowMs - nowPlaying.lastUpdateMs > npTimeoutMs)) {
        clearNowPlayingState(true);
      }
      if (!nowPlaying.active && currentLyric.active && lyricLastUpdateMs > 0 &&
          (nowMs - lyricLastUpdateMs > npTimeoutMs)) {
        currentLyric.active = false;
        nextLyric.active = false;
        lyricScrollOffset = 0;
        lyricLastUpdateMs = 0;
      }
    }

    if (adjustActive) {
    hal.clearImageOverlay();
    hal.clearBarOverlay();
    if (appMode == MODE_TEST_PATTERN) {
      renderTestPattern();
    } else if (appMode == MODE_ABOUT_INFO) {
      renderAboutInfo();
    } else {
      renderAdjustScreen();
    }
    renderStatusBar(nowMs, fpsValue, cpuValue);
    HAL::canvasUpdate();
  } else {
      hal.clearArcOverlay();
      launcher.update();
      if (nowPlaying.active) {
        renderNowPlayingOverlay();
      } else {
        hal.clearImageOverlay();
        hal.clearBarOverlay();
        renderLyrics(); // Display lyrics
      }
      renderStatusBar(nowMs, fpsValue, cpuValue);
      HAL::canvasUpdate();
    }

    frameCount++;
    uint64_t loopEndUs = esp_timer_get_time();
    busyUsAccum += (loopEndUs - loopStartUs);

    if (liveHeartbeat && (nowMs - lastLiveMs >= 1000)) {
      lastLiveMs = nowMs;
      sendLine("APP LIVE");
    }
    if (lastPerfMs == 0) lastPerfMs = nowMs;
    if (nowMs - lastPerfMs >= 1000) {
      uint32_t dt = nowMs - lastPerfMs;
      if (dt > 0) fpsValue = static_cast<int>(frameCount * 1000 / dt);
      if (totalUsAccum > 0) {
        cpuValue = static_cast<int>((busyUsAccum * 100) / totalUsAccum);
      }
      if (dt > 0) {
        upBps = static_cast<int>((txBytes * 1000ULL) / dt);
        downBps = static_cast<int>((rxBytes * 1000ULL) / dt);
      } else {
        upBps = 0;
        downBps = 0;
      }
      txBytes = 0;
      rxBytes = 0;
      frameCount = 0;
      busyUsAccum = 0;
      totalUsAccum = 0;
      lastPerfMs = nowMs;
    }

    vTaskDelay(pdMS_TO_TICKS(2));
  }
}

}  // namespace
