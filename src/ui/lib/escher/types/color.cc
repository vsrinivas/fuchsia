// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/types/color.h"

#include <src/lib/fxl/strings/string_printf.h>

namespace escher {

std::ostream& operator<<(std::ostream& os, const ColorRgba& c) {
  return os << fxl::StringPrintf("RGBA:%02X%02X%02X%02X", c.r, c.g, c.b, c.a);
}

std::ostream& operator<<(std::ostream& os, const ColorBgra& c) {
  return os << fxl::StringPrintf("BGRA:%02X%02X%02X%02X", c.b, c.g, c.r, c.a);
}

}  // namespace escher
