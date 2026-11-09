#pragma once

#include <string>

// "Open this place" request from the phone app. It is kept on the card because the reader restarts
// with a clean heap to open the book.
namespace placerequest {

struct Request {
  int spine = 0;
  int occurrence = 0;  // matches of query before the wanted one in that spine item
  std::string query;
};

bool save(const char* bookPath, int spine, int occurrence, const char* query);

// Removes any stored request. True only when it was for bookPath and could be read.
bool take(const std::string& bookPath, Request& out);

}  // namespace placerequest
