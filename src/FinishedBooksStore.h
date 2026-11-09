#pragma once
#include <ArduinoJson.h>
#include <PersistableStore.h>

#include <string>
#include <vector>

#include "RecentBooksStore.h"

// The latest finished books for the home screen shelf, plus a lifetime count.
class FinishedBooksStore : public PersistableStore<FinishedBooksStore> {
 public:
  static constexpr int MAX_SHELF_BOOKS = 4;

 private:
  std::vector<RecentBook> books;
  uint32_t total = 0;

  FinishedBooksStore() = default;
  ~FinishedBooksStore() = default;

  friend class PersistableStore<FinishedBooksStore>;

 public:
  static const char* getFilePath() { return "/.crosspoint/finished.json"; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);

  // Counts finish events; a book still on the shelf moves to the front without counting again.
  void addBook(const std::string& path, const std::string& title, const std::string& author,
               const std::string& coverBmpPath);
  void clearCover(const std::string& path);
  void updatePath(const std::string& oldPath, const std::string& newPath, const std::string& oldCachePath,
                  const std::string& newCachePath);

  const std::vector<RecentBook>& getBooks() const { return books; }
  uint32_t getTotal() const { return total; }
};

#define FINISHED_BOOKS FinishedBooksStore::getInstance()
