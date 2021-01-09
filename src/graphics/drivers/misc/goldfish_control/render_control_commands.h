// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DRIVERS_MISC_GOLDFISH_CONTROL_RENDER_CONTROL_COMMANDS_H_
#define SRC_GRAPHICS_DRIVERS_MISC_GOLDFISH_CONTROL_RENDER_CONTROL_COMMANDS_H_

#include <cstdint>

namespace goldfish {

struct CreateColorBufferCmd {
  uint32_t op;
  uint32_t size;
  uint32_t width;
  uint32_t height;
  uint32_t internalformat;
};
constexpr uint32_t kOP_rcCreateColorBuffer = 10012;
constexpr uint32_t kSize_rcCreateColorBuffer = 20;

struct CloseColorBufferCmd {
  uint32_t op;
  uint32_t size;
  uint32_t id;
};
constexpr uint32_t kOP_rcCloseColorBuffer = 10014;
constexpr uint32_t kSize_rcCloseColorBuffer = 12;

struct CloseBufferCmd {
  uint32_t op;
  uint32_t size;
  uint32_t id;
};
constexpr uint32_t kOP_rcCloseBuffer = 10050;
constexpr uint32_t kSize_rcCloseBuffer = 12;

struct SetColorBufferVulkanMode2Cmd {
  uint32_t op;
  uint32_t size;
  uint32_t id;
  uint32_t mode;
  uint32_t memory_property;
};
constexpr uint32_t kOP_rcSetColorBufferVulkanMode2 = 10051;
constexpr uint32_t kSize_rcSetColorBufferVulkanMode2 = 20;

struct __attribute__((__packed__)) MapGpaToBufferHandle2Cmd {
  uint32_t op;
  uint32_t size;
  uint32_t id;
  uint64_t gpa;
  uint64_t map_size;
};
constexpr uint32_t kOP_rcMapGpaToBufferHandle2 = 10054;
constexpr uint32_t kSize_rcMapGpaToBufferHandle2 = 28;

struct __attribute__((__packed__)) CreateBuffer2Cmd {
  uint32_t op;
  uint32_t size;
  uint64_t buffer_size;
  uint32_t memory_property;
};
constexpr uint32_t kOP_rcCreateBuffer2 = 10053;
constexpr uint32_t kSize_rcCreateBuffer2 = 20;

}  // namespace goldfish

#endif  // SRC_GRAPHICS_DRIVERS_MISC_GOLDFISH_CONTROL_RENDER_CONTROL_COMMANDS_H_
