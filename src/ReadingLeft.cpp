#include "ReadingLeft.h"

#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>

namespace {
constexpr char RECORD_PATH[] = "/.crosspoint/reading_left.bin";
constexpr char EXCERPT_PATH[] = "/.crosspoint/reading_excerpt.bin";
constexpr size_t MAX_EXCERPT_BYTES = 1024;
constexpr uint32_t RECORD_MAGIC = 0x314C5443;  // "CTL1"
// Starting pace, weighted as this many pages so early sessions cannot swing it.
constexpr uint32_t PRIOR_SECONDS_PER_PAGE = 90;
constexpr uint32_t PRIOR_PAGES = 20;
constexpr uint32_t PACE_SECONDS_LIMIT = 1u << 30;

struct Record {
  uint32_t magic = RECORD_MAGIC;
  uint32_t pathHash = 0;
  uint32_t paceSeconds = 0;
  uint32_t pacePages = 0;
  uint32_t pagesLeft = 0;
};

// FNV-1a; the record only has to tell the last book from any other.
uint32_t hashPath(const char* path) {
  uint32_t hash = 2166136261u;
  for (; *path != '\0'; path++) {
    hash ^= static_cast<uint8_t>(*path);
    hash *= 16777619u;
  }
  return hash;
}

bool load(Record& record) {
  if (!Storage.exists(RECORD_PATH)) return false;
  HalFile file;
  if (!Storage.openFileForRead("LEFT", RECORD_PATH, file)) return false;
  Record stored;
  if (file.read(reinterpret_cast<uint8_t*>(&stored), sizeof(stored)) != static_cast<int>(sizeof(stored))) return false;
  if (stored.magic != RECORD_MAGIC) return false;
  record = stored;
  return true;
}
}  // namespace

void readingleft::record(const char* bookPath, const int pagesLeft, const uint32_t sessionSeconds,
                         const uint32_t sessionPages) {
  Record record;
  load(record);
  record.paceSeconds += sessionSeconds;
  record.pacePages += sessionPages;
  if (record.paceSeconds >= PACE_SECONDS_LIMIT) {
    record.paceSeconds /= 2;
    record.pacePages /= 2;
  }
  if (pagesLeft >= 0) {
    record.pathHash = hashPath(bookPath);
    record.pagesLeft = static_cast<uint32_t>(pagesLeft);
  }

  HalFile file;
  if (!Storage.openFileForWrite("LEFT", RECORD_PATH, file) ||
      file.write(reinterpret_cast<const uint8_t*>(&record), sizeof(record)) != sizeof(record)) {
    LOG_ERR("LEFT", "Could not write record");
  }
}

bool readingleft::estimate(const char* bookPath, Estimate& out) {
  Record record;
  if (!load(record) || record.pathHash != hashPath(bookPath) || record.pagesLeft == 0) return false;
  const uint32_t secondsPerPage =
      (record.paceSeconds + PRIOR_SECONDS_PER_PAGE * PRIOR_PAGES) / (record.pacePages + PRIOR_PAGES);
  out.pagesLeft = record.pagesLeft;
  out.secondsLeft = record.pagesLeft * secondsPerPage;
  return true;
}

void readingleft::saveExcerpt(const char* bookPath, const std::string& text) {
  HalFile file;
  if (!Storage.openFileForWrite("LEFT", EXCERPT_PATH, file)) return;
  const uint32_t hash = hashPath(bookPath);
  file.write(reinterpret_cast<const uint8_t*>(&hash), sizeof(hash));
  file.write(reinterpret_cast<const uint8_t*>(text.data()), std::min(text.size(), MAX_EXCERPT_BYTES));
}

bool readingleft::loadExcerpt(const char* bookPath, std::string& out) {
  if (!Storage.exists(EXCERPT_PATH)) return false;
  HalFile file;
  if (!Storage.openFileForRead("LEFT", EXCERPT_PATH, file)) return false;
  uint32_t hash = 0;
  if (file.read(reinterpret_cast<uint8_t*>(&hash), sizeof(hash)) != static_cast<int>(sizeof(hash)) ||
      hash != hashPath(bookPath)) {
    return false;
  }
  out.resize(MAX_EXCERPT_BYTES);
  const int bytes = file.read(reinterpret_cast<uint8_t*>(&out[0]), MAX_EXCERPT_BYTES);
  out.resize(bytes > 0 ? static_cast<size_t>(bytes) : 0);
  return !out.empty();
}
