#pragma once
#include <Epub.h>

#include <memory>
#include <string>
#include <vector>

#include "EpubReaderMenuActivity.h"
#include "SettingsList.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"
#include "util/QrUtils.h"

// Book menu on button boards: orientation, chapter, font, size and the page QR on one screen.
class ReaderQuickMenuActivity final : public Activity {
 public:
  using MenuItem = EpubReaderMenuActivity::MenuItem;
  ReaderQuickMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::shared_ptr<Epub> epub,
                          int tocIndex, uint8_t orientation, std::string pageText, std::vector<MenuItem> extras,
                          const SdCardFontRegistry* fontRegistry);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool acceptsRemoteInput() const override { return true; }

 private:
  enum Stop { STOP_PORTRAIT, STOP_LANDSCAPE, STOP_CHAPTER, STOP_FONT, STOP_SIZE_DOWN, STOP_SIZE_UP, STOP_EXTRAS };
  enum class Dropdown { None, Chapter, Font };

  int stopCount() const { return STOP_EXTRAS + static_cast<int>(extras.size()); }
  int dropdownCount() const;
  std::string dropdownLabel(int index) const;
  std::string fontLabel(int index) const;
  void move(int delta);
  void activate();
  void activateDropdown();
  void openDropdown(Dropdown kind, int selected);
  void setOrientation(uint8_t value);
  void changeSize(int delta);
  void requestClose(bool cancelled, int action);
  void afterChange();

  std::shared_ptr<Epub> epub;
  std::vector<MenuItem> extras;
  const SdCardFontRegistry* fontRegistry;
  SettingInfo fontSetting;
  SettingInfo sizeSetting;
  QrUtils::Code qr;
  std::string pageText;
  ButtonNavigator navigator;

  int focus = STOP_CHAPTER;
  int tocIndex;
  uint8_t orientation;
  bool textChanged = false;
  int chosenChapter = -1;
  bool closeRequested = false;
  bool closeCancelled = false;
  int closeAction = -1;
  Dropdown dropdown = Dropdown::None;
  int dropdownSelected = 0;
  int dropdownTop = 0;
};
