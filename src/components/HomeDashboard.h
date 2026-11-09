#pragma once

#include <cstdint>
#include <vector>

#include "RecentBooksStore.h"
#include "components/themes/BaseTheme.h"

class GfxRenderer;

enum class HomeBlock : uint8_t { Calendar, Current, Finished, Menu };

constexpr int HOME_BLOCK_COUNT = 4;
// The fixed layout is drawn for this portrait width.
constexpr int HOME_DASHBOARD_WIDTH = 480;
constexpr int HOME_CURRENT_COVER_HEIGHT = 135;
constexpr int HOME_FINISHED_COVER_HEIGHT = 146;
constexpr int HOME_MAX_MENU_ITEMS = 5;

struct HomeDashboardProps {
  const RecentBook* currentBook = nullptr;
  int currentProgress = -1;
  const std::vector<RecentBook>* finishedBooks = nullptr;
  uint32_t finishedTotal = 0;
  bool currentSelected = false;
  int selectedMenuItem = -1;
  const char* menuLabels[HOME_MAX_MENU_ITEMS] = {};
  UIIcon menuIcons[HOME_MAX_MENU_ITEMS] = {};
  int menuCount = 0;
  HomeBlock blocks[HOME_BLOCK_COUNT] = {};
  int blockCount = 0;
};

// True on portrait-480 button devices, whatever the current orientation.
bool homeLayoutsSupported(const GfxRenderer& renderer);
const char* homeBookCountWord(uint32_t count);

// Top-to-bottom blocks from a comma-separated setting. An empty value is the
// default order; the menu is always kept so File Transfer stays reachable.
int parseHomeLayout(const char* value, HomeBlock out[HOME_BLOCK_COUNT]);

void drawHomeDashboard(const GfxRenderer& renderer, const HomeDashboardProps& props);

// Draws a cached cover thumb inside a rounded frame, or a placeholder.
void drawHomeCover(const GfxRenderer& renderer, const RecentBook& book, int height, int x, int y, int width);
