// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_VK_COLOR_SPACE_H_
#define SRC_UI_LIB_ESCHER_VK_COLOR_SPACE_H_

#include <vulkan/vulkan.hpp>

namespace escher {

// Color spaces used in Escher images.
// This corresponds to Fuchsia sysmem |ColorSpaceType| enum.
//
// Similar to Fuchsia |ColorSpaceType|, This list has a separate entry for each
// variant of a color space standard, since different variants may use
// different samplers and thus different render passes.
//
// So should we ever add support for the RGB variant of 709, for example, we'd
// add a separate entry to this list for that variant.  Similarly for the RGB
// variants of 2020 or 2100.  Similarly for the YcCbcCrc variant of 2020.
// Similarly for the ICtCp variant of 2100.
//
enum class ColorSpace : uint32_t {
  // Not a valid color space type.
  kInvalid = 0,
  // sRGB
  kSrgb = 1,
  // 601 NTSC ("525 line") YCbCr primaries, narrow
  kRec601Ntsc = 2,
  // 601 NTSC ("525 line") YCbCr primaries, wide
  kRec601NtscFullRange = 3,
  // 601 PAL ("625 line") YCbCr primaries, narrow
  kRec601Pal = 4,
  // 601 PAL ("625 line") YCbCr primaries, wide
  kRec601PalFullRange = 5,
  // 709 YCbCr (not RGB)
  kRec709 = 6,
  // 2020 YCbCr (not RGB, not YcCbcCrc)
  kRec2020 = 7,
  // 2100 YCbCr (not RGB, not ICtCp)
  kRec2100 = 8,
  // Either the pixel format doesn't represent a color, or it's in an
  // application-specific colorspace that isn't describable by another entry
  // in this enum.
  kPassThrough = 9,
  // The sysmem client is explicitly indicating that the sysmem client does
  // not care which color space is chosen / used.
  kDoNotCare = 0xFFFFFFFE,
};

// Textures created by Escher (e.g. output textures, textures for testing
// purposes) will use the default color space defined by this function and will
// be only determined by the image format (i.e. whether it is a YUV image).
ColorSpace GetDefaultColorSpace(vk::Format format);

}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_VK_COLOR_SPACE_H_
