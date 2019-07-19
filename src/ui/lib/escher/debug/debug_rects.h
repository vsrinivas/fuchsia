// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_DEBUG_DEBUG_RECTS_H_
#define SRC_UI_LIB_ESCHER_DEBUG_DEBUG_RECTS_H_

#include <memory>

#include "src/ui/lib/escher/forward_declarations.h"
#include "src/ui/lib/escher/types/color.h"

#include <vulkan/vulkan.hpp>

namespace escher {

// This is a rectangle renderer that blits to the output image for
// maximal performance.
class DebugRects {
 public:
  static std::unique_ptr<DebugRects> New(BatchGpuUploader* uploader, ImageFactory* factory);

  enum Color { kBlack = 0, kWhite, kRed, kGreen, kBlue, kYellow, kPurple, kBrown, kMax };

  static constexpr ColorRgba colorData[kMax] = {
      ColorRgba(0x0, 0x0, 0x0, 0xff),   ColorRgba(0xff, 0xff, 0xff, 0xff),
      ColorRgba(0xff, 0x0, 0x0, 0xff),  ColorRgba(0x0, 0xff, 0x0, 0xff),
      ColorRgba(0x0, 0x0, 0xff, 0xff),  ColorRgba(0xff, 0xff, 0x0, 0xff),
      ColorRgba(0xc0, 0x0, 0xff, 0xff), ColorRgba(0x60, 0x30, 0x0, 0xff)};

  // Blit a rectangle of the chosen |color|.
  void Blit(CommandBuffer* cb, Color color, const ImagePtr& target, vk::Rect2D rect);

 private:
  explicit DebugRects(ImagePtr image);

  ImagePtr palette_;
};

}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_DEBUG_DEBUG_RECTS_H_
