#pragma once

#include <string>
#include <vector>

// Reader side of the shelves arranged in the book uploader web app. /book-uploader/shelf.json is:
// {"version":2,"shelves":[{"id":"read","name":"","items":[{"path":"/Books/x.epub","title":"North Wind",
//  "author":"Ilya Korneev","finished":"2026-10"}]},{"id":"unread","name":"","items":[]}]}
// "read" and "unread" come first; an empty name on them means the default title in the reader's language.
namespace shelffile {

struct Item {
  std::string path;  // empty when the stored path was too long to be usable
  std::string title;
  std::string author;
  std::string finished;  // YYYY-MM, may be empty
};

// One screen of one shelf. Every string is capped, so a large file costs no more memory than this.
struct Window {
  int shelfCount = 0;
  int firstFilledShelf = -1;  // index of the first shelf with books, -1 when all are empty
  std::string id;
  std::string name;
  int itemCount = 0;
  std::vector<Item> items;
};

// Reads items [first, first+count) of shelf shelfIndex. A missing or invalid file yields the two
// empty built-in shelves. shelfCount <= shelfIndex means the file has no such shelf.
void load(int shelfIndex, int first, int count, Window& out);

}  // namespace shelffile
