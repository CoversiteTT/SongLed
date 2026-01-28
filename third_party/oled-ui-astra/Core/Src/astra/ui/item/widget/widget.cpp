#include "widget.h"

#include <cmath>

namespace astra {

CheckBox::CheckBox(bool &_value) {
  value = _value;
  if (value) isCheck = true;
  else isCheck = false;
  this->parent = nullptr;
}

bool CheckBox::check() {
  value = true;
  isCheck = true;
  return isCheck;
}

bool CheckBox::uncheck() {
  value = false;
  isCheck = false;
  return isCheck;
}

bool CheckBox::toggle() {
  value = !value;
  isCheck = !isCheck;
  return isCheck;
}

void CheckBox::init() {
  if (value) isCheck = true;
  else isCheck = false;
}

void CheckBox::deInit() {
  delete this;
}

void CheckBox::renderIndicator(float _x, float _y, const std::vector<float> &_camera) {
  Item::updateConfig();
  //绘制外框
  HAL::setDrawType(1);
  HAL::drawRFrame(_x + _camera[0],
                  _y + _camera[1],
                  astraConfig.checkBoxWidth,
                  astraConfig.checkBoxHeight,
                  astraConfig.checkBoxRadius);
  if (isCheck) //绘制复选框内的点
    HAL::drawBox(_x + _camera[0] + astraConfig.checkBoxWidth / 4,
                 _y + _camera[1] + astraConfig.checkBoxHeight / 4,
                 astraConfig.checkBoxWidth / 2,
                 astraConfig.checkBoxHeight / 2);
}

void CheckBox::render(const std::vector<float> &_camera) {
  //todo 选中复选框后弹出消息提醒 这玩意现在我倒觉得没啥必要 可以暂时不做
}

PopUp::PopUp(unsigned char _direction,
             const std::string &_title,
             const std::vector<std::string> &_options,
             unsigned char &_value) {
  direction = _direction;
  title = _title;
  options = _options;
  boundary = options.size();
  value = _value;
  this->parent = nullptr;
}

void PopUp::selectNext() {
  if (value == boundary - 1) value = 0;
  else value++;
}

void PopUp::selectPreview() {
  if (value == 0) value = boundary - 1;
  else value--;
}

bool PopUp::select(unsigned char _index) {
  if (_index > boundary - 1) return false;
  value = _index;
  return true;
}

void PopUp::init() { }

void PopUp::deInit() {
  delete this;
}

void PopUp::renderIndicator(float _x, float _y, const std::vector<float> &_camera) {
  Item::updateConfig();
  HAL::setDrawType(1);
  //把左下角转换为左上角 居中
  HAL::drawEnglish(_x + _camera[0] + 1, _y + _camera[1] + astraConfig.listTextHeight, std::to_string(value));
}

void PopUp::render(const std::vector<float> &_camera) {
  //Widget::render(_camera);
}

Slider::Slider(const std::string &_title,
               unsigned char _min,
               unsigned char _max,
               unsigned char _step,
               unsigned char &_value) {
  title = _title;
  maxLength = 0;
  min = _min;
  max = _max;
  step = _step;
  value = _value;
  lengthIndicator = 0;
  this->parent = nullptr;

  if (value > max) valueOverflow = true;
  else valueOverflow = false;
}

unsigned char Slider::add() {
  value += step;
  return this->value;
}

unsigned char Slider::sub() {
  value -= step;
  return this->value;
}

void Slider::init() {
  maxLength = std::floor(HAL::getSystemConfig().screenWeight * 0.6);
  position.lTrg = std::floor(((float)(value - min) / (max - min)) * maxLength); //计算目标长度
  lengthIndicator = std::round(((float)(value - min) / (max - min)) * 6);  //映射在0-6个像素之间
  if (valueOverflow) {
    position.lTrg = maxLength;
    lengthIndicator = 6;
  }
}

void Slider::deInit() {
  delete this;
}

void Slider::renderIndicator(float _x, float _y, const std::vector<float> &_camera) {
  Item::updateConfig();
  const float rightEdge = _x + astraConfig.checkBoxWidth;
  const float top = _y + _camera[1];

  static const uint8_t BAYER4[4][4] = {
      {0, 8, 2, 10},
      {12, 4, 14, 6},
      {3, 11, 1, 9},
      {15, 7, 13, 5}};

  auto mapSelSpeedMs = [](unsigned char v) -> int {
    float maxMs = 900.0f;
    float base = 1.08f;
    float ms = maxMs / std::pow(base, static_cast<float>(v) - 1.0f);
    if (ms < 20.0f) ms = 20.0f;
    if (ms > 900.0f) ms = 900.0f;
    return static_cast<int>(std::round(ms));
  };

  int displayValue = value;
  std::string unit;
  if (title.find("Volume") != std::string::npos) {
    unit = "%";
  } else if (title.find("SPI") != std::string::npos) {
    unit = " MHz";
  } else if (title.find("Speed") != std::string::npos) {
    unit = "x";
  } else if (title.find("Sel") != std::string::npos) {
    displayValue = mapSelSpeedMs(value);
    unit = " ms";
  } else if (title.find("Pause") != std::string::npos) {
    displayValue = static_cast<int>(value) * 20;
    unit = " ms";
  } else if (title.find("Color") != std::string::npos) {
    displayValue = static_cast<int>(std::round(static_cast<float>(value) * 360.0f / 50.0f));
    unit = " deg";
  }

  std::string valText = std::to_string(displayValue) + unit;
  float textW = HAL::getFontWidth(valText);
  float textX = rightEdge - textW + _camera[0];
  if (textX < 0.0f) textX = 0.0f;
  float textY = top + astraConfig.listTextHeight;

  HAL::setDrawType(1);
  HAL::drawEnglish(textX, textY, valText);

  float lineY = textY + 2.0f;
  int lineW = static_cast<int>(std::round(textW));
  if (lineW < 2) lineW = 2;
  const float slope = 1.19f; // ~tan(50deg)
  float denom = static_cast<float>(lineW - 1);
  if (denom < 1.0f) denom = 1.0f;
  for (int row = 0; row < 2; ++row) {
    for (int i = 0; i < lineW; ++i) {
      float t = (static_cast<float>(i) + static_cast<float>(row) * slope) / denom;
      if (t < 0.0f) t = 0.0f;
      if (t > 1.0f) t = 1.0f;
      int level = static_cast<int>(std::round(t * 15.0f));
      int bx = (static_cast<int>(std::round(textX)) + i) & 3;
      int by = (static_cast<int>(std::round(lineY)) + row) & 3;
      if (level > BAYER4[by][bx]) {
        HAL::drawPixel(textX + i, lineY + row);
      }
    }
  }
}

void Slider::render(const std::vector<float> &_camera) {
  Widget::render(_camera);
}
}

