#pragma once

#include <GfxRenderer.h>

#include <cstdint>

// Shared by the Reading Map sleep screen and the home screen strip.
inline int readingMapLevel(const uint16_t seconds) {
  if (seconds == 0) return 0;
  if (seconds < 25 * 60) return 1;
  if (seconds < 45 * 60) return 2;
  return seconds < 90 * 60 ? 3 : 4;
}

// Levels 1-3 are 25/50/75% ordered dither, 4 is solid.
inline void drawReadingMapCell(const GfxRenderer& renderer, const int x, const int y, const int size, const int level) {
  if (level == 4) return renderer.fillRect(x, y, size, size);
  for (int py = y; py < y + size; py++) {
    for (int px = x; px < x + size; px++) {
      const bool sparse = (px & 1) == 0 && (py & 1) == 0;
      const bool on = level == 1 ? sparse : level == 2 ? ((px + py) & 1) == 0 : !sparse;
      renderer.drawPixel(px, py, on);
    }
  }
}
