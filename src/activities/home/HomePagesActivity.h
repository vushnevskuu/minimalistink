#pragma once

#include <atomic>
#include <string>
#include <vector>

#include "RecentBooksStore.h"
#include "ShelfFile.h"
#include "activities/Activity.h"

// Home split into full screens: the book in progress, a bookshelf, and reading
// statistics whose bottom buttons act directly as the menu.
class HomePagesActivity final : public Activity {
  enum Page { PAGE_READING = 0, PAGE_SHELF, PAGE_STATISTICS, PAGE_COUNT };

  int page = PAGE_READING;
  bool cleanInitialRefresh = false;
  bool firstRenderDone = false;
  bool coversPending = true;
  bool hasOpdsServers = false;
  bool hasPlugins = false;
  bool ignoreBackRelease = false;
  bool ignoreConfirmRelease = false;

  std::vector<RecentBook> currentBook;
  int currentProgress = -1;
  std::vector<std::string> excerptLines;

  shelffile::Window shelf;
  int shelfIndex = 0;
  bool shelfPageOpened = false;
  bool libraryIndexChecked = false;
  bool noBooks = false;
  bool libraryReadable = true;
  std::vector<RecentBook> shelfBooks;
  std::vector<std::string> shelfFinished;
  std::vector<bool> shelfMissing;
  int shelfTotal = 0;
  int shelfSelected = 0;
  int shelfWindowStart = -1;

  // Frames of the cover sliding away; negative when not opening a book.
  int openFrame = -1;
  std::atomic<bool> openBookNow{false};

  void loadCurrentBook();
  void loadShelfWindow(int start);
  void prepareCovers();
  void openShelfSelection();
  void openLibrarySlot();
  void moveShelf(int delta);
  void nextShelf();
  bool isAllBooks() const { return shelfIndex == shelf.shelfCount; }
  int firstPage() const { return currentBook.empty() ? PAGE_SHELF : PAGE_READING; }
  void refreshLibraryIndex();
  void openShelfPage();
  void loadLibraryWindow(int start);
  void changePage(int delta);

  void renderReadingPage(int yOffset) const;
  void renderShelfPage() const;
  void renderStatisticsPage() const;
  void drawPageDots() const;

 public:
  HomePagesActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, HomeMenuItem initialMenuItem,
                    bool cleanInitialRefresh)
      : Activity("Home", renderer, mappedInput),
        page(initialMenuItem == HomeMenuItem::NONE ? PAGE_READING : PAGE_STATISTICS),
        cleanInitialRefresh(cleanInitialRefresh) {}
  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool isHomeActivity() const override { return true; }
};
