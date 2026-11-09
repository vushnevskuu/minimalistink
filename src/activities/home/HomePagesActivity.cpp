#include "HomePagesActivity.h"

#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalMemory.h>
#include <HalStorage.h>
#include <I18n.h>
#include <LibraryBuilder.h>
#include <LibraryIndexFile.h>
#include <Memory.h>
#include <TrustedTime.h>
#include <Xtc.h>

#include <algorithm>
#include <cstdio>
#include <ctime>

#include "CrossPointSettings.h"
#include "FinishedBooksStore.h"
#include "MappedInputManager.h"
#include "OpdsServerStore.h"
#include "ReadingDays.h"
#include "ReadingLeft.h"
#include "ShelfFile.h"
#include "components/HomeDashboard.h"
#include "components/ReadingMapDraw.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/BookProgress.h"

namespace {
constexpr int MARGIN = 28;
constexpr int HINTS_TOP = 764;
constexpr int COVER_WIDTH = 176;
constexpr int COVER_HEIGHT = 264;
constexpr int SHELF_COLUMNS = 2;
constexpr int SHELF_PAGE_BOOKS = 4;
constexpr int SHELF_COVER_WIDTH = 206;
constexpr int SHELF_COVER_HEIGHT = 292;
constexpr int SHELF_GAP_X = 12;
constexpr int SHELF_ROW_PITCH = 344;
constexpr int SHELF_FRAME_RADIUS = 8;
constexpr size_t MIN_HEAP_TO_JUDGE_COVER = 80 * 1024;
constexpr int ANIMATION_STEP = 150;
constexpr int ANIMATION_FRAMES = 2;
#ifdef OMIT_FONTS
constexpr int EXCERPT_FONT = UI_10_FONT_ID;
constexpr int FIGURE_FONT = UI_12_FONT_ID;
#else
constexpr int EXCERPT_FONT = NOTOSANS_14_FONT_ID;
constexpr int FIGURE_FONT = NOTOSANS_18_FONT_ID;
#endif

constexpr StrId MONTH_NAMES[12] = {StrId::STR_MONTH_JAN, StrId::STR_MONTH_FEB, StrId::STR_MONTH_MAR,
                                   StrId::STR_MONTH_APR, StrId::STR_MONTH_MAY, StrId::STR_MONTH_JUN,
                                   StrId::STR_MONTH_JUL, StrId::STR_MONTH_AUG, StrId::STR_MONTH_SEP,
                                   StrId::STR_MONTH_OCT, StrId::STR_MONTH_NOV, StrId::STR_MONTH_DEC};

// Rings when `thickness` is positive, solid otherwise. Coordinates are doubled so even sizes center exactly.
void drawDisk(const GfxRenderer& renderer, const int x, const int y, const int size, const int thickness) {
  const int outer = size * size;
  const int innerRadius = size - 2 * thickness;
  const int inner = thickness > 0 ? innerRadius * innerRadius : -1;
  for (int py = 0; py < size; py++) {
    for (int px = 0; px < size; px++) {
      const int dx = 2 * px + 1 - size;
      const int dy = 2 * py + 1 - size;
      const int distance = dx * dx + dy * dy;
      if (distance < outer && distance >= inner) renderer.drawPixel(x + px, y + py);
    }
  }
}

struct YearStats {
  int dayStart[13] = {};
  int firstMonth = -1;
  int today = -1;
  int month = 0;
  uint32_t monthSeconds[12] = {};
  uint32_t totalSeconds = 0;
  int daysRead = 0;
  int streak = 0;
};

void collectYearDay(void* context, const int day, const uint16_t seconds) {
  auto& stats = *static_cast<YearStats*>(context);
  while (stats.month < 11 && day >= stats.dayStart[stats.month + 1]) stats.month++;
  if (day > stats.today) return;
  if (seconds > 0) {
    if (stats.firstMonth < 0) stats.firstMonth = stats.month;
    stats.monthSeconds[stats.month] += seconds;
    stats.totalSeconds += seconds;
    stats.daysRead++;
    stats.streak++;
  } else if (day < stats.today) {
    stats.streak = 0;
  }
}

struct MonthMapContext {
  const GfxRenderer& renderer;
  const YearStats& stats;
  int month = 0;
  int firstTop;
  int blockPitch;
  int perLine;
  int pitch;
  int cell;
};

void drawMonthMapDay(void* contextPtr, const int day, const uint16_t seconds) {
  auto& c = *static_cast<MonthMapContext*>(contextPtr);
  while (c.month < 11 && day >= c.stats.dayStart[c.month + 1]) c.month++;
  if (c.month < c.stats.firstMonth || day > c.stats.today) return;

  const int dayOfMonth = day - c.stats.dayStart[c.month];
  const int x = MARGIN + (dayOfMonth % c.perLine) * c.pitch;
  const int y = c.firstTop + (c.month - c.stats.firstMonth) * c.blockPitch + (dayOfMonth / c.perLine) * c.pitch;
  const int radius = std::max(1, c.cell / 4);

  if (day == c.stats.today) {
    drawDisk(c.renderer, x, y, c.cell, 2);
    return;
  }
  const int level = readingMapLevel(seconds);
  if (level >= 3) {
    c.renderer.fillRoundedRect(x, y, c.cell, c.cell, radius, Color::Black);
  } else if (level >= 1) {
    const int inset = c.cell / 3;
    drawDisk(c.renderer, x + inset, y + inset, c.cell - 2 * inset, 0);
  } else {
    c.renderer.fillRect(x + c.cell / 2 - 1, y + c.cell / 2 - 1, 2, 2);
  }
}

// "2026-10" becomes "Oct 2026"; anything else leaves out empty.
void formatFinishedMonth(const std::string& finished, char* out, const size_t size) {
  if (finished.size() != 7 || finished[4] != '-') return;
  for (const int i : {0, 1, 2, 3, 5, 6}) {
    if (finished[i] < '0' || finished[i] > '9') return;
  }
  const int month = (finished[5] - '0') * 10 + (finished[6] - '0');
  if (month < 1 || month > 12) return;
  snprintf(out, size, "%s %.4s", I18N.get(MONTH_NAMES[month - 1]), finished.c_str());
}

// Empty thumbs mark books with no usable cover, as the reader's own cover code does.
// Fills the thumbnail path template; constructors only, so nothing is parsed.
void resolveThumbPath(RecentBook& book) {
  if (!book.coverBmpPath.empty()) return;
  if (FsHelpers::hasReflowableBookExtension(book.path)) {
    if (auto epub = makeUniqueNoThrow<Epub>(book.path, "/.crosspoint")) book.coverBmpPath = epub->getThumbBmpPath();
  } else if (FsHelpers::hasXtcExtension(book.path)) {
    if (auto xtc = makeUniqueNoThrow<Xtc>(book.path, "/.crosspoint")) book.coverBmpPath = xtc->getThumbBmpPath();
  }
}

// Returns true when a thumbnail was generated, so the screen needs a redraw.
bool ensureThumb(RecentBook& book, const int height) {
  if (book.coverBmpPath.empty()) return false;
  const std::string thumb = UITheme::getCoverThumbPath(book.coverBmpPath, height);
  if (Storage.exists(thumb.c_str())) return false;

  bool created = false;
  if (FsHelpers::hasReflowableBookExtension(book.path)) {
    auto epub = makeUniqueNoThrow<Epub>(book.path, "/.crosspoint");
    if (!epub) return false;
    created = epub->generateThumbBmpFromSource(height);
  } else if (FsHelpers::hasXtcExtension(book.path)) {
    auto xtc = makeUniqueNoThrow<Xtc>(book.path, "/.crosspoint");
    if (!xtc) return false;
    created = xtc->load() && xtc->generateThumbBmp(height);
  }
  if (!created) {
    // Running short of memory says nothing about the book, so try again on a later visit.
    if (HalMemory::getInternalHeap().largestBlockBytes < MIN_HEAP_TO_JUDGE_COVER) {
      book.coverBmpPath.clear();
      return true;
    }
    // A book never opened has no cache folder yet. Without a marker, stop asking for this screen's lifetime.
    Storage.mkdir(thumb.substr(0, thumb.rfind('/')).c_str());
    HalFile marker;
    if (!Storage.openFileForWrite("HOME", thumb, marker)) book.coverBmpPath.clear();
  }
  return true;
}
}  // namespace

namespace {
// Keep the index and record together off the task stack.
struct LibraryReader {
  library::LibraryIndexFile index;
  library::ClixRecord record;
};

// Reads one row, newest first. False when the row cannot be read.
bool readLibraryRow(LibraryReader& reader, const int row, RecentBook& book) {
  if (!reader.index.readRecord(reader.index.ordinalForRow(library::SortOrder::RecentDesc, row), reader.record) ||
      !reader.index.readPath(reader.record, book.path)) {
    return false;
  }
  if (!reader.index.readTitle(reader.record, book.title)) reader.index.readName(reader.record, book.title);
  reader.index.readAuthor(reader.record, book.author);
  return true;
}
}  // namespace

void HomePagesActivity::loadCurrentBook() {
  currentBook.clear();
  excerptLines.clear();
  for (const RecentBook& book : RECENT_BOOKS.getBooks()) {
    if (RecentBooksStore::isMissing(book)) continue;
    currentBook.push_back(book);
    break;
  }
  if (currentBook.empty()) return;
  currentProgress = loadBookProgress(currentBook[0].path);

  std::string excerpt;
  if (!readingleft::loadExcerpt(currentBook[0].path.c_str(), excerpt)) return;
  const int pageWidth = renderer.getScreenWidth();
  const int maxWidth = pageWidth - 2 * MARGIN;
  const int maxLines = (HINTS_TOP - 400) / renderer.getLineHeight(EXCERPT_FONT);
  std::string line;
  size_t cursor = 0;
  while (cursor < excerpt.size() && static_cast<int>(excerptLines.size()) < maxLines) {
    size_t end = excerpt.find(' ', cursor);
    if (end == std::string::npos) end = excerpt.size();
    const std::string word = excerpt.substr(cursor, end - cursor);
    cursor = end + 1;
    const std::string trial = line.empty() ? word : line + " " + word;
    if (renderer.getTextWidth(EXCERPT_FONT, trial.c_str()) <= maxWidth) {
      line = trial;
    } else {
      excerptLines.push_back(line);
      line = word;
    }
  }
  if (!line.empty() && static_cast<int>(excerptLines.size()) < maxLines) excerptLines.push_back(line);
}

// The caller holds RenderLock: the walk needs the card to itself while fonts may read glyphs from it.
void HomePagesActivity::refreshLibraryIndex() {
  if (libraryIndexChecked) return;
  libraryIndexChecked = true;

  auto index = makeUniqueNoThrow<library::LibraryIndexFile>();
  if (!index) return;
  const bool readMetadata = SETTINGS.libraryUseMetadata != 0;
  // An empty index is rebuilt too: books copied on a computer leave no dirty marker.
  if (!library::isLibraryIndexDirty() && index->open(library::libraryIndexPath()) &&
      index->header().metadataEnabled == readMetadata && index->bookCount() > 0) {
    return;
  }
  index.reset();
  GUI.drawPopup(renderer, tr(STR_LIBRARY_REBUILDING));
  library::BuildStats stats;
  if (!library::buildLibraryIndex("/", stats, readMetadata)) LOG_ERR("HOME", "Library index build failed");
}

void HomePagesActivity::loadLibraryWindow(const int start) {
  shelfTotal = 0;
  auto reader = makeUniqueNoThrow<LibraryReader>();
  libraryReadable = reader && reader->index.open(library::libraryIndexPath());
  if (!libraryReadable) return;

  shelfTotal = reader->index.bookCount();
  const int end = std::min(start + SHELF_PAGE_BOOKS, shelfTotal);
  for (int row = start; row < end; row++) {
    RecentBook book;
    const bool missing = !readLibraryRow(*reader, row, book) || RecentBooksStore::isMissing(book);
    if (!missing) {
      resolveThumbPath(book);
    } else if (!reader->index.ioFailed()) {
      library::markLibraryIndexDirty();
    }
    shelfMissing.push_back(missing);
    shelfBooks.push_back(std::move(book));
    shelfFinished.emplace_back();
  }
}

void HomePagesActivity::openShelfPage() {
  if (!shelfPageOpened) {
    shelfPageOpened = true;
    shelffile::load(0, 0, 0, shelf);
    shelfIndex = shelf.firstFilledShelf >= 0 ? shelf.firstFilledShelf : shelf.shelfCount;
  }
  loadShelfWindow(shelfSelected - shelfSelected % SHELF_PAGE_BOOKS);
}

void HomePagesActivity::loadShelfWindow(int start) {
  // Free the old screen first so two screens of book data never share the heap.
  shelfBooks.clear();
  shelfFinished.clear();
  shelfMissing.clear();
  shelfBooks.reserve(SHELF_PAGE_BOOKS);
  shelfFinished.reserve(SHELF_PAGE_BOOKS);
  shelfMissing.reserve(SHELF_PAGE_BOOKS);

  // All books needs nothing from the shelf file, whose counts cannot change while Home is open.
  if (!isAllBooks()) {
    shelffile::load(shelfIndex, start, SHELF_PAGE_BOOKS, shelf);
    if (shelfIndex > shelf.shelfCount) {
      shelfIndex = 0;
      shelfSelected = 0;
      start = 0;
      shelffile::load(0, 0, SHELF_PAGE_BOOKS, shelf);
    }
  }

  shelfWindowStart = start;
  if (isAllBooks()) {
    refreshLibraryIndex();
    return loadLibraryWindow(start);
  }

  shelfTotal = shelf.itemCount;
  for (shelffile::Item& item : shelf.items) {
    RecentBook book;
    book.path = std::move(item.path);
    book.title = std::move(item.title);
    book.author = std::move(item.author);
    const bool missing = book.path.empty() || RecentBooksStore::isMissing(book);
    if (!missing) resolveThumbPath(book);
    shelfMissing.push_back(missing);
    shelfBooks.push_back(std::move(book));
    shelfFinished.push_back(std::move(item.finished));
  }
  shelf.items.clear();
}

void HomePagesActivity::onEnter() {
  Activity::onEnter();
  hasOpdsServers = OPDS_STORE.hasServers();
  hasPlugins = SETTINGS.pluginsEnabled != 0;
  // Hold the lock across the decision so the first frame sees the final page.
  RenderLock lock;
  coversPending = true;  // a render queued during the screen change may have used this up
  loadCurrentBook();
  if (currentBook.empty()) {
    // Nothing opened yet, so there is no reading screen. With no books at all, only the menu is left.
    if (page == PAGE_READING) page = PAGE_SHELF;
    openShelfPage();
    if (isAllBooks() && shelfTotal == 0 && libraryReadable) {
      noBooks = true;
      page = PAGE_STATISTICS;
    }
  }
  lock.unlock();

  // A button still held from the previous screen must not trigger an action here.
  ignoreBackRelease = mappedInput.isPressed(MappedInputManager::Button::Back);
  ignoreConfirmRelease = mappedInput.isPressed(MappedInputManager::Button::Confirm);
  requestUpdate();
}

void HomePagesActivity::prepareCovers() {
  // One book per pass so a page change never waits for a whole row of covers.
  coversPending = false;
  std::vector<RecentBook>* books = page == PAGE_READING ? &currentBook : page == PAGE_SHELF ? &shelfBooks : nullptr;
  if (!books) return;
  const int height = page == PAGE_READING ? COVER_HEIGHT : SHELF_COVER_HEIGHT;
  for (RecentBook& book : *books) {
    if (ensureThumb(book, height)) {
      coversPending = true;
      requestUpdate();
      return;
    }
  }
}

void HomePagesActivity::changePage(const int delta) {
  if (noBooks) return;
  RenderLock lock;
  const int first = firstPage();
  const int count = PAGE_COUNT - first;
  page = first + (page - first + count + delta) % count;
  coversPending = true;
  if (page == PAGE_SHELF) openShelfPage();
  lock.unlock();
  requestUpdate();
}

void HomePagesActivity::moveShelf(const int delta) {
  if (shelfTotal == 0) return;
  RenderLock lock;
  shelfSelected = (shelfSelected + shelfTotal + delta) % shelfTotal;
  const int windowStart = shelfSelected - shelfSelected % SHELF_PAGE_BOOKS;
  if (windowStart != shelfWindowStart) {
    loadShelfWindow(windowStart);
    coversPending = true;
  }
  lock.unlock();
  requestUpdate();
}

void HomePagesActivity::nextShelf() {
  RenderLock lock;
  shelfIndex = (shelfIndex + 1) % (shelf.shelfCount + 1);
  shelfSelected = 0;
  loadShelfWindow(0);
  coversPending = true;
  lock.unlock();
  requestUpdate();
}

void HomePagesActivity::openShelfSelection() {
  const int index = shelfSelected - shelfWindowStart;
  if (index < 0 || index >= static_cast<int>(shelfBooks.size())) return;
  if (!shelfMissing[index]) activityManager.goToReader(shelfBooks[index].path);
}

void HomePagesActivity::openLibrarySlot() {
  if (hasPlugins) {
    activityManager.goToPlugins(hasOpdsServers);
  } else if (hasOpdsServers) {
    activityManager.goToBrowser();
  }
}

void HomePagesActivity::loop() {
  if (openBookNow.load()) {
    activityManager.goToReader(currentBook[0].path);
    // Only reached with this screen still alive if the reader could not be created.
    openBookNow.store(false);
    openFrame = -1;
    requestUpdate();
    return;
  }
  if (openFrame >= 0) return;

  using Button = MappedInputManager::Button;
  if (ignoreBackRelease) {
    const bool released = mappedInput.wasReleased(Button::Back);
    if (released || !mappedInput.isPressed(Button::Back)) ignoreBackRelease = false;
    if (released) return;
  }
  if (ignoreConfirmRelease) {
    const bool released = mappedInput.wasReleased(Button::Confirm);
    if (released || !mappedInput.isPressed(Button::Confirm)) ignoreConfirmRelease = false;
    if (released) return;
  }
  if (mappedInput.wasPressed(Button::Down)) return changePage(1);
  if (mappedInput.wasPressed(Button::Up)) return changePage(-1);

  if (page == PAGE_READING) {
    if (mappedInput.wasReleased(Button::Confirm)) {
      RenderLock lock;
      openFrame = 0;
      lock.unlock();
      requestUpdate();
    }
  } else if (page == PAGE_SHELF) {
    if (mappedInput.wasPressed(Button::Left)) return moveShelf(-1);
    if (mappedInput.wasPressed(Button::Right)) return moveShelf(1);
    if (mappedInput.wasReleased(Button::Confirm)) return openShelfSelection();
    if (mappedInput.wasReleased(Button::Back)) return nextShelf();
  } else {
    if (mappedInput.wasReleased(Button::Back)) return activityManager.goToFileBrowser();
    if (mappedInput.wasReleased(Button::Confirm)) return activityManager.goToFileTransfer();
    if (mappedInput.wasReleased(Button::Left)) return activityManager.goToSettings();
    if (mappedInput.wasReleased(Button::Right)) return openLibrarySlot();
  }
}

void HomePagesActivity::drawPageDots() const {
  if (noBooks) return;
  const int centerX = renderer.getScreenWidth() / 2;
  const int first = firstPage();
  const int count = PAGE_COUNT - first;
  for (int i = first; i < PAGE_COUNT; i++) {
    const int x = centerX + (2 * (i - first) - (count - 1)) * 8;
    if (i == page) {
      drawDisk(renderer, x - 4, 16, 9, 0);
    } else {
      drawDisk(renderer, x - 3, 17, 7, 1);
    }
  }
}

void HomePagesActivity::renderReadingPage(const int yOffset) const {
  const int pageWidth = renderer.getScreenWidth();
  // A render already queued during the screen change can run before onEnter fills this.
  if (currentBook.empty()) return;
  const RecentBook& book = currentBook[0];
  if (yOffset == 0 || yOffset < 44 + COVER_HEIGHT) {
    drawHomeCover(renderer, book, COVER_HEIGHT, (pageWidth - COVER_WIDTH) / 2, 44 - yOffset, COVER_WIDTH);
  }

  const std::string title =
      renderer.truncatedText(UI_12_FONT_ID, book.title.c_str(), pageWidth - 2 * MARGIN, EpdFontFamily::BOLD);
  renderer.drawCenteredText(UI_12_FONT_ID, 318 - yOffset, title.c_str(), true, EpdFontFamily::BOLD);
  char meta[96];
  if (currentProgress < 0) {
    snprintf(meta, sizeof(meta), "%s", book.author.c_str());
  } else if (book.author.empty()) {
    snprintf(meta, sizeof(meta), "%d%%", currentProgress);
  } else {
    snprintf(meta, sizeof(meta), "%s · %d%%", book.author.c_str(), currentProgress);
  }
  const std::string metaFit = renderer.truncatedText(UI_10_FONT_ID, meta, pageWidth - 2 * MARGIN);
  renderer.drawCenteredText(UI_10_FONT_ID, 350 - yOffset, metaFit.c_str());
  renderer.drawLine(MARGIN, 386 - yOffset, pageWidth - MARGIN - 1, 386 - yOffset);

  int y = 398 - yOffset;
  const int lineHeight = renderer.getLineHeight(EXCERPT_FONT);
  for (const std::string& line : excerptLines) {
    renderer.drawText(EXCERPT_FONT, MARGIN, y, line.c_str());
    y += lineHeight;
  }
}

void HomePagesActivity::renderShelfPage() const {
  if (shelf.shelfCount == 0) return;
  const int pageWidth = renderer.getScreenWidth();
  const bool isReadShelf = !isAllBooks() && shelf.id == "read";
  const char* shelfName = isAllBooks()          ? tr(STR_ALL_BOOKS)
                          : !shelf.name.empty() ? shelf.name.c_str()
                          : isReadShelf         ? tr(STR_FINISHED)
                                                : tr(STR_SHELF_UNREAD);

  char range[32] = "";
  if (shelfTotal > 0) {
    const int firstShown = shelfWindowStart + 1;
    const int lastShown = shelfWindowStart + static_cast<int>(shelfBooks.size());
    if (firstShown == lastShown) {
      snprintf(range, sizeof(range), "%d / %d", firstShown, shelfTotal);
    } else {
      snprintf(range, sizeof(range), "%d-%d / %d", firstShown, lastShown, shelfTotal);
    }
    renderer.drawText(UI_12_FONT_ID, pageWidth - MARGIN - renderer.getTextWidth(UI_12_FONT_ID, range), 34, range);
  }
  const int nameWidth = pageWidth - 2 * MARGIN - renderer.getTextWidth(UI_12_FONT_ID, range) - 12;
  renderer.drawText(UI_12_FONT_ID, MARGIN, 34, renderer.truncatedText(UI_12_FONT_ID, shelfName, nameWidth).c_str());

  if (shelfTotal == 0) {
    for (int i = 0; i < SHELF_PAGE_BOOKS; i++) {
      const int x = MARGIN + (i % SHELF_COLUMNS) * (SHELF_COVER_WIDTH + SHELF_GAP_X);
      const int y = 72 + (i / SHELF_COLUMNS) * SHELF_ROW_PITCH;
      renderer.drawRoundedRect(x, y, SHELF_COVER_WIDTH, SHELF_COVER_HEIGHT, 1, SHELF_FRAME_RADIUS, true);
    }
    UITheme::drawCenteredWrappedText(renderer, Rect{MARGIN, 72 + SHELF_COVER_HEIGHT, pageWidth - 2 * MARGIN, 52},
                                     UI_10_FONT_ID, tr(STR_SHELF_EMPTY), 1);
    return;
  }

  for (int i = 0; i < static_cast<int>(shelfBooks.size()); i++) {
    const int x = MARGIN + (i % SHELF_COLUMNS) * (SHELF_COVER_WIDTH + SHELF_GAP_X);
    const int y = 72 + (i / SHELF_COLUMNS) * SHELF_ROW_PITCH;
    drawHomeCover(renderer, shelfBooks[i], SHELF_COVER_HEIGHT, x, y, SHELF_COVER_WIDTH);

    char note[24] = "";
    if (shelfMissing[i]) {
      snprintf(note, sizeof(note), "%s", tr(STR_SHELF_MISSING));
    } else if (isReadShelf) {
      formatFinishedMonth(shelfFinished[i], note, sizeof(note));
    }
    const int noteWidth = note[0] != '\0' ? renderer.getTextWidth(UI_10_FONT_ID, note) : 0;
    const int titleWidth = SHELF_COVER_WIDTH - (noteWidth > 0 ? noteWidth + 8 : 0);
    const std::string title =
        renderer.truncatedText(UI_10_FONT_ID, shelfBooks[i].title.c_str(), titleWidth, EpdFontFamily::BOLD);
    const int textY = y + SHELF_COVER_HEIGHT + 4;
    renderer.drawText(UI_10_FONT_ID, x, textY, title.c_str(), true, EpdFontFamily::BOLD);
    if (noteWidth > 0) renderer.drawText(UI_10_FONT_ID, x + SHELF_COVER_WIDTH - noteWidth, textY, note);
    if (shelfWindowStart + i == shelfSelected) {
      renderer.fillRect(x, y + SHELF_COVER_HEIGHT + 32, SHELF_COVER_WIDTH, 4);
    }
  }
}

void HomePagesActivity::renderStatisticsPage() const {
  const int pageWidth = renderer.getScreenWidth();
  const int64_t now = trustedtime::trustedNow();
  if (now == 0) {
    UITheme::drawCenteredWrappedText(renderer, Rect{MARGIN, 300, pageWidth - 2 * MARGIN, 120}, UI_12_FONT_ID,
                                     tr(STR_CONNECT_WIFI_FOR_DATE), 3);
    return;
  }
  const time_t nowTime = static_cast<time_t>(now);
  struct tm today{};
  localtime_r(&nowTime, &today);
  const int year = today.tm_year + 1900;
  const bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;

  YearStats stats;
  static constexpr int MONTH_DAYS[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  for (int month = 0; month < 12; month++) {
    stats.dayStart[month + 1] = stats.dayStart[month] + MONTH_DAYS[month] + (month == 1 && leap ? 1 : 0);
  }
  stats.today = today.tm_yday;
  readingdays::forEachDay(year, stats.dayStart[12], collectYearDay, &stats);
  if (stats.firstMonth < 0) stats.firstMonth = today.tm_mon;

  char label[24];
  snprintf(label, sizeof(label), "%d", year);
  renderer.drawText(UI_12_FONT_ID, MARGIN, 34, label);
  snprintf(label, sizeof(label), "%lu %s", static_cast<unsigned long>(stats.totalSeconds / 3600), tr(STR_HOURS_SHORT));
  renderer.drawText(UI_12_FONT_ID, pageWidth - MARGIN - renderer.getTextWidth(UI_12_FONT_ID, label), 34, label);

  // Two densities: half a month per line, or a whole month per line.
  constexpr int AREA = 612;
  constexpr int LABEL_HEIGHT = 24;
  const int monthCount = today.tm_mon - stats.firstMonth + 1;
  int perLine = 31;
  int gap = 14;
  int pitch = (pageWidth - 2 * MARGIN) / perLine;
  int total = 0;
  static constexpr struct {
    int perLine;
    int gap;
  } DENSITIES[] = {{16, 30}, {16, 14}, {31, 14}};
  for (const auto& density : DENSITIES) {
    perLine = density.perLine;
    gap = density.gap;
    pitch = (pageWidth - 2 * MARGIN) / perLine;
    total = monthCount * (LABEL_HEIGHT + ((31 + perLine - 1) / perLine) * pitch + gap) - gap;
    if (total <= AREA) break;
  }
  const int lines = (31 + perLine - 1) / perLine;
  const int blockPitch = LABEL_HEIGHT + lines * pitch + gap;
  const int mapTop = 84 + (AREA - total) / 2;

  for (int month = stats.firstMonth; month <= today.tm_mon; month++) {
    const int y = mapTop + (month - stats.firstMonth) * blockPitch;
    renderer.drawText(SMALL_FONT_ID, MARGIN, y, I18N.get(MONTH_NAMES[month]));
    if (month < today.tm_mon) {
      snprintf(label, sizeof(label), "%lu %s", static_cast<unsigned long>(stats.monthSeconds[month] / 3600),
               tr(STR_HOURS_SHORT));
      renderer.drawText(SMALL_FONT_ID, pageWidth - MARGIN - renderer.getTextWidth(SMALL_FONT_ID, label), y, label);
    }
  }

  MonthMapContext context{renderer,   stats,   0,     mapTop + LABEL_HEIGHT,
                          blockPitch, perLine, pitch, pitch - (perLine == 16 ? 6 : 2)};
  readingdays::forEachDay(year, stats.dayStart[12], drawMonthMapDay, &context);

  const uint32_t minutesPerDay = stats.daysRead > 0 ? stats.totalSeconds / 60 / stats.daysRead : 0;
  const struct {
    uint32_t value;
    const char* caption;
  } figures[] = {{static_cast<uint32_t>(stats.daysRead), tr(STR_DAYS_READ)},
                 {static_cast<uint32_t>(stats.streak), tr(STR_DAY_STREAK)},
                 {FINISHED_BOOKS.getTotal(), homeBookCountWord(FINISHED_BOOKS.getTotal())},
                 {minutesPerDay, tr(STR_MIN_A_DAY)}};
  const int columnWidth = (pageWidth - 2 * MARGIN) / 4;
  for (int i = 0; i < 4; i++) {
    const int center = MARGIN + columnWidth * i + columnWidth / 2;
    snprintf(label, sizeof(label), "%lu", static_cast<unsigned long>(figures[i].value));
    renderer.drawText(FIGURE_FONT, center - renderer.getTextWidth(FIGURE_FONT, label, EpdFontFamily::BOLD) / 2, 686,
                      label, true, EpdFontFamily::BOLD);
    renderer.drawText(SMALL_FONT_ID, center - renderer.getTextWidth(SMALL_FONT_ID, figures[i].caption) / 2, 738,
                      figures[i].caption);
  }
}

void HomePagesActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  renderer.clearScreen();

  const int yOffset = openFrame > 0 ? openFrame * ANIMATION_STEP : 0;
  if (page == PAGE_READING) {
    renderReadingPage(yOffset);
  } else if (page == PAGE_SHELF) {
    renderShelfPage();
  } else {
    renderStatisticsPage();
  }

  // Content that slid up must not run under the status row or the buttons.
  renderer.fillRect(0, 0, pageWidth, 30, false);
  renderer.fillRect(0, HINTS_TOP, pageWidth, renderer.getScreenHeight() - HINTS_TOP, false);
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, 24}, nullptr, nullptr, false);
  drawPageDots();

  const char* slotLabel = hasPlugins ? tr(STR_PLUGINS) : hasOpdsServers ? tr(STR_OPDS_BROWSER) : "";
  MappedInputManager::Labels labels{};
  if (page == PAGE_READING) {
    labels = mappedInput.mapLabels("", tr(STR_RESUME), "", "");
  } else if (page == PAGE_SHELF) {
    const int selected = shelfSelected - shelfWindowStart;
    const bool canOpen = selected >= 0 && selected < static_cast<int>(shelfBooks.size()) && !shelfMissing[selected];
    const bool hasBooks = shelfTotal > 0;
    labels = mappedInput.mapLabels(tr(STR_SHELVES), canOpen ? tr(STR_OPEN) : "", hasBooks ? tr(STR_DIR_UP) : "",
                                   hasBooks ? tr(STR_DIR_DOWN) : "");
  } else {
    labels = mappedInput.mapLabels(tr(STR_FILES_SHORT), tr(STR_HINT_TRANSFER), tr(STR_HINT_SETTINGS), slotLabel);
  }
  if (openFrame < 0) GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer(cleanInitialRefresh && !firstRenderDone ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH);

  if (openFrame >= 0) {
    if (openFrame < ANIMATION_FRAMES) {
      openFrame++;
      requestUpdate();
    } else {
      openBookNow.store(true);
    }
    return;
  }
  firstRenderDone = true;
  if (coversPending) prepareCovers();
}
