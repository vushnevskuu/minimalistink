#include "ReadingDays.h"

#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <cstdio>
#include <ctime>

namespace {
constexpr char DAYS_DIR[] = "/.crosspoint/reading_days";
constexpr size_t SLOT_BYTES = sizeof(uint16_t);
constexpr int FILE_DAYS = 366;
constexpr size_t FILE_BYTES = FILE_DAYS * SLOT_BYTES;
constexpr uint32_t MAX_DAY_SECONDS = UINT16_MAX;
constexpr uint32_t SECONDS_PER_DAY = 86400;

void yearPath(char* out, const size_t size, const int year) { snprintf(out, size, "%s/%d.bin", DAYS_DIR, year); }

// Every day needs a slot to seek to, so short files are zero-filled first.
bool ensureFullSize(HalFile& file) {
  size_t size = file.size();
  if (size >= FILE_BYTES) return true;
  if (!file.seek(size)) return false;
  while (size < FILE_BYTES) {
    if (file.write(static_cast<uint8_t>(0)) != 1) return false;
    size++;
  }
  return true;
}

void creditDay(const struct tm& local, const uint32_t seconds) {
  char path[48];
  yearPath(path, sizeof(path), local.tm_year + 1900);
  if (!Storage.exists(DAYS_DIR)) Storage.mkdir(DAYS_DIR);

  HalFile file = Storage.open(path, O_RDWR | O_CREAT);
  if (!file || !ensureFullSize(file)) {
    LOG_ERR("DAYS", "Could not prepare %s", path);
    return;
  }

  const size_t offset = static_cast<size_t>(local.tm_yday) * SLOT_BYTES;
  uint8_t slot[SLOT_BYTES] = {};
  if (!file.seek(offset) || file.read(slot, SLOT_BYTES) != static_cast<int>(SLOT_BYTES)) {
    LOG_ERR("DAYS", "Could not read day %d", local.tm_yday);
    return;
  }

  const uint32_t stored = static_cast<uint32_t>(slot[0]) | (static_cast<uint32_t>(slot[1]) << 8);
  const uint32_t total = std::min(stored + seconds, MAX_DAY_SECONDS);
  slot[0] = static_cast<uint8_t>(total & 0xFF);
  slot[1] = static_cast<uint8_t>(total >> 8);
  if (!file.seek(offset) || file.write(slot, SLOT_BYTES) != SLOT_BYTES) {
    LOG_ERR("DAYS", "Could not write day %d", local.tm_yday);
  }
}
}  // namespace

void readingdays::add(const int64_t startEpoch, const uint32_t seconds) {
  if (startEpoch <= 0) return;

  time_t start = static_cast<time_t>(startEpoch);
  uint32_t remaining = seconds;
  while (remaining > 0) {
    struct tm local{};
    localtime_r(&start, &local);
    const uint32_t untilMidnight = SECONDS_PER_DAY - (local.tm_hour * 3600 + local.tm_min * 60 + local.tm_sec);
    const uint32_t part = std::min(remaining, untilMidnight);
    creditDay(local, part);
    start += part;
    remaining -= part;
  }
}

void readingdays::forEachDay(const int year, const int days, const DayVisitor visit, void* context) {
  char path[48];
  yearPath(path, sizeof(path), year);
  HalFile file;
  const bool hasFile = Storage.exists(path) && Storage.openFileForRead("DAYS", path, file);
  for (int day = 0; day < days; day++) {
    uint8_t slot[SLOT_BYTES] = {};
    if (hasFile) file.read(slot, SLOT_BYTES);
    visit(context, day, static_cast<uint16_t>(slot[0] | (slot[1] << 8)));
  }
}
