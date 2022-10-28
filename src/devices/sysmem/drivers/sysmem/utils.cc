// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "utils.h"

#include <inttypes.h>

bool IsWriteUsage(const fuchsia_sysmem2::BufferUsage& buffer_usage) {
  const uint32_t kCpuWriteBits =
      fuchsia_sysmem2::kCpuUsageWriteOften | fuchsia_sysmem2::kCpuUsageWrite;
  // This list may not be complete.
  const uint32_t kVulkanWriteBits =
      fuchsia_sysmem2::kVulkanUsageTransferDst | fuchsia_sysmem2::kVulkanUsageStorage;
  // Display usages don't include any writing.
  const uint32_t kDisplayWriteBits = 0;
  const uint32_t kVideoWriteBits =
      fuchsia_sysmem2::kVideoUsageHwDecoder | fuchsia_sysmem2::kVideoUsageHwDecoderInternal |
      fuchsia_sysmem2::kVideoUsageDecryptorOutput | fuchsia_sysmem2::kVideoUsageHwEncoder;

  uint32_t cpu = buffer_usage.cpu().has_value() ? buffer_usage.cpu().value() : 0;
  uint32_t vulkan = buffer_usage.vulkan().has_value() ? buffer_usage.vulkan().value() : 0;
  uint32_t display = buffer_usage.display().has_value() ? buffer_usage.display().value() : 0;
  uint32_t video = buffer_usage.video().has_value() ? buffer_usage.video().value() : 0;

  bool is_write_needed = (cpu & kCpuWriteBits) || (vulkan & kVulkanWriteBits) ||
                         (display & kDisplayWriteBits) || (video & kVideoWriteBits);

  return is_write_needed;
}

bool IsCpuUsage(const fuchsia_sysmem2::BufferUsage& buffer_usage) {
  uint32_t cpu = buffer_usage.cpu().has_value() ? buffer_usage.cpu().value() : 0;
  return cpu != 0;
}

bool IsAnyUsage(const fuchsia_sysmem2::BufferUsage& buffer_usage) {
  // none() is intentionally missing here
  uint32_t cpu = buffer_usage.cpu().has_value() ? buffer_usage.cpu().value() : 0;
  uint32_t vulkan = buffer_usage.vulkan().has_value() ? buffer_usage.vulkan().value() : 0;
  uint32_t display = buffer_usage.display().has_value() ? buffer_usage.display().value() : 0;
  uint32_t video = buffer_usage.video().has_value() ? buffer_usage.video().value() : 0;
  return cpu || vulkan || display || video;
}
