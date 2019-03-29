// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "src/lib/fxl/strings/string_view.h"
#include "src/lib/fxl/strings/utf_codecs.h"

namespace zxdb {

// Returns a string containing the right arrow marker for marking the current
// line. May be UTF-8 so size() could be > 1, but it will only be one Unicode
// character.
std::string GetRightArrow();

// Returns a circle symbol suitable for marking breakpoints in code listings.
std::string GetBreakpointMarker();
std::string GetDisabledBreakpointMarker();

// Returns a Unicode bullet in UTF-8.
std::string GetBullet();

// Returns the number of Unicode characters in the given UTF-8 string. This
// attempts to predict how many spaces the given string will take up when
// printed to a Unicode-aware text console.
//
// NOTE: This function currently doesn't handle any combining accents which it
// seems modern Linux terminals do handle. It obviously doesn't handle
// complicated things like ligatures and Arabic which we assume you're not
// typing into the console and expecting to be aligned anyway.
size_t UnicodeCharWidth(const std::string& str);

}  // namespace zxdb
