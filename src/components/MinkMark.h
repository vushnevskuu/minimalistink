#pragma once

#include <GfxRenderer.h>

#include <cstdint>

// The MINK wordmark drawn from solid blocks, centred on a point. It stands in for a logo image.
inline void drawMinkMark(const GfxRenderer& renderer, const int centerX, const int centerY) {
  static constexpr int LETTERS = 4;
  static constexpr int ROWS = 7;
  static constexpr int COLUMNS = 5;
  static constexpr int BLOCK = 12;
  static constexpr uint8_t GLYPHS[LETTERS][ROWS] = {
      {0b10001, 0b11011, 0b10101, 0b10101, 0b10001, 0b10001, 0b10001},  // M
      {0b01110, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b01110},  // I
      {0b10001, 0b11001, 0b11001, 0b10101, 0b10011, 0b10011, 0b10001},  // N
      {0b10001, 0b10010, 0b10100, 0b11000, 0b10100, 0b10010, 0b10001},  // K
  };
  const int width = (LETTERS * COLUMNS + LETTERS - 1) * BLOCK;
  const int left = centerX - width / 2;
  const int top = centerY - ROWS * BLOCK / 2;
  for (int letter = 0; letter < LETTERS; letter++) {
    const int letterLeft = left + letter * (COLUMNS + 1) * BLOCK;
    for (int row = 0; row < ROWS; row++) {
      for (int column = 0; column < COLUMNS; column++) {
        if ((GLYPHS[letter][row] >> (COLUMNS - 1 - column)) & 1) {
          renderer.fillRect(letterLeft + column * BLOCK, top + row * BLOCK, BLOCK, BLOCK, true);
        }
      }
    }
  }
}
