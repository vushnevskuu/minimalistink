#include "FinishedBooksStore.h"

#include <algorithm>

void FinishedBooksStore::toJson(JsonDocument& doc) const {
  doc["total"] = total;
  JsonArray arr = doc["books"].to<JsonArray>();
  for (const auto& book : books) {
    JsonObject obj = arr.add<JsonObject>();
    obj["path"] = book.path;
    obj["title"] = book.title;
    obj["author"] = book.author;
    obj["coverBmpPath"] = book.coverBmpPath;
  }
}

bool FinishedBooksStore::fromJson(JsonVariantConst doc) {
  books.clear();
  total = doc["total"] | 0u;
  for (JsonObjectConst obj : doc["books"].as<JsonArrayConst>()) {
    if (books.size() >= MAX_SHELF_BOOKS) break;
    RecentBook book;
    book.path = obj["path"] | "";
    book.title = obj["title"] | "";
    book.author = obj["author"] | "";
    book.coverBmpPath = obj["coverBmpPath"] | "";
    books.push_back(std::move(book));
  }
  return true;
}

void FinishedBooksStore::addBook(const std::string& path, const std::string& title, const std::string& author,
                                 const std::string& coverBmpPath) {
  RecentBook book{path, title, author, coverBmpPath};
  const auto existing = std::find(books.begin(), books.end(), book);
  if (existing == books.end()) {
    total++;
  } else {
    // Finishing again restores a cover lost to a failed generation.
    if (existing == books.begin()) {
      if (existing->coverBmpPath == coverBmpPath) return;
      existing->coverBmpPath = coverBmpPath;
      saveToFile();
      return;
    }
    books.erase(existing);
  }
  books.insert(books.begin(), std::move(book));
  if (books.size() > MAX_SHELF_BOOKS) books.pop_back();
  saveToFile();
}

void FinishedBooksStore::clearCover(const std::string& path) {
  for (auto& book : books) {
    if (book.path != path) continue;
    book.coverBmpPath.clear();
    saveToFile();
    return;
  }
}

void FinishedBooksStore::updatePath(const std::string& oldPath, const std::string& newPath,
                                    const std::string& oldCachePath, const std::string& newCachePath) {
  for (auto& book : books) {
    if (book.path != oldPath) continue;
    book.path = newPath;
    if (book.coverBmpPath.rfind(oldCachePath, 0) == 0) {
      book.coverBmpPath = newCachePath + book.coverBmpPath.substr(oldCachePath.size());
    }
    saveToFile();
    return;
  }
}
