#include "ReaderPlaceRequest.h"

#include <HalStorage.h>

#include <cstdio>
#include <cstdlib>

namespace {
constexpr char REQUEST_PATH[] = "/.crosspoint/open_request.txt";
constexpr size_t MAX_REQUEST_BYTES = 1024;
}  // namespace

bool placerequest::save(const char* bookPath, const int spine, const int occurrence, const char* query) {
  HalFile file;
  if (!Storage.openFileForWrite("PLACE", REQUEST_PATH, file)) return false;
  char numbers[32];
  const int length = snprintf(numbers, sizeof(numbers), "\n%d\n%d\n", spine, occurrence);
  file.print(bookPath);
  file.write(reinterpret_cast<const uint8_t*>(numbers), static_cast<size_t>(length));
  file.print(query);
  return true;
}

bool placerequest::take(const std::string& bookPath, Request& out) {
  if (!Storage.exists(REQUEST_PATH)) return false;
  std::string text;
  const bool read = Storage.readFileToString("PLACE", REQUEST_PATH, MAX_REQUEST_BYTES, text);
  Storage.remove(REQUEST_PATH);
  if (!read) return false;

  const size_t pathEnd = text.find('\n');
  const size_t spineEnd = pathEnd == std::string::npos ? pathEnd : text.find('\n', pathEnd + 1);
  const size_t occurrenceEnd = spineEnd == std::string::npos ? spineEnd : text.find('\n', spineEnd + 1);
  if (occurrenceEnd == std::string::npos || text.compare(0, pathEnd, bookPath) != 0) return false;

  out.spine = atoi(text.c_str() + pathEnd + 1);
  out.occurrence = atoi(text.c_str() + spineEnd + 1);
  out.query = text.substr(occurrenceEnd + 1);
  return !out.query.empty();
}
