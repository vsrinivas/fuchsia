// Copyright (C) 2011-2022 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_GOLDFISH_DISPLAY_THIRD_PARTY_AOSP_HWCOMPOSER_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_GOLDFISH_DISPLAY_THIRD_PARTY_AOSP_HWCOMPOSER_H_

#include <cstdint>
#include <vector>

namespace goldfish::hwc {

// All the definitions below should match the definitions in Android
// hwcomposer2.h:
// https://android.googlesource.com/platform/hardware/libhardware/+/master/include/hardware/hwcomposer2.h

using android_transform_t = enum {
  HAL_TRANSFORM_FLIP_H = 1,   // (1 << 0)
  HAL_TRANSFORM_FLIP_V = 2,   // (1 << 1)
  HAL_TRANSFORM_ROT_90 = 4,   // (1 << 2)
  HAL_TRANSFORM_ROT_180 = 3,  // (FLIP_H | FLIP_V)
  HAL_TRANSFORM_ROT_270 = 7,  // ((FLIP_H | FLIP_V) | ROT_90)
};

using hwc_color_t = struct hwc_color {
  uint8_t r;
  uint8_t g;
  uint8_t b;
  uint8_t a;
};

using hwc_frect_t = struct hwc_frect {
  float left;
  float top;
  float right;
  float bottom;
};

using hwc_rect_t = struct hwc_rect {
  int left;
  int top;
  int right;
  int bottom;
};

using hwc_transform_t = enum : int32_t {
  /* flip source image horizontally */
  HWC_TRANSFORM_FLIP_H = HAL_TRANSFORM_FLIP_H,
  /* flip source image vertically */
  HWC_TRANSFORM_FLIP_V = HAL_TRANSFORM_FLIP_V,
  /* rotate source image 90 degrees clock-wise */
  HWC_TRANSFORM_ROT_90 = HAL_TRANSFORM_ROT_90,
  /* rotate source image 180 degrees */
  HWC_TRANSFORM_ROT_180 = HAL_TRANSFORM_ROT_180,
  /* rotate source image 270 degrees clock-wise */
  HWC_TRANSFORM_ROT_270 = HAL_TRANSFORM_ROT_270,
  /* flip source image horizontally, the rotate 90 degrees clock-wise */
  HWC_TRANSFORM_FLIP_H_ROT_90 = HAL_TRANSFORM_FLIP_H | HAL_TRANSFORM_ROT_90,
  /* flip source image vertically, the rotate 90 degrees clock-wise */
  HWC_TRANSFORM_FLIP_V_ROT_90 = HAL_TRANSFORM_FLIP_V | HAL_TRANSFORM_ROT_90,
};

using hwc2_composition_t = enum : int32_t {
  HWC2_COMPOSITION_INVALID = 0,
  HWC2_COMPOSITION_CLIENT = 1,
  HWC2_COMPOSITION_DEVICE = 2,
  HWC2_COMPOSITION_SOLID_COLOR = 3,
  HWC2_COMPOSITION_CURSOR = 4,
  HWC2_COMPOSITION_SIDEBAND = 5,
};

enum class Composition : std::underlying_type<hwc2_composition_t>::type {
  kInvalid = HWC2_COMPOSITION_INVALID,
  kClient = HWC2_COMPOSITION_CLIENT,
  kDevice = HWC2_COMPOSITION_DEVICE,
  kSolidColor = HWC2_COMPOSITION_SOLID_COLOR,
  kCursor = HWC2_COMPOSITION_CURSOR,
  kSideband = HWC2_COMPOSITION_SIDEBAND,
};

using hwc2_blend_mode_t = enum : int32_t {
  HWC2_BLEND_MODE_INVALID = 0,
  HWC2_BLEND_MODE_NONE = 1,
  HWC2_BLEND_MODE_PREMULTIPLIED = 2,
  HWC2_BLEND_MODE_COVERAGE = 3,
};

enum class BlendMode : std::underlying_type<hwc2_blend_mode_t>::type {
  kInvalid = HWC2_BLEND_MODE_INVALID,
  kNone = HWC2_BLEND_MODE_NONE,
  kPremultiplied = HWC2_BLEND_MODE_PREMULTIPLIED,
  kCoverage = HWC2_BLEND_MODE_COVERAGE,
};

using ComposeLayer = struct compose_layer {
  uint32_t cb_handle;
  Composition compose_mode;
  hwc_rect_t display_frame;
  hwc_frect_t crop;
  BlendMode blend_mode;
  float alpha;
  hwc_color_t color;
  hwc_transform_t transform;
};

using compose_device_t = struct compose_device {
  uint32_t version;
  uint32_t target_handle;
  uint32_t num_layers;
  struct compose_layer layers[0];
};

class ComposeDevice {
 public:
  explicit ComposeDevice(size_t num_layers)
      : data_(sizeof(compose_device_t) + num_layers * sizeof(ComposeLayer), 0u) {}

  compose_device_t* get() { return reinterpret_cast<compose_device_t*>(data_.data()); }

  const compose_device_t* get() const {
    return reinterpret_cast<const compose_device_t*>(data_.data());
  }

  compose_device_t& operator[](size_t index) {
    return reinterpret_cast<compose_device_t*>(data_.data())[index];
  }

  const compose_device_t& operator[](size_t index) const {
    return reinterpret_cast<const compose_device_t*>(data_.data())[index];
  }

  compose_device_t* operator->() { return reinterpret_cast<compose_device_t*>(data_.data()); }

  const compose_device_t* operator->() const {
    return reinterpret_cast<const compose_device_t*>(data_.data());
  }

  size_t size() const { return data_.size(); }

 private:
  std::vector<uint8_t> data_;
};

using compose_device_v2_t = struct compose_device_v2 {
  uint32_t version;
  uint32_t display_id;
  uint32_t target_handle;
  uint32_t num_layers;
  struct compose_layer layers[0];
};

class ComposeDeviceV2 {
 public:
  explicit ComposeDeviceV2(size_t num_layers)
      : data_(sizeof(compose_device_v2_t) + num_layers * sizeof(ComposeLayer), 0u) {}

  compose_device_v2_t* get() { return reinterpret_cast<compose_device_v2_t*>(data_.data()); }

  const compose_device_v2_t* get() const {
    return reinterpret_cast<const compose_device_v2_t*>(data_.data());
  }

  compose_device_v2_t& operator[](size_t index) {
    return reinterpret_cast<compose_device_v2_t*>(data_.data())[index];
  }

  const compose_device_v2_t& operator[](size_t index) const {
    return reinterpret_cast<const compose_device_v2_t*>(data_.data())[index];
  }

  compose_device_v2_t* operator->() { return reinterpret_cast<compose_device_v2_t*>(data_.data()); }

  const compose_device_v2_t* operator->() const {
    return reinterpret_cast<const compose_device_v2_t*>(data_.data());
  }

  size_t size() const { return data_.size(); }

 private:
  std::vector<uint8_t> data_;
};

}  // namespace goldfish::hwc

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_GOLDFISH_DISPLAY_THIRD_PARTY_AOSP_HWCOMPOSER_H_
