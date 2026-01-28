#include "astra/ui/item/item.h"

namespace astra {
float g_anim_dt = 1.0f / 60.0f;
uint32_t g_anim_last_ms = 0;
float g_anim_speed_scale = 1.0f;

void Animation::tick() {
  uint32_t now = HAL::millis();
  if (g_anim_last_ms == 0) {
    g_anim_last_ms = now;
    g_anim_dt = 1.0f / 60.0f;
    return;
  }
  uint32_t dt_ms = now - g_anim_last_ms;
  g_anim_last_ms = now;
  float dt = static_cast<float>(dt_ms) / 1000.0f;
  if (dt < 0.001f) dt = 0.001f;
  if (dt > 0.05f) dt = 0.05f;
  g_anim_dt = dt;
}
}  // namespace astra
