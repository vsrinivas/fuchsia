// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/string_util.h"

namespace zxdb {

std::string GetRightArrow() {
  // U+25B6 BLACK RIGHT-POINTING TRIANGLE.
  return std::string("\xe2\x96\xb6");
}

std::string GetBreakpointMarker() {
  // U+25C9 FISHEYE.
  // It might be nice to use U+1F534 LARGE RED CIRCLE which on Mac even has
  // cool shading. But the Mac terminal things it takes 2 characters of layout
  // which makes it hard to predict layout in a cross-platform way.
  return std::string("\xe2\x97\x89");
}

std::string GetDisabledBreakpointMarker() {
  // U+25EF LARGE CIRCLE
  return std::string("\xe2\x97\xaf");
}

std::string GetBullet() {
  // U+2022 BULLET
  return std::string("\xe2\x80\xa2");
}

size_t UnicodeCharWidth(const std::string& str) {
  size_t result = 0;

  for (size_t i = 0; i < str.size(); i++) {
    uint32_t code_point;
    // Don't care about the success of this since we just care about
    // incrementing the index.
    fxl::ReadUnicodeCharacter(str.data(), str.size(), &i, &code_point);
    result++;
  }
  return result;
}

}  // namespace zxdb
