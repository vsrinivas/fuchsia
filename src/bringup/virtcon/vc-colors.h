// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_BRINGUP_VIRTCON_VC_COLORS_H_
#define SRC_BRINGUP_VIRTCON_VC_COLORS_H_

#include <string.h>

constexpr uint32_t default_palette[] = {
    // 0-7 Normal/dark versions of colors
    0xff000000,  // black
    0xffaa0000,  // red
    0xff00aa00,  // green
    0xffaa5500,  // brown
    0xff0000aa,  // blue
    0xffaa00aa,  // zircon
    0xff00aaaa,  // cyan
    0xffaaaaaa,  // grey
    // 8-15 Bright/light versions of colors
    0xff555555,  // dark grey
    0xffff5555,  // bright red
    0xff55ff55,  // bright green
    0xffffff55,  // yellow
    0xff5555ff,  // bright blue
    0xffff55ff,  // bright zircon
    0xff55ffff,  // bright cyan
    0xffffffff,  // white
};

typedef struct color_scheme {
  uint8_t front;
  uint8_t back;
} color_scheme_t;

constexpr color_scheme_t color_schemes[] = {
    {
        // Dark (white/black) [Default].
        .front = 0x0F,
        .back = 0x00,
    },
    {
        // Light (black/white).
        .front = 0x00,
        .back = 0x0F,
    },
    {
        // Special (White, Blue).
        .front = 0x0F,
        .back = 0x04,
    },
};

constexpr int kDarkColorScheme = 0;
constexpr int kLightColorScheme = 1;
constexpr int kSpecialColorScheme = 2;

constexpr int kDefaultColorScheme = kDarkColorScheme;

constexpr const color_scheme_t* string_to_color_scheme(const char* string) {
  if (string != NULL) {
    if (strcmp(string, "dark") == 0) {
      return &color_schemes[kDarkColorScheme];
    } else if (strcmp(string, "light") == 0) {
      return &color_schemes[kLightColorScheme];
    } else if (strcmp(string, "special") == 0) {
      return &color_schemes[kSpecialColorScheme];
    }
  }
  return &color_schemes[kDefaultColorScheme];
}

#endif  // SRC_BRINGUP_VIRTCON_VC_COLORS_H_
