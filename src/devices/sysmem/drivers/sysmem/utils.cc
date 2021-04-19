// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "utils.h"

#include <inttypes.h>

bool IsWriteUsage(const fuchsia_sysmem2::wire::BufferUsage& buffer_usage) {
  const uint32_t kCpuWriteBits =
      fuchsia_sysmem2::wire::kCpuUsageWriteOften | fuchsia_sysmem2::wire::kCpuUsageWrite;
  // This list may not be complete.
  const uint32_t kVulkanWriteBits =
      fuchsia_sysmem2::wire::kVulkanUsageTransferDst | fuchsia_sysmem2::wire::kVulkanUsageStorage;
  // Display usages don't include any writing.
  const uint32_t kDisplayWriteBits = 0;
  const uint32_t kVideoWriteBits = fuchsia_sysmem2::wire::kVideoUsageHwDecoder |
                                   fuchsia_sysmem2::wire::kVideoUsageHwDecoderInternal |
                                   fuchsia_sysmem2::wire::kVideoUsageDecryptorOutput |
                                   fuchsia_sysmem2::wire::kVideoUsageHwEncoder;

  uint32_t cpu = buffer_usage.has_cpu() ? buffer_usage.cpu() : 0;
  uint32_t vulkan = buffer_usage.has_vulkan() ? buffer_usage.vulkan() : 0;
  uint32_t display = buffer_usage.has_display() ? buffer_usage.display() : 0;
  uint32_t video = buffer_usage.has_video() ? buffer_usage.video() : 0;

  bool is_write_needed = (cpu & kCpuWriteBits) || (vulkan & kVulkanWriteBits) ||
                         (display & kDisplayWriteBits) || (video & kVideoWriteBits);

  return is_write_needed;
}

bool IsCpuUsage(const fuchsia_sysmem2::wire::BufferUsage& buffer_usage) {
  uint32_t cpu = buffer_usage.has_cpu() ? buffer_usage.cpu() : 0;
  return cpu != 0;
}

bool IsAnyUsage(const fuchsia_sysmem2::wire::BufferUsage& buffer_usage) {
  // none() is intentionally missing here
  uint32_t cpu = buffer_usage.has_cpu() ? buffer_usage.cpu() : 0;
  uint32_t vulkan = buffer_usage.has_vulkan() ? buffer_usage.vulkan() : 0;
  uint32_t display = buffer_usage.has_display() ? buffer_usage.display() : 0;
  uint32_t video = buffer_usage.has_video() ? buffer_usage.video() : 0;
  return cpu || vulkan || display || video;
}
