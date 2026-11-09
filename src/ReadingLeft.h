#pragma once

#include <cstdint>
#include <string>

// Reading pace and the pages left in the last open book, for the Time Left
// sleep screen, plus the text of the page where reading stopped for Home.
namespace readingleft {

struct Estimate {
  uint32_t pagesLeft;
  uint32_t secondsLeft;
};

// A negative pagesLeft keeps the stored snapshot and only updates the pace.
void record(const char* bookPath, int pagesLeft, uint32_t sessionSeconds, uint32_t sessionPages);

// The start of the current page, for the home screen. Replaces any earlier excerpt.
void saveExcerpt(const char* bookPath, const std::string& text);
bool loadExcerpt(const char* bookPath, std::string& out);

// False when the book has no snapshot or is finished.
bool estimate(const char* bookPath, Estimate& out);

}  // namespace readingleft
