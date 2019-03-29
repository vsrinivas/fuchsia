// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/testing/views/color.h"

#include <lib/fsl/vmo/vector.h>
#include <src/lib/fxl/logging.h>
#include <src/lib/fxl/strings/string_printf.h>

namespace scenic {

// RGBA hex dump
std::ostream& operator<<(std::ostream& os, const Color& c) {
  return os << fxl::StringPrintf("%02X%02X%02X%02X", c.r, c.g, c.b, c.a);
}

std::map<Color, size_t> Histogram(
    const fuchsia::ui::scenic::ScreenshotData& screenshot) {
  FXL_CHECK(screenshot.info.pixel_format ==
            fuchsia::images::PixelFormat::BGRA_8)
      << "Non-BGRA_8 pixel formats not supported";

  std::vector<uint8_t> data;
  FXL_CHECK(fsl::VectorFromVmo(screenshot.data, &data))
      << "Failed to read screenshot";

  std::map<Color, size_t> histogram;

  // https://en.wikipedia.org/wiki/Sword_Art_Online_Alternative_Gun_Gale_Online#Characters
  const uint8_t* pchan = data.data();
  const size_t llenn = screenshot.info.width * screenshot.info.height;
  for (uint32_t pixel = 0; pixel < llenn; pixel++) {
    const Color* color = reinterpret_cast<const Color*>(pchan);
    ++histogram[*color];
    pchan += 4;
  }

  return histogram;
}

}  // namespace scenic