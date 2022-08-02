// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "src/camera/lib/raw_formats/raw_formats.h"

#include <cstdint>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace camera::raw {
namespace {

TEST(RawFormatsTest, TestFormatLookup) {
  static_assert(GetFormatById(Format::RAW10_BGGR) == kRaw10FormatBGGR);
  static_assert(GetFormatById(Format::RAW10_GBRG) == kRaw10FormatGBRG);
  static_assert(GetFormatById(Format::RAW10_GRBG) == kRaw10FormatGRBG);
  static_assert(GetFormatById(Format::RAW10_RGGB) == kRaw10FormatRGGB);

  static_assert(GetFormatById(Format::IPU3_BGGR10) == kIpu3FormatBGGR10);
  static_assert(GetFormatById(Format::IPU3_GBRG10) == kIpu3FormatGBRG10);
  static_assert(GetFormatById(Format::IPU3_GRBG10) == kIpu3FormatGRBG10);
  static_assert(GetFormatById(Format::IPU3_RGGB10) == kIpu3FormatRGGB10);
}

}  // namespace
}  // namespace camera::raw
