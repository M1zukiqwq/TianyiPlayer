// TianyiPlayer theme: colors, layout metrics and asset paths.
// 洛天依主题配色：天依蓝为主，樱粉点缀，深色半透明面板。
#pragma once

#include <SDL3/SDL.h>

#include <string>

namespace tianyi::theme {

// ---- Colors (天依蓝 Tianyi Blue / 樱粉 Sakura Pink) ----
inline constexpr SDL_Color kAccent{0x57, 0xC7, 0xFF, 0xFF};       // primary accent
inline constexpr SDL_Color kAccentSoft{0x57, 0xC7, 0xFF, 0x99};   // accent, translucent
inline constexpr SDL_Color kPink{0xFF, 0xB7, 0xC5, 0xFF};         // secondary accent
inline constexpr SDL_Color kText{0xF2, 0xF7, 0xFF, 0xFF};         // main text
inline constexpr SDL_Color kTextDim{0xB9, 0xC8, 0xDC, 0xFF};      // dimmed text
inline constexpr SDL_Color kPanel{0x10, 0x18, 0x28, 0xC0};        // control bar panel
inline constexpr SDL_Color kPanelDeep{0x0A, 0x10, 0x1C, 0xE6};    // deeper panel
inline constexpr SDL_Color kTrack{0xFF, 0xFF, 0xFF, 0x33};        // slider track
inline constexpr SDL_Color kOverlay{0x08, 0x0C, 0x16, 0x66};      // background dim

// ---- Layout ----
inline constexpr float kBottomBarHeight = 112.0f;
inline constexpr float kTopBarHeight = 64.0f;
inline constexpr float kButtonSize = 56.0f;
inline constexpr float kCornerRadius = 12.0f;
inline constexpr float kSeekHeight = 8.0f;

// ---- Asset files (relative to the executable directory) ----
inline std::string background_path() { return "assets/theme/tianyi_background.jpg"; }
inline std::string poster_path() { return "assets/theme/tianyi_poster.jpg"; }
inline std::string font_path() { return "assets/fonts/NotoSansSC.ttf"; }

} // namespace tianyi::theme
