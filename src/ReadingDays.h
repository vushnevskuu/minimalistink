#pragma once

#include <cstdint>

// Active reading seconds per local calendar day, one SD file per year.
namespace readingdays {

using DayVisitor = void (*)(void* context, int dayOfYear, uint16_t seconds);

// Credits local days from startEpoch on, splitting at midnight. A day saturates at 65535 s.
void add(int64_t startEpoch, uint32_t seconds);

// Visits days 0..days-1 in order. Days with no record report 0 seconds.
void forEachDay(int year, int days, DayVisitor visit, void* context);

}  // namespace readingdays
