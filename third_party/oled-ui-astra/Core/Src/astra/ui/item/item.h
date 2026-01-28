//
// Created by Fir on 2024/4/14 014.
//
#pragma once
#ifndef ASTRA_CORE_SRC_ASTRA_UI_ITEM_ITEM_H_
#define ASTRA_CORE_SRC_ASTRA_UI_ITEM_ITEM_H_

#include <cmath>
#include "../../../hal/hal.h"
#include "../../../astra/config/config.h"

namespace astra {

extern float g_anim_dt;
extern uint32_t g_anim_last_ms;
extern float g_anim_speed_scale;

class Item {
protected:
  sys::config systemConfig;
  config astraConfig;

  inline void updateConfig() {
    this->systemConfig = HAL::getSystemConfig();
    this->astraConfig = getUIConfig();
  }
};

class Animation {
public:
  static void entry();
  static void exit();
  static void blur();
  static void tick();
  static void move(float *_pos, float _posTrg, float _speed);
};

inline void Animation::entry() { }

//todo 未实现功能
inline void Animation::exit() {
  static unsigned char fadeFlag = 1;
  static size_t bufferLen = 8 * static_cast<size_t>(HAL::getBufferTileHeight()) *
                            static_cast<size_t>(HAL::getBufferTileWidth());
  auto *bufferPointer = (unsigned char *) HAL::getCanvasBuffer();

  HAL::delay(getUIConfig().fadeAnimationSpeed);

  if (getUIConfig().lightMode)
    switch (fadeFlag) {
      case 1:
        for (uint16_t i = 0; i < bufferLen; ++i) if (i % 2 != 0) bufferPointer[i] = bufferPointer[i] & 0xAA;
        break;
      case 2:
        for (uint16_t i = 0; i < bufferLen; ++i) if (i % 2 != 0) bufferPointer[i] = bufferPointer[i] & 0x00;
        break;
      case 3:
        for (uint16_t i = 0; i < bufferLen; ++i) if (i % 2 == 0) bufferPointer[i] = bufferPointer[i] & 0x55;
        break;
      case 4:
        for (uint16_t i = 0; i < bufferLen; ++i) if (i % 2 == 0) bufferPointer[i] = bufferPointer[i] & 0x00;
        break;
      default:
        //放动画结束退出函数的代码
        fadeFlag = 0;
        break;
    }
  else
    switch (fadeFlag) {
      case 1:
        for (uint16_t i = 0; i < bufferLen; ++i) if (i % 2 != 0) bufferPointer[i] = bufferPointer[i] | 0xAA;
        break;
      case 2:
        for (uint16_t i = 0; i < bufferLen; ++i) if (i % 2 != 0) bufferPointer[i] = bufferPointer[i] | 0x00;
        break;
      case 3:
        for (uint16_t i = 0; i < bufferLen; ++i) if (i % 2 == 0) bufferPointer[i] = bufferPointer[i] | 0x55;
        break;
      case 4:
        for (uint16_t i = 0; i < bufferLen; ++i) if (i % 2 == 0) bufferPointer[i] = bufferPointer[i] | 0x00;
        break;
      default:
        fadeFlag = 0;
        break;
    }
  fadeFlag++;
}

inline void Animation::blur() {
  static size_t bufferLen = 8 * static_cast<size_t>(HAL::getBufferTileHeight()) *
                            static_cast<size_t>(HAL::getBufferTileWidth());
  static auto *bufferPointer = (unsigned char *) HAL::getCanvasBuffer();

  for (uint16_t i = 0; i < bufferLen; ++i) bufferPointer[i] = bufferPointer[i] & (i % 2 == 0 ? 0x55 : 0xAA);
}

inline void Animation::move(float *_pos, float _posTrg, float _speed) {
  if (*_pos == _posTrg) return;
  float diff = _posTrg - *_pos;
  if (std::fabs(diff) <= 1.0f) {
    *_pos = _posTrg;
    return;
  }
  float dt = g_anim_dt * g_anim_speed_scale;
  if (dt < 0.001f) dt = 0.001f;
  if (dt > 0.25f) dt = 0.25f;
  float speed = _speed;
  if (speed < 1.0f) speed = 1.0f;
  if (speed > 95.0f) speed = 95.0f;
  float k = speed / 30.0f; // tuned to match legacy at 60fps
  float alpha = 1.0f - std::exp(-k * dt);
  *_pos += diff * alpha;
}
}

#endif //ASTRA_CORE_SRC_ASTRA_UI_ITEM_ITEM_H_
