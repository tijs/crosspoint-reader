#include "NetworkModeSelectionActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <cstring>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

void NetworkModeSelectionActivity::onEnter() {
  Activity::onEnter();

  selectedIndex = 0;
  hasArticlesUrl = strlen(SETTINGS.articlesBackendUrl) > 0;

  requestUpdate();
}

void NetworkModeSelectionActivity::onExit() { Activity::onExit(); }

void NetworkModeSelectionActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    onCancel();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    // Parallel array kept in sync with menu items built in render()
    NetworkMode modes[] = {NetworkMode::JOIN_NETWORK, NetworkMode::CONNECT_CALIBRE, NetworkMode::CREATE_HOTSPOT,
                           NetworkMode::SYNC_ARTICLES};
    onModeSelected(modes[selectedIndex]);
    return;
  }

  buttonNavigator.onNext([this] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, (hasArticlesUrl ? 4 : 3));
    requestUpdate();
  });

  buttonNavigator.onPrevious([this] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, (hasArticlesUrl ? 4 : 3));
    requestUpdate();
  });
}

void NetworkModeSelectionActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_FILE_TRANSFER));

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;

  // Build menu dynamically based on whether articles backend is configured
  StrId menuItems[4] = {StrId::STR_JOIN_NETWORK, StrId::STR_CALIBRE_WIRELESS, StrId::STR_CREATE_HOTSPOT};
  StrId menuDescs[4] = {StrId::STR_JOIN_DESC, StrId::STR_CALIBRE_DESC, StrId::STR_HOTSPOT_DESC};
  UIIcon menuIcons[4] = {UIIcon::Wifi, UIIcon::Library, UIIcon::Hotspot};

  if (hasArticlesUrl) {
    menuItems[3] = StrId::STR_SYNC_ARTICLES;
    menuDescs[3] = StrId::STR_SYNC_ARTICLES_DESC;
    menuIcons[3] = UIIcon::Wifi;
  }

  const int count = (hasArticlesUrl ? 4 : 3);
  GUI.drawList(
      renderer, Rect{0, contentTop, pageWidth, contentHeight}, count, selectedIndex,
      [&menuItems](int index) { return std::string(I18N.get(menuItems[index])); },
      [&menuDescs](int index) { return std::string(I18N.get(menuDescs[index])); },
      [&menuIcons](int index) { return menuIcons[index]; });

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}

void NetworkModeSelectionActivity::onModeSelected(NetworkMode mode) {
  setResult(NetworkModeResult{mode});
  finish();
}

void NetworkModeSelectionActivity::onCancel() {
  ActivityResult result;
  result.isCancelled = true;
  setResult(std::move(result));
  finish();
}
