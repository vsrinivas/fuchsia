// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "color_source.h"

#include <lib/syslog/cpp/macros.h>
#include <zircon/assert.h>
#include <zircon/syscalls.h>

#include <array>

namespace camera {

// To fill an ARGB32 image, you have the following offsets:
static constexpr uint8_t kAlphaShift = 24;
static constexpr uint8_t kRedShift = 16;
static constexpr uint8_t kGreenShift = 8;
// Set the alpha value to 100%:
static constexpr uint8_t kAlphaValue = 0xff;

static constexpr uint32_t kLowByteMask = 0xff;
static constexpr uint32_t kLowThreeBitMask = 0x7;
static constexpr uint32_t kUint8Max = 0xff;

// Every new frame, we increment by kFrameColorInc.  There are kNumberOfPhases, and each phase
// takes 2^kIndexDownshift steps.
static constexpr uint32_t kFrameColorInc = 0x01;
static constexpr uint8_t kIndexDownshift = 8;
static constexpr uint8_t kNumberOfPhases = 6;
// The cycle repeats after kNumberOfPhases * 2^kIndexDownshift steps.
static constexpr uint32_t kMaxFrameColor = kNumberOfPhases << kIndexDownshift;
// To cover the HSV color space, R,G,B go through the phases offset by the following:
static constexpr uint8_t kRedPhaseOffset = 1;
static constexpr uint8_t kGreenPhaseOffset = 5;
static constexpr uint8_t kBluePhaseOffset = 3;

void ColorSource::FillARGB(void* start, size_t buffer_size) {
  if (!start) {
    FX_LOGS(ERROR) << "Must pass a valid buffer pointer";
    return;
  }
  uint8_t r, g, b;
  hsv_color(frame_color_, &r, &g, &b);
  FX_VLOGS(4) << "Filling with " << static_cast<int>(r) << " " << static_cast<int>(g) << " "
              << static_cast<int>(b);
  uint32_t color = kAlphaValue << kAlphaShift | r << kRedShift | g << kGreenShift | b;
  ZX_DEBUG_ASSERT(buffer_size % 4 == 0);
  size_t num_pixels = buffer_size / 4;
  auto* pixels = reinterpret_cast<uint32_t*>(start);
  for (uint32_t i = 0; i < num_pixels; i++) {
    pixels[i] = color;
  }

  // Ignore if flushing the cache fails.
  zx_cache_flush(start, buffer_size, ZX_CACHE_FLUSH_DATA | ZX_CACHE_FLUSH_INVALIDATE);
  frame_color_ += kFrameColorInc;
  if (frame_color_ > kMaxFrameColor) {
    frame_color_ -= kMaxFrameColor;
  }
}

void ColorSource::hsv_color(uint32_t index, uint8_t* r, uint8_t* g, uint8_t* b) {
  index = index % kMaxFrameColor;
  uint8_t pos = index & kLowByteMask;
  uint8_t neg = kLowByteMask - (index & kLowByteMask);
  uint8_t phase = (index >> kIndexDownshift) & kLowThreeBitMask;
  std::array<uint8_t, kNumberOfPhases> phases = {kUint8Max, kUint8Max, neg, 0x00, 0x00, pos};
  *r = phases[(phase + kRedPhaseOffset) % phases.size()];
  *g = phases[(phase + kGreenPhaseOffset) % phases.size()];
  *b = phases[(phase + kBluePhaseOffset) % phases.size()];
}

}  // namespace camera
