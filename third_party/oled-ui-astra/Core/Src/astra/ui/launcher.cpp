//
// Created by Fir on 2024/2/2.
//

#include "launcher.h"

namespace astra {

void Launcher::popInfo(std::string _info, uint16_t _time) {
  bool onRender = true;
  bool stickyByBackOnly = (_time == 65535);
  uint32_t beginTimeMs = HAL::millis();

  float padX = getUIConfig().popMargin;
  float padTop = 1.0f;
  float padBottom = 2.0f;

  float wPop = HAL::getFontWidth(_info) + 2 * padX;
  float hPop = HAL::getFontHeight() + padTop + padBottom;
  float yPop = 0 - hPop - 8;
  float yPopTrg = (HAL::getSystemConfig().screenHeight - hPop) / 3;
  float xPop = (HAL::getSystemConfig().screenWeight - wPop) / 2;
  float yHide = 0 - hPop - 8;

  while (onRender) {
    Animation::tick();
    time = HAL::millis();

    HAL::canvasClear();
    currentMenu->render(camera->getPosition());
    selector->render(camera->getPosition());
    camera->update(currentMenu, selector);
    if (popRenderHook) {
      popRenderHook();
    }

    HAL::setDrawType(0);
    HAL::drawRBox(xPop - 4, yPop - 4, wPop + 8, hPop + 8, getUIConfig().popRadius + 2);
    HAL::setDrawType(1);
    HAL::drawRFrame(xPop - 1, yPop - 1, wPop + 2, hPop + 2, getUIConfig().popRadius);
    HAL::drawEnglish(xPop + padX,
                     yPop + padTop + HAL::getFontHeight() - 2,
                     _info);

    HAL::canvasUpdate();
    Animation::move(&yPop, yPopTrg, getUIConfig().popSpeed);

    if (!stickyByBackOnly && time - beginTimeMs >= _time) yPopTrg = yHide;

    HAL::keyScan();
    if (HAL::getAnyKey()) {
      for (unsigned char i = 0; i < key::KEY_NUM; i++) {
        if (HAL::getKeyMap()[i] == key::CLICK) {
          if (!stickyByBackOnly || i == key::KEY_0) {
            yPopTrg = yHide;
          }
        }
      }
      std::fill(HAL::getKeyMap(), HAL::getKeyMap() + key::KEY_NUM, key::INVALID);
    }

    if (std::fabs(yPop - yHide) <= 1.0f) {
      yPop = yHide;
      onRender = false;
    }
  }
}

void Launcher::init(Menu *_rootPage) {
  currentMenu = _rootPage;

  camera = new Camera(0, 0);
  _rootPage->childPosInit(camera->getPosition());

  selector = new Selector();
  selector->inject(_rootPage);

  camera->init(_rootPage->getType());
}

/**
 * @brief 打开选中的页面
 *
 * @return 是否成功打开
 * @warning 仅可调用一次
 */
bool Launcher::open() {

  //如果当前页面指向的当前item没有后继 那就返回false
  if (currentMenu->getNextMenu() == nullptr) {
    popInfo("unreferenced page!", 600);
    return false;
  }
  if (currentMenu->getNextMenu()->getItemNum() == 0) {
    popInfo("empty page!", 600);
    return false;
  }

  currentMenu->rememberCameraPos(camera->getPositionTrg());

  currentMenu->deInit();  //先析构（退场动画）再挪动指针

  currentMenu = currentMenu->getNextMenu();
  currentMenu->selectIndex = 0;
  currentMenu->forePosInit();
  currentMenu->childPosInit(camera->getPosition());

  selector->inject(currentMenu);
  //selector->go(currentPage->selectIndex);

  return true;
}

/**
 * @brief 关闭选中的页面
 *
 * @return 是否成功关闭
 * @warning 仅可调用一次
 */
bool Launcher::close() {
  if (currentMenu->getPreview() == nullptr) {
    popInfo("unreferenced page!", 600);
    return false;
  }
  if (currentMenu->getPreview()->getItemNum() == 0) {
    popInfo("empty page!", 600);
    return false;
  }

  currentMenu->rememberCameraPos(camera->getPositionTrg());

  currentMenu->deInit();  //先析构（退场动画）再挪动指针

  currentMenu = currentMenu->getPreview();
  currentMenu->selectIndex = 0;
  currentMenu->forePosInit();
  currentMenu->childPosInit(camera->getPosition());

  selector->inject(currentMenu);
  //selector->go(currentPage->selectIndex);

  return true;
}

void Launcher::update() {
  Animation::tick();
  HAL::canvasClear();

  currentMenu->render(camera->getPosition());
  if (currentWidget != nullptr) currentWidget->render(camera->getPosition());
  selector->render(camera->getPosition());
  camera->update(currentMenu, selector);

//  if (time == 500) selector->go(3);  //test
//  if (time == 800) open();  //test
//  if (time == 1200) selector->go(0);  //test
//  if (time == 1500) selector->go(1z);  //test
//  if (time == 1800) selector->go(6);  //test
//  if (time == 2100) selector->go(1);  //test
//  if (time == 2300) selector->go(0);  //test
//  if (time == 2500) open();  //test
//  if (time == 2900) close();
//  if (time == 3200) selector->go(0);  //test
//  if (time >= 3250) time = 0;  //test

  time = HAL::millis();
}
}
