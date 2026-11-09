#include "HomeDashboard.h"

#include <Bitmap.h>
#include <BoardConfig.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <TrustedTime.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>

#include "ReadingDays.h"
#include "components/ReadingMapDraw.h"
#include "components/UITheme.h"
#include "components/icons/blocks.h"
#include "components/icons/folder.h"
#include "components/icons/library.h"
#include "components/icons/settings2.h"
#include "components/icons/transfer.h"
#include "fontIds.h"

namespace {
constexpr int SIDE_MARGIN = 28;
constexpr int CARD_INSET = 16;
constexpr int TOP = 34;
constexpr int BLOCK_GAP = 14;
constexpr int BOTTOM_RESERVE = 40;
constexpr int CELL_PITCH = 8;
constexpr int CELL_SIZE = 7;
constexpr int CORNER_RADIUS = 8;
constexpr int LONG_READ_LEVEL = 3;
constexpr int CALENDAR_HEIGHT = 110;
constexpr int CURRENT_HEIGHT = 152;
constexpr int FINISHED_HEIGHT = 176;
constexpr int SHELF_COVER_WIDTH = 97;
constexpr int SHELF_GAP = 12;
constexpr int TILE_COLUMNS = 4;
constexpr int TILE_WIDTH = SHELF_COVER_WIDTH;
constexpr int TILE_GAP = SHELF_GAP;
constexpr int TILE_HEIGHT = 104;
constexpr int TILE_ICON_SIZE = 32;
constexpr int TILE_TEXT_INSET = 5;
constexpr int MAX_EXTRA_BLOCK_GAP = 40;

const uint8_t* iconBitmap(const UIIcon icon) {
  switch (icon) {
    case UIIcon::Folder:
      return FolderIcon;
    case UIIcon::Library:
      return LibraryIcon;
    case UIIcon::Transfer:
      return TransferIcon;
    case UIIcon::Settings:
      return Settings2Icon;
    default:
      return BlocksIcon;
  }
}

struct CalendarContext {
  const GfxRenderer& renderer;
  int top;
  int firstOffset;
  int today;  // -1 while the date is unknown
  uint32_t totalSeconds;
};

void drawCalendarDay(void* context, const int day, const uint16_t seconds) {
  auto& c = *static_cast<CalendarContext*>(context);
  c.totalSeconds += seconds;
  const int position = day + c.firstOffset;
  const int x = SIDE_MARGIN + (position / 7) * CELL_PITCH;
  const int y = c.top + (position % 7) * CELL_PITCH;
  const int level = c.today >= 0 && day <= c.today ? readingMapLevel(seconds) : 0;
  if (level >= LONG_READ_LEVEL) {
    c.renderer.fillRect(x, y, CELL_SIZE, CELL_SIZE);
  } else if (level > 0) {
    c.renderer.fillRect(x + 2, y + 2, CELL_SIZE - 4, CELL_SIZE - 4);
  }
  if (day == c.today) c.renderer.drawRect(x - 1, y - 1, CELL_SIZE + 2, CELL_SIZE + 2);
}

void drawCalendar(const GfxRenderer& renderer, const int top) {
  const int pageWidth = renderer.getScreenWidth();
  const int64_t now = trustedtime::trustedNow();
  struct tm today{};
  if (now != 0) {
    const time_t nowTime = static_cast<time_t>(now);
    localtime_r(&nowTime, &today);
  }
  const int year = now != 0 ? today.tm_year + 1900 : 0;
  const bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
  const int firstOffset = now != 0 ? ((today.tm_wday - today.tm_yday) % 7 + 13) % 7 : 0;

  const int mapTop = top + 34;
  CalendarContext context{renderer, mapTop, firstOffset, now != 0 ? today.tm_yday : -1, 0};
  readingdays::forEachDay(year, leap ? 366 : 365, drawCalendarDay, &context);

  char label[16];
  snprintf(label, sizeof(label), "%d", year);
  renderer.drawText(UI_12_FONT_ID, SIDE_MARGIN, top, now != 0 ? label : tr(STR_NOT_SET));
  snprintf(label, sizeof(label), "%lu %s", static_cast<unsigned long>(context.totalSeconds / 3600),
           tr(STR_HOURS_SHORT));
  renderer.drawText(UI_12_FONT_ID, pageWidth - SIDE_MARGIN - renderer.getTextWidth(UI_12_FONT_ID, label), top, label);
}

// Clears the square corners outside the frame radius, then draws the frame.
void drawRoundedFrame(const GfxRenderer& renderer, const int x, const int y, const int width, const int height) {
  for (int dy = 0; dy < CORNER_RADIUS; dy++) {
    for (int dx = 0; dx < CORNER_RADIUS; dx++) {
      const int fromCenterX = CORNER_RADIUS - dx;
      const int fromCenterY = CORNER_RADIUS - dy;
      if (fromCenterX * fromCenterX + fromCenterY * fromCenterY <= CORNER_RADIUS * CORNER_RADIUS) continue;
      renderer.drawPixel(x + dx, y + dy, false);
      renderer.drawPixel(x + width - 1 - dx, y + dy, false);
      renderer.drawPixel(x + dx, y + height - 1 - dy, false);
      renderer.drawPixel(x + width - 1 - dx, y + height - 1 - dy, false);
    }
  }
  renderer.drawRoundedRect(x, y, width, height, 1, CORNER_RADIUS, true);
}

bool drawThumb(const GfxRenderer& renderer, const RecentBook& book, const int height, const int x, const int y,
               const int width) {
  bool drawn = false;
  if (!book.coverBmpPath.empty()) {
    const std::string path = UITheme::getCoverThumbPath(book.coverBmpPath, height);
    HalFile file;
    if (Storage.exists(path.c_str()) && Storage.openFileForRead("HOME", path, file)) {
      Bitmap bitmap(file);
      if (bitmap.parseHeaders() == BmpReaderError::Ok) {
        drawn = BaseTheme::drawCoverThumbFill(renderer, bitmap, Rect{x, y, width, height});
      }
    }
  }
  if (!drawn) BaseTheme::drawCoverPlaceholder(renderer, Rect{x, y, width, height});
  drawRoundedFrame(renderer, x, y, width, height);
  return drawn;
}

void drawSelectionBar(const GfxRenderer& renderer, const int x, const int y, const int height) {
  renderer.fillRect(x + 4, y + 10, 4, height - 20);
}

void drawCurrent(const GfxRenderer& renderer, const HomeDashboardProps& props, const int top) {
  const int pageWidth = renderer.getScreenWidth();
  const int cardWidth = pageWidth - 2 * CARD_INSET;
  renderer.drawRoundedRect(CARD_INSET, top, cardWidth, CURRENT_HEIGHT, 1, CORNER_RADIUS, true);
  if (props.currentSelected) drawSelectionBar(renderer, CARD_INSET, top, CURRENT_HEIGHT);

  if (!props.currentBook) {
    renderer.drawText(UI_12_FONT_ID, SIDE_MARGIN + 8, top + 54, tr(STR_NO_OPEN_BOOK), true, EpdFontFamily::BOLD);
    renderer.drawText(UI_10_FONT_ID, SIDE_MARGIN + 8, top + 86, tr(STR_START_READING));
    return;
  }

  const RecentBook& book = *props.currentBook;
  const int coverWidth = HOME_CURRENT_COVER_HEIGHT * 2 / 3;
  drawThumb(renderer, book, HOME_CURRENT_COVER_HEIGHT, SIDE_MARGIN, top + 8, coverWidth);

  const int textX = SIDE_MARGIN + coverWidth + 16;
  const int textWidth = pageWidth - textX - SIDE_MARGIN;
  const std::string title = renderer.truncatedText(UI_12_FONT_ID, book.title.c_str(), textWidth, EpdFontFamily::BOLD);
  renderer.drawText(UI_12_FONT_ID, textX, top + 26, title.c_str(), true, EpdFontFamily::BOLD);
  const std::string author = renderer.truncatedText(UI_10_FONT_ID, book.author.c_str(), textWidth);
  renderer.drawText(UI_10_FONT_ID, textX, top + 60, author.c_str());

  if (props.currentProgress >= 0) {
    char percent[8];
    snprintf(percent, sizeof(percent), "%d%%", props.currentProgress);
    renderer.drawText(UI_10_FONT_ID, textX, top + 96, percent);
    const int barY = top + 128;
    renderer.drawRect(textX, barY, textWidth, 8);
    renderer.fillRect(textX, barY, textWidth * std::min(props.currentProgress, 100) / 100, 8);
  }
}

void drawFinished(const GfxRenderer& renderer, const HomeDashboardProps& props, const int top) {
  const int pageWidth = renderer.getScreenWidth();
  renderer.drawText(UI_12_FONT_ID, SIDE_MARGIN, top, tr(STR_FINISHED));
  char count[24];
  snprintf(count, sizeof(count), "%lu %s", static_cast<unsigned long>(props.finishedTotal),
           homeBookCountWord(props.finishedTotal));
  renderer.drawText(UI_10_FONT_ID, pageWidth - SIDE_MARGIN - renderer.getTextWidth(UI_10_FONT_ID, count), top + 4,
                    count);

  const int coversTop = top + 30;
  for (size_t i = 0; i < 4; i++) {
    const int x = SIDE_MARGIN + static_cast<int>(i) * (SHELF_COVER_WIDTH + SHELF_GAP);
    if (props.finishedBooks && i < props.finishedBooks->size()) {
      drawThumb(renderer, (*props.finishedBooks)[i], HOME_FINISHED_COVER_HEIGHT, x, coversTop, SHELF_COVER_WIDTH);
    } else {
      renderer.drawRoundedRect(x, coversTop, SHELF_COVER_WIDTH, HOME_FINISHED_COVER_HEIGHT, 1, CORNER_RADIUS, true);
    }
  }
}

int menuHeight(const int count) {
  const int rows = (count + TILE_COLUMNS - 1) / TILE_COLUMNS;
  return rows * TILE_HEIGHT + (rows - 1) * TILE_GAP;
}

void drawMenu(const GfxRenderer& renderer, const HomeDashboardProps& props, const int top) {
  // One size for the whole row: long translations step every label down together.
  bool labelsFit = true;
  for (int i = 0; i < props.menuCount; i++) {
    labelsFit =
        labelsFit && renderer.getTextWidth(UI_10_FONT_ID, props.menuLabels[i]) <= TILE_WIDTH - 2 * TILE_TEXT_INSET;
  }
  const int labelFont = labelsFit ? UI_10_FONT_ID : SMALL_FONT_ID;
  const int lineHeight = renderer.getLineHeight(labelFont);
  for (int i = 0; i < props.menuCount; i++) {
    const int x = SIDE_MARGIN + (i % TILE_COLUMNS) * (TILE_WIDTH + TILE_GAP);
    const int y = top + (i / TILE_COLUMNS) * (TILE_HEIGHT + TILE_GAP);
    renderer.drawRoundedRect(x, y, TILE_WIDTH, TILE_HEIGHT, 1, CORNER_RADIUS, true);
    if (i == props.selectedMenuItem) renderer.fillRect(x + 12, y + TILE_HEIGHT - 9, TILE_WIDTH - 24, 4);

    const int contentTop = y + (TILE_HEIGHT - TILE_ICON_SIZE - lineHeight - 4) / 2;
    renderer.drawIcon(iconBitmap(props.menuIcons[i]), x + (TILE_WIDTH - TILE_ICON_SIZE) / 2, contentTop,
                      TILE_ICON_SIZE);
    const int labelWidth = renderer.getTextWidth(labelFont, props.menuLabels[i]);
    renderer.drawText(labelFont, x + (TILE_WIDTH - labelWidth) / 2, contentTop + TILE_ICON_SIZE + 4,
                      props.menuLabels[i]);
  }
}
}  // namespace

bool homeLayoutsSupported(const GfxRenderer& renderer) {
  return !BoardConfig::hasTouch() && !UITheme::getInstance().hasCoverGridHome() &&
         std::min(renderer.getScreenWidth(), renderer.getScreenHeight()) == HOME_DASHBOARD_WIDTH;
}

bool homePagesSupported(const GfxRenderer& renderer) {
  return !BoardConfig::hasTouch() && !UITheme::getInstance().hasCoverGridHome() &&
         renderer.getScreenWidth() >= HOME_DASHBOARD_WIDTH && renderer.getScreenHeight() > renderer.getScreenWidth();
}

const char* homeBookCountWord(const uint32_t count) {
  if (I18N.getLanguage() == Language::RU) {
    const uint32_t lastTwo = count % 100;
    const uint32_t last = count % 10;
    if (lastTwo >= 11 && lastTwo <= 14) return tr(STR_BOOKS_MANY);
    if (last == 1) return tr(STR_BOOK_ONE);
    return last >= 2 && last <= 4 ? tr(STR_BOOKS_FEW) : tr(STR_BOOKS_MANY);
  }
  return count == 1 ? tr(STR_BOOK_ONE) : tr(STR_BOOKS_MANY);
}

int parseHomeLayout(const char* value, HomeBlock out[HOME_BLOCK_COUNT]) {
  static constexpr struct {
    const char* id;
    HomeBlock block;
  } NAMES[HOME_BLOCK_COUNT] = {{"calendar", HomeBlock::Calendar},
                               {"current", HomeBlock::Current},
                               {"finished", HomeBlock::Finished},
                               {"menu", HomeBlock::Menu}};

  int count = 0;
  const auto contains = [&](const HomeBlock block) { return std::find(out, out + count, block) != out + count; };

  if (value == nullptr || value[0] == '\0') {
    for (const auto& name : NAMES) out[count++] = name.block;
    return count;
  }

  const char* cursor = value;
  while (*cursor != '\0') {
    const char* end = strchr(cursor, ',');
    const size_t length = end ? static_cast<size_t>(end - cursor) : strlen(cursor);
    for (const auto& name : NAMES) {
      if (strlen(name.id) == length && strncmp(cursor, name.id, length) == 0 && !contains(name.block)) {
        out[count++] = name.block;
      }
    }
    cursor += length + (end ? 1 : 0);
  }
  if (!contains(HomeBlock::Menu)) out[count++] = HomeBlock::Menu;
  return count;
}

void drawHomeCover(const GfxRenderer& renderer, const RecentBook& book, const int height, const int x, const int y,
                   const int width) {
  drawThumb(renderer, book, height, x, y, width);
}

void drawHomeDashboard(const GfxRenderer& renderer, const HomeDashboardProps& props) {
  const int bottom = renderer.getScreenHeight() - BOTTOM_RESERVE;
  int contentHeight = 0;
  for (int i = 0; i < props.blockCount; i++) {
    if (props.blocks[i] == HomeBlock::Calendar) contentHeight += CALENDAR_HEIGHT;
    if (props.blocks[i] == HomeBlock::Current) contentHeight += CURRENT_HEIGHT;
    if (props.blocks[i] == HomeBlock::Finished) contentHeight += FINISHED_HEIGHT;
    if (props.blocks[i] == HomeBlock::Menu) contentHeight += menuHeight(props.menuCount);
  }
  // Spare height goes between the blocks so the screen stays evenly filled.
  const int gaps = props.blockCount - 1;
  const int spare = gaps > 0 ? (bottom - TOP - contentHeight - BLOCK_GAP * gaps) / gaps : 0;
  const int blockGap = BLOCK_GAP + std::clamp(spare, 0, MAX_EXTRA_BLOCK_GAP);

  int y = TOP;
  for (int i = 0; i < props.blockCount; i++) {
    switch (props.blocks[i]) {
      case HomeBlock::Calendar:
        drawCalendar(renderer, y);
        y += CALENDAR_HEIGHT;
        break;
      case HomeBlock::Current:
        drawCurrent(renderer, props, y);
        y += CURRENT_HEIGHT;
        break;
      case HomeBlock::Finished:
        drawFinished(renderer, props, y);
        y += FINISHED_HEIGHT;
        break;
      case HomeBlock::Menu:
        drawMenu(renderer, props, y);
        y += menuHeight(props.menuCount);
        break;
    }
    y += blockGap;
  }
}
