// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_CAMERA_LIB_RAW_FORMATS_RAW_FORMATS_H_
#define SRC_CAMERA_LIB_RAW_FORMATS_RAW_FORMATS_H_

#include "src/camera/lib/raw_formats/raw.h"
#include "src/camera/lib/raw_formats/raw10.h"
#include "src/camera/lib/raw_formats/raw_ipu3.h"

namespace camera::raw {

// Human readable format ID tags that can be used in code, though it is suggested to pass a
// reference to the actual format struct wherever it is reasonable to do so. This primarily
// exists to enable certain optimizations when receiving format objects over the wire.
enum class Format : uint64_t {
  // Variants of Android's RAW10.
  RAW10_BGGR = kRaw10FormatBGGR.id,
  RAW10_GBRG = kRaw10FormatGBRG.id,
  RAW10_GRBG = kRaw10FormatGRBG.id,
  RAW10_RGGB = kRaw10FormatRGGB.id,

  // Intel's IPU3 formats.
  IPU3_BGGR10 = kIpu3FormatBGGR10.id,
  IPU3_GBRG10 = kIpu3FormatGBRG10.id,
  IPU3_GRBG10 = kIpu3FormatGRBG10.id,
  IPU3_RGGB10 = kIpu3FormatRGGB10.id,
};

// Given a format ID, get the corresponding format.
// A bonus to the existence of this function is that it will fail to compile if there are any hash
// collisions between the formats.
constexpr const RawFormat& GetFormatById(Format format_id) {
  switch (format_id) {
    using enum Format;

    // Variants of Android's RAW10.
    case RAW10_BGGR:
      return kRaw10FormatBGGR;
    case RAW10_GBRG:
      return kRaw10FormatGBRG;
    case RAW10_GRBG:
      return kRaw10FormatGRBG;
    case RAW10_RGGB:
      return kRaw10FormatRGGB;

    // Intel's IPU3 formats.
    case IPU3_BGGR10:
      return kIpu3FormatBGGR10;
    case IPU3_GBRG10:
      return kIpu3FormatGBRG10;
    case IPU3_GRBG10:
      return kIpu3FormatGRBG10;
    case IPU3_RGGB10:
      return kIpu3FormatRGGB10;
  }
}

}  // namespace camera::raw

#endif  // SRC_CAMERA_LIB_RAW_FORMATS_RAW_FORMATS_H_
