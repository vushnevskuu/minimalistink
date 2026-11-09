#include "ShelfFile.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <StreamingJsonParser.h>

#include <cstdint>
#include <cstring>

namespace {
constexpr char SHELF_PATH[] = "/book-uploader/shelf.json";
constexpr int SUPPORTED_VERSION = 2;
constexpr size_t MAX_ID_BYTES = 32;
constexpr size_t MAX_NAME_BYTES = 96;
constexpr size_t MAX_BOOK_TEXT_BYTES = 128;
constexpr size_t MAX_PATH_BYTES = 255;
constexpr size_t FINISHED_BYTES = 7;
constexpr size_t READ_CHUNK = 256;

// Container depth of each interesting object: {shelves:[{items:[{...}]}]}
constexpr int ROOT_DEPTH = 1;
constexpr int SHELVES_DEPTH = 2;
constexpr int SHELF_DEPTH = 3;
constexpr int ITEMS_DEPTH = 4;
constexpr int ITEM_DEPTH = 5;

enum class Field : uint8_t { None, Version, Shelves, Id, Name, Items, Path, Title, Author, Finished };

struct Reader {
  int shelfIndex;
  int first;
  int count;
  shelffile::Window& out;

  int depth = 0;
  Field field = Field::None;
  bool inShelves = false;
  bool inItems = false;
  bool versionOk = false;
  shelffile::Item item;
  int currentShelf = -1;
  int currentItem = -1;

  bool itemWanted() const { return currentShelf == shelfIndex && currentItem >= first && currentItem < first + count; }
};

// Cuts on a character boundary, so a capped name never ends in half a UTF-8 sequence.
void assignCapped(std::string& target, const char* value, size_t len, const size_t maxBytes) {
  if (len > maxBytes) {
    len = maxBytes;
    while (len > 0 && (static_cast<uint8_t>(value[len]) & 0xC0) == 0x80) len--;
  }
  target.assign(value, len);
}

bool matches(const char* key, const size_t len, const char* name) {
  return strlen(name) == len && strncmp(key, name, len) == 0;
}

void onKey(void* ctx, const char* key, const size_t len) {
  auto& r = *static_cast<Reader*>(ctx);
  r.field = Field::None;
  if (r.depth == ROOT_DEPTH) {
    if (matches(key, len, "version")) r.field = Field::Version;
    if (matches(key, len, "shelves")) r.field = Field::Shelves;
  } else if (r.depth == SHELF_DEPTH && r.inShelves) {
    if (matches(key, len, "id")) r.field = Field::Id;
    if (matches(key, len, "name")) r.field = Field::Name;
    if (matches(key, len, "items")) r.field = Field::Items;
  } else if (r.depth == ITEM_DEPTH && r.inItems) {
    if (matches(key, len, "path")) r.field = Field::Path;
    if (matches(key, len, "title")) r.field = Field::Title;
    if (matches(key, len, "author")) r.field = Field::Author;
    if (matches(key, len, "finished")) r.field = Field::Finished;
  }
}

void onString(void* ctx, const char* value, const size_t len) {
  auto& r = *static_cast<Reader*>(ctx);
  if (r.currentShelf == r.shelfIndex) {
    if (r.field == Field::Id) assignCapped(r.out.id, value, len, MAX_ID_BYTES);
    if (r.field == Field::Name) assignCapped(r.out.name, value, len, MAX_NAME_BYTES);
  }
  if (r.itemWanted()) {
    // A cut path would point at a different file, so an oversized one is left empty.
    if (r.field == Field::Path && len <= MAX_PATH_BYTES) r.item.path.assign(value, len);
    if (r.field == Field::Title) assignCapped(r.item.title, value, len, MAX_BOOK_TEXT_BYTES);
    if (r.field == Field::Author) assignCapped(r.item.author, value, len, MAX_BOOK_TEXT_BYTES);
    if (r.field == Field::Finished && len <= FINISHED_BYTES) r.item.finished.assign(value, len);
  }
  r.field = Field::None;
}

void onNumber(void* ctx, const char* value, const size_t len) {
  auto& r = *static_cast<Reader*>(ctx);
  if (r.field == Field::Version) r.versionOk = len == 1 && value[0] == '0' + SUPPORTED_VERSION;
  r.field = Field::None;
}

void onBool(void* ctx, bool) { static_cast<Reader*>(ctx)->field = Field::None; }

void onNull(void* ctx) { static_cast<Reader*>(ctx)->field = Field::None; }

void onObjectStart(void* ctx) {
  auto& r = *static_cast<Reader*>(ctx);
  r.depth++;
  r.field = Field::None;
  if (r.depth == SHELF_DEPTH && r.inShelves) {
    r.currentShelf++;
    r.currentItem = -1;
  } else if (r.depth == ITEM_DEPTH && r.inItems) {
    r.currentItem++;
    r.item = shelffile::Item{};
  }
}

void onObjectEnd(void* ctx) {
  auto& r = *static_cast<Reader*>(ctx);
  if (r.depth == ITEM_DEPTH && r.inItems && r.itemWanted()) {
    r.out.items.push_back(std::move(r.item));
  } else if (r.depth == SHELF_DEPTH && r.inShelves) {
    r.out.shelfCount++;
    if (r.out.firstFilledShelf < 0 && r.currentItem >= 0) r.out.firstFilledShelf = r.currentShelf;
    if (r.currentShelf == r.shelfIndex) r.out.itemCount = r.currentItem + 1;
  }
  r.depth--;
}

void onArrayStart(void* ctx) {
  auto& r = *static_cast<Reader*>(ctx);
  r.depth++;
  if (r.depth == SHELVES_DEPTH && r.field == Field::Shelves) r.inShelves = true;
  if (r.depth == ITEMS_DEPTH && r.field == Field::Items) r.inItems = true;
  r.field = Field::None;
}

void onArrayEnd(void* ctx) {
  auto& r = *static_cast<Reader*>(ctx);
  if (r.depth == SHELVES_DEPTH) r.inShelves = false;
  if (r.depth == ITEMS_DEPTH) r.inItems = false;
  r.depth--;
}

void useBuiltInShelves(const int shelfIndex, shelffile::Window& out) {
  out = shelffile::Window{};
  out.shelfCount = 2;
  out.id = shelfIndex == 1 ? "unread" : "read";
}
}  // namespace

void shelffile::load(const int shelfIndex, const int first, const int count, Window& out) {
  out = Window{};
  out.items.reserve(count);

  HalFile file;
  if (!Storage.exists(SHELF_PATH) || !Storage.openFileForRead("SHELF", SHELF_PATH, file)) {
    return useBuiltInShelves(shelfIndex, out);
  }

  Reader reader{shelfIndex, first, count, out};
  JsonCallbacks callbacks{&reader, onKey,         onString,    onNumber,     onBool,
                          onNull,  onObjectStart, onObjectEnd, onArrayStart, onArrayEnd};
  auto parser = makeUniqueNoThrow<StreamingJsonParser>(callbacks);
  if (!parser) return useBuiltInShelves(shelfIndex, out);

  char chunk[READ_CHUNK];
  int bytes;
  while ((bytes = file.read(reinterpret_cast<uint8_t*>(chunk), sizeof(chunk))) > 0) {
    parser->feed(chunk, static_cast<size_t>(bytes));
    if (parser->hasError()) break;
  }

  // A depth above zero means the file was cut short.
  if (parser->hasError() || reader.depth != 0 || !reader.versionOk || out.shelfCount == 0) {
    LOG_ERR("SHELF", "Ignoring invalid shelf file");
    useBuiltInShelves(shelfIndex, out);
  }
}
