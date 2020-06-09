// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "utils.h"

#include <inttypes.h>

bool IsWriteUsage(const llcpp::fuchsia::sysmem2::BufferUsage& buffer_usage) {
  const uint32_t kCpuWriteBits =
      llcpp::fuchsia::sysmem2::CPU_USAGE_WRITE_OFTEN | llcpp::fuchsia::sysmem2::CPU_USAGE_WRITE;
  // This list may not be complete.
  const uint32_t kVulkanWriteBits = llcpp::fuchsia::sysmem2::VULKAN_USAGE_TRANSFER_DST |
                                    llcpp::fuchsia::sysmem2::VULKAN_USAGE_STORAGE;
  // Display usages don't include any writing.
  const uint32_t kDisplayWriteBits = 0;
  const uint32_t kVideoWriteBits = llcpp::fuchsia::sysmem2::VIDEO_USAGE_HW_DECODER |
                                   llcpp::fuchsia::sysmem2::VIDEO_USAGE_HW_DECODER_INTERNAL |
                                   llcpp::fuchsia::sysmem2::VIDEO_USAGE_DECRYPTOR_OUTPUT |
                                   llcpp::fuchsia::sysmem2::VIDEO_USAGE_HW_ENCODER;

  uint32_t cpu = buffer_usage.has_cpu() ? buffer_usage.cpu() : 0;
  uint32_t vulkan = buffer_usage.has_vulkan() ? buffer_usage.vulkan() : 0;
  uint32_t display = buffer_usage.has_display() ? buffer_usage.display() : 0;
  uint32_t video = buffer_usage.has_video() ? buffer_usage.video() : 0;

  bool is_write_needed = (cpu & kCpuWriteBits) || (vulkan & kVulkanWriteBits) ||
                         (display & kDisplayWriteBits) || (video & kVideoWriteBits);

  return is_write_needed;
}

bool IsCpuUsage(const llcpp::fuchsia::sysmem2::BufferUsage& buffer_usage) {
  uint32_t cpu = buffer_usage.has_cpu() ? buffer_usage.cpu() : 0;
  return cpu != 0;
}

bool IsAnyUsage(const llcpp::fuchsia::sysmem2::BufferUsage& buffer_usage) {
  // none() is intentionally missing here
  uint32_t cpu = buffer_usage.has_cpu() ? buffer_usage.cpu() : 0;
  uint32_t vulkan = buffer_usage.has_vulkan() ? buffer_usage.vulkan() : 0;
  uint32_t display = buffer_usage.has_display() ? buffer_usage.display() : 0;
  uint32_t video = buffer_usage.has_video() ? buffer_usage.video() : 0;
  return cpu || vulkan || display || video;
}
