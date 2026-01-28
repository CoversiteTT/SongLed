//
// Created by Fir on 2024/4/14 014.
//

#include "selector.h"

namespace astra {

void Selector::setPosition() {
  //在go的时候改变trg的值
  if (menu->getType() == "Tile") {
//    xTrg = menu->child[_index]->position.xTrg - (astraConfig.tileSelectBoxWeight - astraConfig.tilePicWidth) / 2;
//    yTrg = menu->child[_index]->position.yTrg - (astraConfig.tileSelectBoxHeight - astraConfig.tilePicHeight) / 2;
    xTrg = menu->childMenu[menu->selectIndex]->position.xTrg - astraConfig.tileSelectBoxMargin;
    yTrg = menu->childMenu[menu->selectIndex]->position.yTrg - astraConfig.tileSelectBoxMargin;

    yText = systemConfig.screenHeight; //给磁贴文字归零 从屏幕外滑入
    yTextTrg = systemConfig.screenHeight - astraConfig.tileTextBottomMargin;

    wTrg = astraConfig.tileSelectBoxWidth;
    hTrg = astraConfig.tileSelectBoxHeight;
  } else if (menu->getType() == "List") {
    xTrg = menu->childMenu[menu->selectIndex]->position.xTrg - astraConfig.selectorMargin;
    yTrg = menu->childMenu[menu->selectIndex]->position.yTrg;

    wTrg = (float) HAL::getFontWidth(menu->childMenu[menu->selectIndex]->title) + astraConfig.listTextMargin * 2;
    hTrg = astraConfig.listLineHeight;
    if (xTrg != x || yTrg != y || wTrg != w || hTrg != h) {
      listAnimStartMs = HAL::millis();
      listStartX = x;
      listStartY = y;
      listStartW = w;
      listStartH = h;
      listAnimActive = true;
    }
  }
}

/**
 * @brief
 *
 * @param _index
 * @note selector接管了移动选择指针的功能
 * @warning not support in loop. 不支持在循环内执行
 */
void Selector::go(unsigned char _index) {
  Item::updateConfig();

//  if (_index > menu->childMenu.size() - 1) {
//    if (astraConfig.menuLoop) _index = 0;
//    else return;
//  } else if (_index < 0) {
//    if (astraConfig.menuLoop) _index = menu->childMenu.size() - 1;
//    else return;
//  }

  if (_index > menu->childMenu.size() - 1) return;
  menu->selectIndex = _index;
  lastMoveMs = HAL::millis();

  setPosition();
}

void Selector::goNext() {
  uint32_t now = HAL::millis();
  uint32_t pauseMs = static_cast<uint32_t>(astraConfig.wrapPauseMs);
  if (now < wrapPauseUntilMs && wrapPauseDir == 1) return;
  if (this->menu->selectIndex == this->menu->childMenu.size() - 1) {
    if (astraConfig.menuLoop) {
      if (pauseMs == 0 || (lastMoveMs != 0 && (now - lastMoveMs) >= pauseMs)) {
        wrapArmed = false;
        go(0);
        wrapPauseUntilMs = now + pauseMs;
        wrapPauseDir = 1;
        return;
      }
      if (!wrapArmed || wrapArmedDir != 1) {
        wrapArmed = true;
        wrapArmedDir = 1;
        wrapPauseUntilMs = now + pauseMs;
        wrapPauseDir = 1;
        return;
      }
      if (now < wrapPauseUntilMs) return;
      wrapArmed = false;
      go(0);
      wrapPauseUntilMs = now + pauseMs;
      wrapPauseDir = 1;
    } else {
      return;
    }
  } else {
    wrapArmed = false;
    go(menu->selectIndex + 1);
  }
  setPosition();
}

void Selector::goPreview() {
  uint32_t now = HAL::millis();
  uint32_t pauseMs = static_cast<uint32_t>(astraConfig.wrapPauseMs);
  if (now < wrapPauseUntilMs && wrapPauseDir == -1) return;
  if (this->menu->selectIndex == 0) {
    if (astraConfig.menuLoop) {
      if (pauseMs == 0 || (lastMoveMs != 0 && (now - lastMoveMs) >= pauseMs)) {
        wrapArmed = false;
        go(this->menu->childMenu.size() - 1);
        wrapPauseUntilMs = now + pauseMs;
        wrapPauseDir = -1;
        return;
      }
      if (!wrapArmed || wrapArmedDir != -1) {
        wrapArmed = true;
        wrapArmedDir = -1;
        wrapPauseUntilMs = now + pauseMs;
        wrapPauseDir = -1;
        return;
      }
      if (now < wrapPauseUntilMs) return;
      wrapArmed = false;
      go(this->menu->childMenu.size() - 1);
      wrapPauseUntilMs = now + pauseMs;
      wrapPauseDir = -1;
    } else {
      return;
    }
  } else {
    wrapArmed = false;
    go(menu->selectIndex - 1);
  }
  setPosition();
}

bool Selector::inject(Menu *_menu) {
  if (_menu == nullptr) return false;

  //if (this->menu != nullptr) return false;
  this->menu = _menu;

  go(this->menu->selectIndex);  //注入之后要初始化选择框的位置

  return true;
}

bool Selector::destroy() {
  if (this->menu == nullptr) return false;

  delete this->menu;
  this->menu = nullptr;
  return true;
}

void Selector::render(std::vector<float> _camera) {
  Item::updateConfig();

  //实际上 这里已经实现过渡动画了
  if (menu->getType() == "List" && listAnimActive) {
    uint32_t now = HAL::millis();
    float duration = astraConfig.selectorListDurationMs;
    if (duration < 1.0f) duration = 1.0f;
    float t = (now - listAnimStartMs) / duration;
    if (t >= 1.0f) {
      t = 1.0f;
      listAnimActive = false;
    }
    float ease = t * t * (3.0f - 2.0f * t);
    x = listStartX + (xTrg - listStartX) * ease;
    y = listStartY + (yTrg - listStartY) * ease;
    w = listStartW + (wTrg - listStartW) * ease;
    h = listStartH + (hTrg - listStartH) * ease;
  } else {
    Animation::move(&x, xTrg, astraConfig.selectorXAnimationSpeed);
    Animation::move(&y, yTrg, astraConfig.selectorYAnimationSpeed);
    Animation::move(&h, hTrg, astraConfig.selectorHeightAnimationSpeed);
    Animation::move(&w, wTrg, astraConfig.selectorWidthAnimationSpeed);
  }

  if (menu->getType() == "Tile") {
    Animation::move(&yText, yTextTrg, astraConfig.selectorYAnimationSpeed);

    //draw text.
    //文字不受摄像机的影响
    HAL::setDrawType(1);
    HAL::drawChinese((systemConfig.screenWeight -
                      (float) HAL::getFontWidth(menu->childMenu[menu->selectIndex]->title)) / 2.0,
                     yText + astraConfig.tileTitleHeight,
                     menu->childMenu[menu->selectIndex]->title);

    //draw box.
    //大框需要受摄像机的影响
    HAL::setDrawType(2);
    HAL::drawPixel(x + _camera[0], y + _camera[1]);
    //左上角
    HAL::drawHLine(x + _camera[0], y + _camera[1], astraConfig.tileSelectBoxLineLength + 1);
    HAL::drawVLine(x + _camera[0], y + _camera[1], astraConfig.tileSelectBoxLineLength + 1);
    //左下角
    HAL::drawHLine(x + _camera[0], y + _camera[1] + h - 1, astraConfig.tileSelectBoxLineLength + 1);
    HAL::drawVLine(x + _camera[0],
                   y + _camera[1] + h - astraConfig.tileSelectBoxLineLength - 1,
                   astraConfig.tileSelectBoxLineLength);
    //右上角
    HAL::drawHLine(x + _camera[0] + w - astraConfig.tileSelectBoxLineLength - 1,
                   y + _camera[1],
                   astraConfig.tileSelectBoxLineLength);
    HAL::drawVLine(x + _camera[0] + w - 1, y + _camera[1], astraConfig.tileSelectBoxLineLength + 1);
    //右下角
    HAL::drawHLine(x + _camera[0] + w - astraConfig.tileSelectBoxLineLength - 1,
                   y + _camera[1] + h - 1,
                   astraConfig.tileSelectBoxLineLength);
    HAL::drawVLine(x + _camera[0] + w - 1,
                   y + _camera[1] + h - astraConfig.tileSelectBoxLineLength - 1,
                   astraConfig.tileSelectBoxLineLength);

    HAL::drawPixel(x + _camera[0] + w - 1, y + _camera[1] + h - 1);
  } else if (menu->getType() == "List") {
    //animation(&h, hTrg, astraConfig.selectorAnimationSpeed);

    //draw select box.
    //受摄像机的影响
    HAL::setDrawType(2);
    HAL::drawRBox(x + _camera[0], y + _camera[1], w, h - 1, astraConfig.selectorRadius);
    //HAL::drawRBox(x, y, w, astraConfig.listLineHeight, astraConfig.selectorRadius);
    HAL::setDrawType(1);
  }
}

std::vector<float> Selector::getPosition() {
  return {xTrg, yTrg};
}
}
