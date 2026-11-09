#include "ReaderQuickMenuActivity.h"

#include <BlePageTurner.h>
#include <GfxRenderer.h>
#include <HalFrontlight.h>
#include <I18n.h>

#include <algorithm>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "ReaderUtils.h"
#include "activities/ActivityResult.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
using MenuAction = EpubReaderMenuActivity::MenuAction;

constexpr int SIDE_MARGIN = 16;
constexpr int TILE_HEIGHT = 56;
constexpr int TILE_GAP = 12;
constexpr int ROW_HEIGHT = 44;
constexpr int DROPDOWN_ROW_HEIGHT = 40;
constexpr int CORNER_RADIUS = 8;
constexpr int SIZE_BOX_WIDTH = 48;
constexpr int SIZE_BOX_HEIGHT = 36;
constexpr int SIZE_VALUE_WIDTH = 72;
constexpr int CAPTION_HEIGHT = 64;

bool isLandscape(const uint8_t orientation) {
  return orientation == CrossPointSettings::LANDSCAPE_CW || orientation == CrossPointSettings::LANDSCAPE_CCW;
}

void drawDeviceIcon(const GfxRenderer& renderer, const int centerX, const int centerY, const bool landscape,
                    const bool black) {
  const int width = landscape ? 30 : 20;
  const int height = landscape ? 20 : 30;
  renderer.drawRoundedRect(centerX - width / 2, centerY - height / 2, width, height, 1, 3, black);
}

void drawChevron(const GfxRenderer& renderer, const int centerX, const int centerY, const bool open) {
  const int tip = open ? -3 : 3;
  renderer.drawLine(centerX - 6, centerY - tip, centerX, centerY + tip);
  renderer.drawLine(centerX, centerY + tip, centerX + 6, centerY - tip);
}
}  // namespace

ReaderQuickMenuActivity::ReaderQuickMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                 std::shared_ptr<Epub> epub, const int tocIndex,
                                                 const uint8_t orientation, std::string pageText,
                                                 std::vector<MenuItem> extras, const SdCardFontRegistry* fontRegistry)
    : Activity("ReaderQuickMenu", renderer, mappedInput),
      epub(std::move(epub)),
      extras(std::move(extras)),
      fontRegistry(fontRegistry),
      fontSetting(buildFontFamilySetting(fontRegistry)),
      sizeSetting(buildFontSizeSetting(fontRegistry)),
      pageText(std::move(pageText)),
      tocIndex(tocIndex),
      orientation(orientation) {}

void ReaderQuickMenuActivity::onEnter() {
  Activity::onEnter();
  if (!pageText.empty()) QrUtils::build(pageText, qr);
  std::string().swap(pageText);
  requestUpdate();
}

int ReaderQuickMenuActivity::dropdownCount() const {
  if (dropdown == Dropdown::Chapter) return epub->getTocItemsCount();
  if (dropdown == Dropdown::Font) {
    return static_cast<int>(fontSetting.enumStringValues.empty() ? fontSetting.enumValues.size()
                                                                 : fontSetting.enumStringValues.size());
  }
  return 0;
}

std::string ReaderQuickMenuActivity::dropdownLabel(const int index) const {
  if (dropdown == Dropdown::Chapter) return epub->getTocItem(index).title;
  return fontLabel(index);
}

std::string ReaderQuickMenuActivity::fontLabel(const int index) const {
  if (!fontSetting.enumStringValues.empty()) return fontSetting.enumStringValues[index];
  return I18N.get(fontSetting.enumValues[index]);
}

void ReaderQuickMenuActivity::move(const int delta) {
  if (dropdown != Dropdown::None) {
    const int count = dropdownCount();
    if (count > 0) dropdownSelected = (dropdownSelected + count + delta) % count;
  } else {
    focus = (focus + stopCount() + delta) % stopCount();
  }
}

void ReaderQuickMenuActivity::openDropdown(const Dropdown kind, const int selected) {
  dropdown = kind;
  dropdownSelected = std::max(selected, 0);
  dropdownTop = 0;
}

void ReaderQuickMenuActivity::setOrientation(const uint8_t value) {
  if (isLandscape(value) == isLandscape(orientation)) return;
  orientation = value;
  // Only the renderer turns here. The reader reflows when the menu closes.
  ReaderUtils::applyOrientation(renderer, orientation);
}

void ReaderQuickMenuActivity::changeSize(const int delta) {
  const int count = static_cast<int>(sizeSetting.enumStringValues.size());
  const int next = sizeSetting.valueGetter() + delta;
  if (next < 0 || next >= count) return;
  sizeSetting.valueSetter(static_cast<uint8_t>(next));
  textChanged = true;
  SETTINGS.saveToFile();  // the result callback does not run on sleep
}

void ReaderQuickMenuActivity::requestClose(const bool cancelled, const int action) {
  closeRequested = true;
  closeCancelled = cancelled;
  closeAction = action;
}

void ReaderQuickMenuActivity::afterChange() {
  if (!closeRequested) {
    requestUpdate();
    return;
  }
  ActivityResult result;
  result.isCancelled = closeCancelled;
  MenuResult menu;
  menu.action = closeAction;
  menu.orientation = orientation;
  menu.textChanged = textChanged;
  menu.chapterToc = chosenChapter;
  result.data = menu;
  setResult(std::move(result));
  finish();
}

void ReaderQuickMenuActivity::activateDropdown() {
  if (dropdown == Dropdown::Chapter) {
    chosenChapter = dropdownSelected;
    requestClose(false, -1);
    return;
  }
  fontSetting.valueSetter(static_cast<uint8_t>(dropdownSelected));
  sizeSetting = buildFontSizeSetting(fontRegistry);
  textChanged = true;
  SETTINGS.saveToFile();  // the result callback does not run on sleep
  dropdown = Dropdown::None;
}

void ReaderQuickMenuActivity::activate() {
  if (dropdown != Dropdown::None) return activateDropdown();
  switch (focus) {
    case STOP_PORTRAIT:
      return setOrientation(CrossPointSettings::PORTRAIT);
    case STOP_LANDSCAPE:
      return setOrientation(CrossPointSettings::LANDSCAPE_CW);
    case STOP_CHAPTER:
      return openDropdown(Dropdown::Chapter, tocIndex);
    case STOP_FONT:
      return openDropdown(Dropdown::Font, fontSetting.valueGetter());
    case STOP_SIZE_DOWN:
      return changeSize(-1);
    case STOP_SIZE_UP:
      return changeSize(1);
    default:
      break;
  }

  const MenuAction action = extras[focus - STOP_EXTRAS].action;
  if (action == MenuAction::BLUETOOTH) {
    EpubReaderMenuActivity::toggleBluetooth();
  } else if (action == MenuAction::FRONTLIGHT) {
    const bool lightOn = !Frontlight.isOn();
    Frontlight.setOn(lightOn);
    SETTINGS.frontlightOn = lightOn ? 1 : 0;
    SETTINGS.saveToFile();
  } else {
    requestClose(false, static_cast<int>(action));
  }
}

void ReaderQuickMenuActivity::loop() {
  using Button = MappedInputManager::Button;
  // Everything render reads changes under the lock, and the result is sent after it is released.
  if (mappedInput.wasReleased(Button::Back)) {
    {
      RenderLock lock;
      if (dropdown == Dropdown::None) {
        requestClose(true, -1);
      } else {
        dropdown = Dropdown::None;
      }
    }
    return afterChange();
  }
  if (mappedInput.wasReleased(Button::Confirm)) {
    {
      RenderLock lock;
      activate();
    }
    return afterChange();
  }
  // Focus only picks what is highlighted, so a press never waits for a repaint here.
  navigator.onNext([this] {
    move(1);
    requestUpdate();
  });
  navigator.onPrevious([this] {
    move(-1);
    requestUpdate();
  });
}

void ReaderQuickMenuActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  const bool wide = safe.width > safe.height;
  const int left = safe.x + SIDE_MARGIN;
  const int captionY = safe.y + safe.height - CAPTION_HEIGHT;
  const int top = safe.y + SIDE_MARGIN;
  const int qrSide = wide ? std::min(captionY - top - 8, safe.width / 2) : safe.width - 2 * SIDE_MARGIN;
  const int columnWidth = wide ? safe.width - 3 * SIDE_MARGIN - qrSide : safe.width - 2 * SIDE_MARGIN;

  const int tileWidth = (columnWidth - TILE_GAP) / 2;
  const bool landscapeNow = isLandscape(orientation);
  for (int i = 0; i < 2; i++) {
    const int x = left + i * (tileWidth + TILE_GAP);
    const bool current = (i == 1) == landscapeNow;
    if (current) {
      renderer.fillRoundedRect(x, top, tileWidth, TILE_HEIGHT, CORNER_RADIUS, Color::Black);
    } else {
      renderer.drawRoundedRect(x, top, tileWidth, TILE_HEIGHT, 1, CORNER_RADIUS, true);
    }
    drawDeviceIcon(renderer, x + tileWidth / 2, top + TILE_HEIGHT / 2, i == 1, !current);
    if (dropdown == Dropdown::None && focus == STOP_PORTRAIT + i) {
      renderer.fillRect(x + 12, top + TILE_HEIGHT + 2, tileWidth - 24, 3, true);
    }
  }

  int y = top + TILE_HEIGHT + 12;
  const auto drawRow = [&](const int stop, const char* label, const bool hasChevron, const bool open) {
    if (dropdown == Dropdown::None && focus == stop) {
      renderer.fillRoundedRect(left - 8, y, columnWidth + 16, ROW_HEIGHT - 4, CORNER_RADIUS, Color::LightGray);
    }
    const int textY = y + (ROW_HEIGHT - 4 - renderer.getLineHeight(UI_12_FONT_ID)) / 2;
    const int width = columnWidth - (hasChevron ? 28 : 0);
    renderer.drawText(UI_12_FONT_ID, left, textY, renderer.truncatedText(UI_12_FONT_ID, label, width).c_str());
    if (hasChevron) drawChevron(renderer, left + columnWidth - 10, y + (ROW_HEIGHT - 4) / 2, open);
    y += ROW_HEIGHT;
  };

  const std::string chapterTitle = tocIndex >= 0 ? epub->getTocItem(tocIndex).title : tr(STR_SELECT_CHAPTER);
  drawRow(STOP_CHAPTER, chapterTitle.c_str(), true, dropdown == Dropdown::Chapter);
  const int fontIndex = fontSetting.valueGetter();
  const std::string fontName = fontLabel(fontIndex);
  drawRow(STOP_FONT, fontName.c_str(), true, dropdown == Dropdown::Font);

  const int sizeY = y;
  drawRow(-1, tr(STR_SIZE), false, false);
  const int boxY = sizeY + (ROW_HEIGHT - 4 - SIZE_BOX_HEIGHT) / 2;
  const int plusX = left + columnWidth - SIZE_BOX_WIDTH;
  const int minusX = plusX - SIZE_VALUE_WIDTH - SIZE_BOX_WIDTH;
  for (const int stop : {static_cast<int>(STOP_SIZE_DOWN), static_cast<int>(STOP_SIZE_UP)}) {
    const int boxX = stop == STOP_SIZE_DOWN ? minusX : plusX;
    if (dropdown == Dropdown::None && focus == stop) {
      renderer.fillRoundedRect(boxX, boxY, SIZE_BOX_WIDTH, SIZE_BOX_HEIGHT, CORNER_RADIUS, Color::LightGray);
    }
    renderer.drawRoundedRect(boxX, boxY, SIZE_BOX_WIDTH, SIZE_BOX_HEIGHT, 1, CORNER_RADIUS, true);
    renderer.fillRect(boxX + SIZE_BOX_WIDTH / 2 - 7, boxY + SIZE_BOX_HEIGHT / 2 - 1, 14, 2, true);
    if (stop == STOP_SIZE_UP) {
      renderer.fillRect(boxX + SIZE_BOX_WIDTH / 2 - 1, boxY + SIZE_BOX_HEIGHT / 2 - 7, 2, 14, true);
    }
  }
  const char* sizeLabel = sizeSetting.enumStringValues[sizeSetting.valueGetter()].c_str();
  const int sizeTextX =
      minusX + SIZE_BOX_WIDTH + (SIZE_VALUE_WIDTH - renderer.getTextWidth(UI_12_FONT_ID, sizeLabel)) / 2;
  renderer.drawText(UI_12_FONT_ID, sizeTextX, sizeY + (ROW_HEIGHT - 4 - renderer.getLineHeight(UI_12_FONT_ID)) / 2,
                    sizeLabel);

  for (size_t i = 0; i < extras.size(); i++) {
    drawRow(STOP_EXTRAS + static_cast<int>(i), I18N.get(extras[i].labelId), false, false);
  }

  const Rect qrArea = wide ? Rect{left + columnWidth + SIDE_MARGIN, top, qrSide, captionY - top - 4}
                           : Rect{left, y + 4, columnWidth, captionY - y - 8};
  if (dropdown != Dropdown::None) {
    const int visible = std::max(1, (safe.y + safe.height - qrArea.y) / DROPDOWN_ROW_HEIGHT);
    if (dropdownSelected < dropdownTop) dropdownTop = dropdownSelected;
    if (dropdownSelected >= dropdownTop + visible) dropdownTop = dropdownSelected - visible + 1;
    const int count = dropdownCount();
    const int current = dropdown == Dropdown::Chapter ? tocIndex : fontIndex;
    for (int row = dropdownTop; row < std::min(dropdownTop + visible, count); row++) {
      const int rowY = qrArea.y + (row - dropdownTop) * DROPDOWN_ROW_HEIGHT;
      if (row == dropdownSelected) {
        renderer.fillRoundedRect(qrArea.x - 8, rowY, qrArea.width + 16, DROPDOWN_ROW_HEIGHT - 4, CORNER_RADIUS,
                                 Color::LightGray);
      }
      const auto style = row == current ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
      const std::string label = dropdownLabel(row);
      renderer.drawText(UI_10_FONT_ID, qrArea.x,
                        rowY + (DROPDOWN_ROW_HEIGHT - 4 - renderer.getLineHeight(UI_10_FONT_ID)) / 2,
                        renderer.truncatedText(UI_10_FONT_ID, label.c_str(), qrArea.width, style).c_str(), true, style);
    }
  } else {
    QrUtils::draw(renderer, qrArea, qr);
    UITheme::drawCenteredWrappedText(renderer,
                                     Rect{safe.x + SIDE_MARGIN, captionY, safe.width - 2 * SIDE_MARGIN, CAPTION_HEIGHT},
                                     UI_10_FONT_ID, tr(STR_SCREENSHOT_HINT), 2);
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
