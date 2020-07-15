// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "usage_pixel_format_cost.h"

#include <fuchsia/sysmem2/llcpp/fidl.h>
#include <lib/fidl/llcpp/allocator.h>
#include <lib/image-format/image_format.h>
#include <lib/sysmem-make-tracking/make_tracking.h>
#include <zircon/assert.h>

#include <list>
#include <map>

#include <ddk/platform-defs.h>

#include "macros.h"

namespace sysmem_driver {

namespace {

// The local Platform definition is different than the PID(s) in platform-defs.h
// in that this local enum includes values that can be used as catch-all for any
// PID in a set of PIDs, typically named with a _GENERIC suffix.
//
// Membership of a PID in a more _GENERIC category is via the next_platform
// field.
//
// Some values of this enum are 1:1 with specific PID values, while others are
// essentially more generic categories (groupings) of PID values.  This allows
// an entry for a more-specific Platform value to effectively share entries of
// a more-generic Platform value.
enum Platform {
  kPlatform_None,
  kPlatform_Generic,
  kPlatform_Arm_Mali,
  kPlatform_Amlogic_Generic,
  kPlatform_Amlogic_S912,
  kPlatform_Amlogic_S905D2,
  kPlatform_Amlogic_T931,
};

constexpr uint64_t MakeVidPidKey(uint32_t vid, uint32_t pid) {
  return (static_cast<uint64_t>(vid) << 32) | pid;
}

// Map from PID (platform id) to Platform value.
const std::map<uint64_t, Platform> kPlatformTranslation = {
    {MakeVidPidKey(PDEV_VID_AMLOGIC, PDEV_PID_AMLOGIC_S912), kPlatform_Amlogic_S912},
    {MakeVidPidKey(PDEV_VID_AMLOGIC, PDEV_PID_AMLOGIC_S905D2), kPlatform_Amlogic_S905D2},
    {MakeVidPidKey(PDEV_VID_AMLOGIC, PDEV_PID_AMLOGIC_T931), kPlatform_Amlogic_T931},
};

// A UsagePixelFormatCostEntry with more query usage bits in
// required_buffer_usage_bits is considered a closer match.
//
// If two UsagePixelFormatCostEntry(s) have equal number of corresponding usage
// bits in required_buffer_usage_bits, the entry occurring first in the overall
// effective list of CostEntry(s) for the PID is preferred.  This causes any
// later-listed otherwise-equally-close-match to be ignored.
struct UsagePixelFormatCostEntry {
  // The query's pixel_format must match for this entry to be considered.
  llcpp::fuchsia::sysmem2::PixelFormat pixel_format;
  // A query's usage bits must contain all these usage bits for this entry to
  // be considered.
  llcpp::fuchsia::sysmem2::BufferUsage required_buffer_usage_bits;
  // First the entry that's the best match for the GetCost() query is
  // determined.  If this entry is selected as the best match for the query,
  // this is the cost returned by GetCost().
  const double cost;
};

struct PlatformCostsEntry {
  // platform
  const Platform platform;
  // The next_platform can be kPlatform_None in which case the effective
  // overall list is terminated, or next_pid can chain into another Platform
  // enum value which will be considered part of this platform's list.  In
  // this way, more specific Platform values can chain into less-specific
  // platform values.
  const Platform next_platform;

  const std::list<const UsagePixelFormatCostEntry>& costs;
};

// Since we know exactly how much space we need to avoid using heap, and because this buffer is
// exactly full of stuff that has trivial dtor, we can (marginally) justify avoiding the heap for
// this stuff, since I happen to already know the appropriate size this time.  However, it's a good
// idea to avoid spending the time to update this number each time more entries are added, since the
// opportunity cost of that time will almost certainly be more than any real savings from updating
// this number.
constexpr size_t kBufferThenHeapAllocatorSize = 3792;
fidl::BufferThenHeapAllocator<kBufferThenHeapAllocatorSize> allocator;

const std::list<const UsagePixelFormatCostEntry> kArm_Mali_Cost_Entries = [] {
  std::list<const UsagePixelFormatCostEntry> result;
  // AFBC TE is best.
  result.emplace_back(UsagePixelFormatCostEntry{
      // .pixel_format
      allocator.make_table_builder<llcpp::fuchsia::sysmem2::PixelFormat>()
          .set_type(
              sysmem::MakeTracking(&allocator, llcpp::fuchsia::sysmem2::PixelFormatType::BGRA32))
          .set_format_modifier_value(sysmem::MakeTracking(
              &allocator, llcpp::fuchsia::sysmem2::FORMAT_MODIFIER_ARM_AFBC_16X16_TE))
          .build(),
      // .required_buffer_usage_bits
      allocator.make_table_builder<llcpp::fuchsia::sysmem2::BufferUsage>()
          .set_none(sysmem::MakeTracking(&allocator, 0u))
          .set_cpu(sysmem::MakeTracking(&allocator, 0u))
          .set_vulkan(sysmem::MakeTracking(&allocator, 0u))
          .set_display(sysmem::MakeTracking(&allocator, 0u))
          .set_video(sysmem::MakeTracking(&allocator, 0u))
          .build(),
      // .cost
      500.0L,
  });
  result.emplace_back(UsagePixelFormatCostEntry{
      // .pixel_format
      allocator.make_table_builder<llcpp::fuchsia::sysmem2::PixelFormat>()
          .set_type(
              sysmem::MakeTracking(&allocator, llcpp::fuchsia::sysmem2::PixelFormatType::R8G8B8A8))
          .set_format_modifier_value(sysmem::MakeTracking(
              &allocator, llcpp::fuchsia::sysmem2::FORMAT_MODIFIER_ARM_AFBC_16X16_TE))
          .build(),
      // .required_buffer_usage_bits
      allocator.make_table_builder<llcpp::fuchsia::sysmem2::BufferUsage>()
          .set_none(sysmem::MakeTracking(&allocator, 0u))
          .set_cpu(sysmem::MakeTracking(&allocator, 0u))
          .set_vulkan(sysmem::MakeTracking(&allocator, 0u))
          .set_display(sysmem::MakeTracking(&allocator, 0u))
          .set_video(sysmem::MakeTracking(&allocator, 0u))
          .build(),
      // .cost
      500.0L,
  });
  result.emplace_back(UsagePixelFormatCostEntry{
      // .pixel_format
      allocator.make_table_builder<llcpp::fuchsia::sysmem2::PixelFormat>()
          .set_type(
              sysmem::MakeTracking(&allocator, llcpp::fuchsia::sysmem2::PixelFormatType::BGRA32))
          .set_format_modifier_value(sysmem::MakeTracking(
              &allocator, llcpp::fuchsia::sysmem2::FORMAT_MODIFIER_ARM_AFBC_32X8_TE))
          .build(),
      // .required_buffer_usage_bits
      allocator.make_table_builder<llcpp::fuchsia::sysmem2::BufferUsage>()
          .set_none(sysmem::MakeTracking(&allocator, 0u))
          .set_cpu(sysmem::MakeTracking(&allocator, 0u))
          .set_vulkan(sysmem::MakeTracking(&allocator, 0u))
          .set_display(sysmem::MakeTracking(&allocator, 0u))
          .set_video(sysmem::MakeTracking(&allocator, 0u))
          .build(),
      // .cost
      500.0L,
  });
  result.emplace_back(UsagePixelFormatCostEntry{
      // .pixel_format
      allocator.make_table_builder<llcpp::fuchsia::sysmem2::PixelFormat>()
          .set_type(
              sysmem::MakeTracking(&allocator, llcpp::fuchsia::sysmem2::PixelFormatType::R8G8B8A8))
          .set_format_modifier_value(sysmem::MakeTracking(
              &allocator, llcpp::fuchsia::sysmem2::FORMAT_MODIFIER_ARM_AFBC_32X8_TE))
          .build(),
      // .required_buffer_usage_bits
      allocator.make_table_builder<llcpp::fuchsia::sysmem2::BufferUsage>()
          .set_none(sysmem::MakeTracking(&allocator, 0u))
          .set_cpu(sysmem::MakeTracking(&allocator, 0u))
          .set_vulkan(sysmem::MakeTracking(&allocator, 0u))
          .set_display(sysmem::MakeTracking(&allocator, 0u))
          .set_video(sysmem::MakeTracking(&allocator, 0u))
          .build(),
      // .cost
      500.0L,
  });
  // AFBC always preferred when supported.
  result.emplace_back(UsagePixelFormatCostEntry{
      // .pixel_format
      allocator.make_table_builder<llcpp::fuchsia::sysmem2::PixelFormat>()
          .set_type(
              sysmem::MakeTracking(&allocator, llcpp::fuchsia::sysmem2::PixelFormatType::BGRA32))
          .set_format_modifier_value(sysmem::MakeTracking(
              &allocator, llcpp::fuchsia::sysmem2::FORMAT_MODIFIER_ARM_AFBC_16X16))
          .build(),
      // .required_buffer_usage_bits
      allocator.make_table_builder<llcpp::fuchsia::sysmem2::BufferUsage>()
          .set_none(sysmem::MakeTracking(&allocator, 0u))
          .set_cpu(sysmem::MakeTracking(&allocator, 0u))
          .set_vulkan(sysmem::MakeTracking(&allocator, 0u))
          .set_display(sysmem::MakeTracking(&allocator, 0u))
          .set_video(sysmem::MakeTracking(&allocator, 0u))
          .build(),
      // .cost
      1000.0L,
  });
  result.emplace_back(UsagePixelFormatCostEntry{
      // .pixel_format
      allocator.make_table_builder<llcpp::fuchsia::sysmem2::PixelFormat>()
          .set_type(
              sysmem::MakeTracking(&allocator, llcpp::fuchsia::sysmem2::PixelFormatType::R8G8B8A8))
          .set_format_modifier_value(sysmem::MakeTracking(
              &allocator, llcpp::fuchsia::sysmem2::FORMAT_MODIFIER_ARM_AFBC_16X16))
          .build(),
      // .required_buffer_usage_bits
      allocator.make_table_builder<llcpp::fuchsia::sysmem2::BufferUsage>()
          .set_none(sysmem::MakeTracking(&allocator, 0u))
          .set_cpu(sysmem::MakeTracking(&allocator, 0u))
          .set_vulkan(sysmem::MakeTracking(&allocator, 0u))
          .set_display(sysmem::MakeTracking(&allocator, 0u))
          .set_video(sysmem::MakeTracking(&allocator, 0u))
          .build(),
      // .cost
      1000.0L,
  });
  result.emplace_back(UsagePixelFormatCostEntry{
      // .pixel_format
      allocator.make_table_builder<llcpp::fuchsia::sysmem2::PixelFormat>()
          .set_type(
              sysmem::MakeTracking(&allocator, llcpp::fuchsia::sysmem2::PixelFormatType::BGRA32))
          .set_format_modifier_value(sysmem::MakeTracking(
              &allocator, llcpp::fuchsia::sysmem2::FORMAT_MODIFIER_ARM_AFBC_32X8))
          .build(),
      // .required_buffer_usage_bits
      allocator.make_table_builder<llcpp::fuchsia::sysmem2::BufferUsage>()
          .set_none(sysmem::MakeTracking(&allocator, 0u))
          .set_cpu(sysmem::MakeTracking(&allocator, 0u))
          .set_vulkan(sysmem::MakeTracking(&allocator, 0u))
          .set_display(sysmem::MakeTracking(&allocator, 0u))
          .set_video(sysmem::MakeTracking(&allocator, 0u))
          .build(),
      // .cost
      1000.0L,
  });
  result.emplace_back(UsagePixelFormatCostEntry{
      // .pixel_format
      allocator.make_table_builder<llcpp::fuchsia::sysmem2::PixelFormat>()
          .set_type(
              sysmem::MakeTracking(&allocator, llcpp::fuchsia::sysmem2::PixelFormatType::R8G8B8A8))
          .set_format_modifier_value(sysmem::MakeTracking(
              &allocator, llcpp::fuchsia::sysmem2::FORMAT_MODIFIER_ARM_AFBC_32X8))
          .build(),
      // .required_buffer_usage_bits
      allocator.make_table_builder<llcpp::fuchsia::sysmem2::BufferUsage>()
          .set_none(sysmem::MakeTracking(&allocator, 0u))
          .set_cpu(sysmem::MakeTracking(&allocator, 0u))
          .set_vulkan(sysmem::MakeTracking(&allocator, 0u))
          .set_display(sysmem::MakeTracking(&allocator, 0u))
          .set_video(sysmem::MakeTracking(&allocator, 0u))
          .build(),
      // .cost
      1000.0L,
  });
  // Linear TE is better than linear.
  result.emplace_back(UsagePixelFormatCostEntry{
      // .pixel_format
      allocator.make_table_builder<llcpp::fuchsia::sysmem2::PixelFormat>()
          .set_type(
              sysmem::MakeTracking(&allocator, llcpp::fuchsia::sysmem2::PixelFormatType::BGRA32))
          .set_format_modifier_value(sysmem::MakeTracking(
              &allocator, llcpp::fuchsia::sysmem2::FORMAT_MODIFIER_ARM_LINEAR_TE))
          .build(),
      // .required_buffer_usage_bits
      allocator.make_table_builder<llcpp::fuchsia::sysmem2::BufferUsage>()
          .set_none(sysmem::MakeTracking(&allocator, 0u))
          .set_cpu(sysmem::MakeTracking(&allocator, 0u))
          .set_vulkan(sysmem::MakeTracking(&allocator, 0u))
          .set_display(sysmem::MakeTracking(&allocator, 0u))
          .set_video(sysmem::MakeTracking(&allocator, 0u))
          .build(),
      // .cost
      1500.0L,
  });
  result.emplace_back(UsagePixelFormatCostEntry{
      // .pixel_format
      allocator.make_table_builder<llcpp::fuchsia::sysmem2::PixelFormat>()
          .set_type(
              sysmem::MakeTracking(&allocator, llcpp::fuchsia::sysmem2::PixelFormatType::R8G8B8A8))
          .set_format_modifier_value(sysmem::MakeTracking(
              &allocator, llcpp::fuchsia::sysmem2::FORMAT_MODIFIER_ARM_LINEAR_TE))
          .build(),
      // .required_buffer_usage_bits
      allocator.make_table_builder<llcpp::fuchsia::sysmem2::BufferUsage>()
          .set_none(sysmem::MakeTracking(&allocator, 0u))
          .set_cpu(sysmem::MakeTracking(&allocator, 0u))
          .set_vulkan(sysmem::MakeTracking(&allocator, 0u))
          .set_display(sysmem::MakeTracking(&allocator, 0u))
          .set_video(sysmem::MakeTracking(&allocator, 0u))
          .build(),
      // .cost
      1500.0L,
  });
  result.emplace_back(UsagePixelFormatCostEntry{
      // .pixel_format
      allocator.make_table_builder<llcpp::fuchsia::sysmem2::PixelFormat>()
          .set_type(
              sysmem::MakeTracking(&allocator, llcpp::fuchsia::sysmem2::PixelFormatType::BGRA32))
          .set_format_modifier_value(sysmem::MakeTracking(
              &allocator, llcpp::fuchsia::sysmem2::FORMAT_MODIFIER_ARM_LINEAR_TE))
          .build(),
      // .required_buffer_usage_bits
      allocator.make_table_builder<llcpp::fuchsia::sysmem2::BufferUsage>()
          .set_none(sysmem::MakeTracking(&allocator, 0u))
          .set_cpu(sysmem::MakeTracking(&allocator, 0u))
          .set_vulkan(sysmem::MakeTracking(&allocator, 0u))
          .set_display(sysmem::MakeTracking(&allocator, 0u))
          .set_video(sysmem::MakeTracking(&allocator, 0u))
          .build(),
      // .cost
      1500.0L,
  });
  result.emplace_back(UsagePixelFormatCostEntry{
      // .pixel_format
      allocator.make_table_builder<llcpp::fuchsia::sysmem2::PixelFormat>()
          .set_type(
              sysmem::MakeTracking(&allocator, llcpp::fuchsia::sysmem2::PixelFormatType::R8G8B8A8))
          .set_format_modifier_value(sysmem::MakeTracking(
              &allocator, llcpp::fuchsia::sysmem2::FORMAT_MODIFIER_ARM_LINEAR_TE))
          .build(),
      // .required_buffer_usage_bits
      allocator.make_table_builder<llcpp::fuchsia::sysmem2::BufferUsage>()
          .set_none(sysmem::MakeTracking(&allocator, 0u))
          .set_cpu(sysmem::MakeTracking(&allocator, 0u))
          .set_vulkan(sysmem::MakeTracking(&allocator, 0u))
          .set_display(sysmem::MakeTracking(&allocator, 0u))
          .set_video(sysmem::MakeTracking(&allocator, 0u))
          .build(),
      // .cost
      1500.0L,
  });
  // AFBC TE is best. Split block is slightly worse than non-split-block for GPU<->GPU, but better
  // for GPU->display.
  result.emplace_back(UsagePixelFormatCostEntry{
      // .pixel_format
      allocator.make_table_builder<llcpp::fuchsia::sysmem2::PixelFormat>()
          .set_type(
              sysmem::MakeTracking(&allocator, llcpp::fuchsia::sysmem2::PixelFormatType::BGRA32))
          .set_format_modifier_value(sysmem::MakeTracking(
              &allocator,
              llcpp::fuchsia::sysmem2::FORMAT_MODIFIER_ARM_AFBC_16X16_SPLIT_BLOCK_SPARSE_YUV_TE))
          .build(),
      // .required_buffer_usage_bits
      allocator.make_table_builder<llcpp::fuchsia::sysmem2::BufferUsage>()
          .set_none(sysmem::MakeTracking(&allocator, 0u))
          .set_cpu(sysmem::MakeTracking(&allocator, 0u))
          .set_vulkan(sysmem::MakeTracking(&allocator, 0u))
          .set_display(sysmem::MakeTracking(&allocator, 0u))
          .set_video(sysmem::MakeTracking(&allocator, 0u))
          .build(),
      // .cost
      600.0L,
  });
  result.emplace_back(UsagePixelFormatCostEntry{
      // .pixel_format
      allocator.make_table_builder<llcpp::fuchsia::sysmem2::PixelFormat>()
          .set_type(
              sysmem::MakeTracking(&allocator, llcpp::fuchsia::sysmem2::PixelFormatType::R8G8B8A8))
          .set_format_modifier_value(sysmem::MakeTracking(
              &allocator,
              llcpp::fuchsia::sysmem2::FORMAT_MODIFIER_ARM_AFBC_16X16_SPLIT_BLOCK_SPARSE_YUV_TE))
          .build(),
      // .required_buffer_usage_bits
      allocator.make_table_builder<llcpp::fuchsia::sysmem2::BufferUsage>()
          .set_none(sysmem::MakeTracking(&allocator, 0u))
          .set_cpu(sysmem::MakeTracking(&allocator, 0u))
          .set_vulkan(sysmem::MakeTracking(&allocator, 0u))
          .set_display(sysmem::MakeTracking(&allocator, 0u))
          .set_video(sysmem::MakeTracking(&allocator, 0u))
          .build(),
      // .cost
      600.0L,
  });
  result.emplace_back(UsagePixelFormatCostEntry{
      // .pixel_format
      allocator.make_table_builder<llcpp::fuchsia::sysmem2::PixelFormat>()
          .set_type(
              sysmem::MakeTracking(&allocator, llcpp::fuchsia::sysmem2::PixelFormatType::BGRA32))
          .set_format_modifier_value(sysmem::MakeTracking(
              &allocator,
              llcpp::fuchsia::sysmem2::FORMAT_MODIFIER_ARM_AFBC_16X16_SPLIT_BLOCK_SPARSE_YUV))
          .build(),
      // .required_buffer_usage_bits
      allocator.make_table_builder<llcpp::fuchsia::sysmem2::BufferUsage>()
          .set_none(sysmem::MakeTracking(&allocator, 0u))
          .set_cpu(sysmem::MakeTracking(&allocator, 0u))
          .set_vulkan(sysmem::MakeTracking(&allocator, 0u))
          .set_display(sysmem::MakeTracking(&allocator, 0u))
          .set_video(sysmem::MakeTracking(&allocator, 0u))
          .build(),
      // .cost
      1100.0L,
  });
  result.emplace_back(UsagePixelFormatCostEntry{
      // .pixel_format
      allocator.make_table_builder<llcpp::fuchsia::sysmem2::PixelFormat>()
          .set_type(
              sysmem::MakeTracking(&allocator, llcpp::fuchsia::sysmem2::PixelFormatType::R8G8B8A8))
          .set_format_modifier_value(sysmem::MakeTracking(
              &allocator,
              llcpp::fuchsia::sysmem2::FORMAT_MODIFIER_ARM_AFBC_16X16_SPLIT_BLOCK_SPARSE_YUV))
          .build(),
      // .required_buffer_usage_bits
      allocator.make_table_builder<llcpp::fuchsia::sysmem2::BufferUsage>()
          .set_none(sysmem::MakeTracking(&allocator, 0u))
          .set_cpu(sysmem::MakeTracking(&allocator, 0u))
          .set_vulkan(sysmem::MakeTracking(&allocator, 0u))
          .set_display(sysmem::MakeTracking(&allocator, 0u))
          .set_video(sysmem::MakeTracking(&allocator, 0u))
          .build(),
      // .cost
      1100.0L,
  });
  return result;
}();

const PlatformCostsEntry kArm_Mali_Costs = {
    .platform = kPlatform_Arm_Mali,
    .next_platform = kPlatform_Generic,
    .costs = kArm_Mali_Cost_Entries,
};

const std::list<const UsagePixelFormatCostEntry> kAmlogic_Generic_Cost_Entries = [] {
  std::list<const UsagePixelFormatCostEntry> result;
  result.emplace_back(
      // NV12 weakly preferred for VIDEO_USAGE_HW_DECODER.
      UsagePixelFormatCostEntry{
          // .pixel_format
          allocator.make_table_builder<llcpp::fuchsia::sysmem2::PixelFormat>()
              .set_type(
                  sysmem::MakeTracking(&allocator, llcpp::fuchsia::sysmem2::PixelFormatType::NV12))
              .build(),
          // .required_buffer_usage_bits
          allocator.make_table_builder<llcpp::fuchsia::sysmem2::BufferUsage>()
              .set_none(sysmem::MakeTracking(&allocator, 0u))
              .set_cpu(sysmem::MakeTracking(&allocator, 0u))
              .set_vulkan(sysmem::MakeTracking(&allocator, 0u))
              .set_display(sysmem::MakeTracking(&allocator, 0u))
              .set_video(
                  sysmem::MakeTracking(&allocator, llcpp::fuchsia::sysmem2::VIDEO_USAGE_HW_DECODER))
              .build(),
          // .cost
          100.0L,
      });
  return result;
}();

// These costs are expected to be true on every platform.
const std::list<const UsagePixelFormatCostEntry> kGeneric_Cost_Entries = [] {
  std::list<const UsagePixelFormatCostEntry> result;
  result.emplace_back(UsagePixelFormatCostEntry{
      // .pixel_format
      allocator.make_table_builder<llcpp::fuchsia::sysmem2::PixelFormat>()
          .set_type(
              sysmem::MakeTracking(&allocator, llcpp::fuchsia::sysmem2::PixelFormatType::BGRA32))
          .set_format_modifier_value(sysmem::MakeTracking(
              &allocator, llcpp::fuchsia::sysmem2::FORMAT_MODIFIER_INTEL_I915_X_TILED))
          .build(),
      // .required_buffer_usage_bits
      allocator.make_table_builder<llcpp::fuchsia::sysmem2::BufferUsage>()
          .set_none(sysmem::MakeTracking(&allocator, 0u))
          .set_cpu(sysmem::MakeTracking(&allocator, 0u))
          .set_vulkan(sysmem::MakeTracking(&allocator, 0u))
          .set_display(sysmem::MakeTracking(&allocator, 0u))
          .set_video(sysmem::MakeTracking(&allocator, 0u))
          .build(),
      // .cost
      1000.0L,
  });
  result.emplace_back(UsagePixelFormatCostEntry{
      // .pixel_format
      allocator.make_table_builder<llcpp::fuchsia::sysmem2::PixelFormat>()
          .set_type(
              sysmem::MakeTracking(&allocator, llcpp::fuchsia::sysmem2::PixelFormatType::BGRA32))
          .set_format_modifier_value(sysmem::MakeTracking(
              &allocator, llcpp::fuchsia::sysmem2::FORMAT_MODIFIER_INTEL_I915_YF_TILED))
          .build(),
      // .required_buffer_usage_bits
      allocator.make_table_builder<llcpp::fuchsia::sysmem2::BufferUsage>()
          .set_none(sysmem::MakeTracking(&allocator, 0u))
          .set_cpu(sysmem::MakeTracking(&allocator, 0u))
          .set_vulkan(sysmem::MakeTracking(&allocator, 0u))
          .set_display(sysmem::MakeTracking(&allocator, 0u))
          .set_video(sysmem::MakeTracking(&allocator, 0u))
          .build(),
      // .cost
      1000.0L,
  });
  result.emplace_back(UsagePixelFormatCostEntry{
      // .pixel_format
      allocator.make_table_builder<llcpp::fuchsia::sysmem2::PixelFormat>()
          .set_type(
              sysmem::MakeTracking(&allocator, llcpp::fuchsia::sysmem2::PixelFormatType::BGRA32))
          .set_format_modifier_value(sysmem::MakeTracking(
              &allocator, llcpp::fuchsia::sysmem2::FORMAT_MODIFIER_INTEL_I915_Y_TILED))
          .build(),
      // .required_buffer_usage_bits
      allocator.make_table_builder<llcpp::fuchsia::sysmem2::BufferUsage>()
          .set_none(sysmem::MakeTracking(&allocator, 0u))
          .set_cpu(sysmem::MakeTracking(&allocator, 0u))
          .set_vulkan(sysmem::MakeTracking(&allocator, 0u))
          .set_display(sysmem::MakeTracking(&allocator, 0u))
          .set_video(sysmem::MakeTracking(&allocator, 0u))
          .build(),
      // .cost
      1000.0L,
  });
  result.emplace_back(UsagePixelFormatCostEntry{
      // .pixel_format
      allocator.make_table_builder<llcpp::fuchsia::sysmem2::PixelFormat>()
          .set_type(
              sysmem::MakeTracking(&allocator, llcpp::fuchsia::sysmem2::PixelFormatType::R8G8B8A8))
          .set_format_modifier_value(sysmem::MakeTracking(
              &allocator, llcpp::fuchsia::sysmem2::FORMAT_MODIFIER_INTEL_I915_X_TILED))
          .build(),
      // .required_buffer_usage_bits
      allocator.make_table_builder<llcpp::fuchsia::sysmem2::BufferUsage>()
          .set_none(sysmem::MakeTracking(&allocator, 0u))
          .set_cpu(sysmem::MakeTracking(&allocator, 0u))
          .set_vulkan(sysmem::MakeTracking(&allocator, 0u))
          .set_display(sysmem::MakeTracking(&allocator, 0u))
          .set_video(sysmem::MakeTracking(&allocator, 0u))
          .build(),
      // .cost
      1000.0L,
  });
  result.emplace_back(UsagePixelFormatCostEntry{
      // .pixel_format
      allocator.make_table_builder<llcpp::fuchsia::sysmem2::PixelFormat>()
          .set_type(
              sysmem::MakeTracking(&allocator, llcpp::fuchsia::sysmem2::PixelFormatType::R8G8B8A8))
          .set_format_modifier_value(sysmem::MakeTracking(
              &allocator, llcpp::fuchsia::sysmem2::FORMAT_MODIFIER_INTEL_I915_YF_TILED))
          .build(),
      // .required_buffer_usage_bits
      allocator.make_table_builder<llcpp::fuchsia::sysmem2::BufferUsage>()
          .set_none(sysmem::MakeTracking(&allocator, 0u))
          .set_cpu(sysmem::MakeTracking(&allocator, 0u))
          .set_vulkan(sysmem::MakeTracking(&allocator, 0u))
          .set_display(sysmem::MakeTracking(&allocator, 0u))
          .set_video(sysmem::MakeTracking(&allocator, 0u))
          .build(),
      // .cost
      1000.0L,
  });
  result.emplace_back(UsagePixelFormatCostEntry{
      // .pixel_format
      allocator.make_table_builder<llcpp::fuchsia::sysmem2::PixelFormat>()
          .set_type(
              sysmem::MakeTracking(&allocator, llcpp::fuchsia::sysmem2::PixelFormatType::R8G8B8A8))
          .set_format_modifier_value(sysmem::MakeTracking(
              &allocator, llcpp::fuchsia::sysmem2::FORMAT_MODIFIER_INTEL_I915_Y_TILED))
          .build(),
      // .required_buffer_usage_bits
      allocator.make_table_builder<llcpp::fuchsia::sysmem2::BufferUsage>()
          .set_none(sysmem::MakeTracking(&allocator, 0u))
          .set_cpu(sysmem::MakeTracking(&allocator, 0u))
          .set_vulkan(sysmem::MakeTracking(&allocator, 0u))
          .set_display(sysmem::MakeTracking(&allocator, 0u))
          .set_video(sysmem::MakeTracking(&allocator, 0u))
          .build(),
      // .cost
      1000.0L,
  });
  // LOG(INFO, "usage_pixel_format_cost.cc - allocator.debug_needed_buffer_size(): %zu",
  //    allocator.inner_allocator().debug_needed_buffer_size());
  return result;
}();

const PlatformCostsEntry kAmlogic_Generic_Costs = {
    .platform = kPlatform_Amlogic_Generic,
    .next_platform = kPlatform_Arm_Mali,
    .costs = kAmlogic_Generic_Cost_Entries,
};

// kAmlogic_S912_Cost_Entries will go here.

const PlatformCostsEntry kAmlogic_S912_Costs = {
    .platform = kPlatform_Amlogic_S912,
    .next_platform = kPlatform_Amlogic_Generic,
    .costs = std::list<const UsagePixelFormatCostEntry>(),
};

// kAmlogic_S905D2_Cost_Entries will go here.

const PlatformCostsEntry kAmlogic_S905D2_Costs = {
    .platform = kPlatform_Amlogic_S905D2,
    .next_platform = kPlatform_Amlogic_Generic,
    .costs = std::list<const UsagePixelFormatCostEntry>(),
};

// kAmlogic_T931_Cost_Entries will go here.

const PlatformCostsEntry kAmlogic_T931_Costs = {
    .platform = kPlatform_Amlogic_T931,
    .next_platform = kPlatform_Amlogic_Generic,
    .costs = std::list<const UsagePixelFormatCostEntry>(),
};

const PlatformCostsEntry kGeneric_Costs = {
    .platform = kPlatform_Generic,
    .next_platform = kPlatform_None,
    .costs = kGeneric_Cost_Entries,
};

const std::map<Platform, const PlatformCostsEntry*> kPlatformCosts = {
    {kPlatform_Generic, &kGeneric_Costs},
    {kPlatform_Arm_Mali, &kArm_Mali_Costs},
    {kPlatform_Amlogic_Generic, &kAmlogic_Generic_Costs},
    {kPlatform_Amlogic_S912, &kAmlogic_S912_Costs},
    {kPlatform_Amlogic_S905D2, &kAmlogic_S905D2_Costs},
    {kPlatform_Amlogic_T931, &kAmlogic_T931_Costs},
};

const double kDefaultCost = std::numeric_limits<double>::max();

Platform FindPlatform(uint32_t pdev_device_info_vid, uint32_t pdev_device_info_pid) {
  auto iter = kPlatformTranslation.find(MakeVidPidKey(pdev_device_info_vid, pdev_device_info_pid));
  if (iter == kPlatformTranslation.end()) {
    return kPlatform_Generic;
  }
  return iter->second;
}

const PlatformCostsEntry* FindPlatformCosts(Platform platform) {
  if (platform == kPlatform_None) {
    return nullptr;
  }
  auto iter = kPlatformCosts.find(platform);
  if (iter == kPlatformCosts.end()) {
    return nullptr;
  }
  return iter->second;
}

// |a| to check
// |r| required bits
bool HasAllRequiredBits(uint32_t a, uint32_t r) { return (r & a) == r; }

// |a| to check
// |r| required bits
bool HasAllRequiredUsageBits(const llcpp::fuchsia::sysmem2::BufferUsage& a,
                             const llcpp::fuchsia::sysmem2::BufferUsage& r) {
  const uint32_t a_cpu = a.has_cpu() ? a.cpu() : 0;
  const uint32_t a_vulkan = a.has_vulkan() ? a.vulkan() : 0;
  const uint32_t a_display = a.has_display() ? a.display() : 0;
  const uint32_t a_video = a.has_video() ? a.video() : 0;
  const uint32_t r_cpu = r.has_cpu() ? r.cpu() : 0;
  const uint32_t r_vulkan = r.has_vulkan() ? r.vulkan() : 0;
  const uint32_t r_display = r.has_display() ? r.display() : 0;
  const uint32_t r_video = r.has_video() ? r.video() : 0;
  return HasAllRequiredBits(a_cpu, r_cpu) && HasAllRequiredBits(a_vulkan, r_vulkan) &&
         HasAllRequiredBits(a_display, r_display) && HasAllRequiredBits(a_video, r_video);
}

uint32_t SharedBitsCount(uint32_t a, uint32_t b) {
  uint32_t set_in_both = a & b;

  // TODO(dustingreen): Consider using popcount intrinsic (or equivalent).
  uint32_t count = 0;
  for (uint32_t i = 0; i < sizeof(uint32_t) * 8; ++i) {
    if (set_in_both & (1 << i)) {
      ++count;
    }
  }

  return count;
}

uint32_t SharedUsageBitsCount(const llcpp::fuchsia::sysmem2::BufferUsage& a,
                              const llcpp::fuchsia::sysmem2::BufferUsage& b) {
  const uint32_t a_cpu = a.has_cpu() ? a.cpu() : 0;
  const uint32_t a_vulkan = a.has_vulkan() ? a.vulkan() : 0;
  const uint32_t a_display = a.has_display() ? a.display() : 0;
  const uint32_t a_video = a.has_video() ? a.video() : 0;
  const uint32_t b_cpu = b.has_cpu() ? b.cpu() : 0;
  const uint32_t b_vulkan = b.has_vulkan() ? b.vulkan() : 0;
  const uint32_t b_display = b.has_display() ? b.display() : 0;
  const uint32_t b_video = b.has_video() ? b.video() : 0;
  return SharedBitsCount(a_cpu, b_cpu) + SharedBitsCount(a_vulkan, b_vulkan) +
         SharedBitsCount(a_display, b_display) + SharedBitsCount(a_video, b_video);
}

// This comparison has nothing to do with the cost of a or cost of b.  This is
// only about finding the best-match UsagePixelFormatCostEntry for the given
// query.
//
// |constraints| the query's constraints
//
// |image_format_constraints_index| the query's image_format_constraints_index
//
// |a| the new UsagePixelFormatCostEntry to consider
//
// |b| the existing UsagePixelFormatCostEntry that a is being compared against
bool IsBetterMatch(const llcpp::fuchsia::sysmem2::BufferCollectionConstraints::Builder& constraints,
                   uint32_t image_format_constraints_index, const UsagePixelFormatCostEntry* a,
                   const UsagePixelFormatCostEntry* b) {
  ZX_DEBUG_ASSERT(a);
  ZX_DEBUG_ASSERT(image_format_constraints_index < constraints.image_format_constraints().count());
  // We intentionally allow b to be nullptr.

  if (!ImageFormatIsPixelFormatEqual(
          a->pixel_format,
          constraints.image_format_constraints()[image_format_constraints_index].pixel_format()))
    return false;

  llcpp::fuchsia::sysmem2::BufferUsage default_usage;
  const llcpp::fuchsia::sysmem2::BufferUsage* usage_ptr;
  if (constraints.has_usage()) {
    usage_ptr = &constraints.usage();
  } else {
    usage_ptr = &default_usage;
  }
  const llcpp::fuchsia::sysmem2::BufferUsage& usage = *usage_ptr;
  if (!HasAllRequiredUsageBits(usage, a->required_buffer_usage_bits)) {
    return false;
  }
  ZX_DEBUG_ASSERT(HasAllRequiredUsageBits(usage, a->required_buffer_usage_bits));
  // We intentionally allow b to be nullptr.
  if (b == nullptr) {
    return true;
  }
  ZX_DEBUG_ASSERT(HasAllRequiredUsageBits(usage, b->required_buffer_usage_bits));
  uint32_t a_shared_bits = SharedUsageBitsCount(usage, a->required_buffer_usage_bits);
  uint32_t b_shared_bits = SharedUsageBitsCount(usage, b->required_buffer_usage_bits);
  return a_shared_bits > b_shared_bits;
}

double GetCostInternal(
    const llcpp::fuchsia::sysmem2::BufferCollectionConstraints::Builder& constraints,
    uint32_t image_format_constraints_index, Platform platform) {
  const PlatformCostsEntry* platform_costs = FindPlatformCosts(platform);
  if (!platform_costs) {
    return kDefaultCost;
  }
  const UsagePixelFormatCostEntry* best_match = nullptr;
  while (platform_costs) {
    for (const UsagePixelFormatCostEntry& cost : platform_costs->costs) {
      if (IsBetterMatch(constraints, image_format_constraints_index, &cost, best_match)) {
        best_match = &cost;
      }
    }
    platform_costs = FindPlatformCosts(platform_costs->next_platform);
  }
  if (!best_match) {
    return kDefaultCost;
  }
  ZX_DEBUG_ASSERT(best_match);
  return best_match->cost;
}

double GetCost(uint32_t pdev_device_info_vid, uint32_t pdev_device_info_pid,
               const llcpp::fuchsia::sysmem2::BufferCollectionConstraints::Builder& constraints,
               uint32_t image_format_constraints_index) {
  Platform platform = FindPlatform(pdev_device_info_vid, pdev_device_info_pid);
  if (platform == kPlatform_None) {
    return kDefaultCost;
  }
  return GetCostInternal(constraints, image_format_constraints_index, platform);
}

}  // namespace

int32_t UsagePixelFormatCost::Compare(
    uint32_t pdev_device_info_vid, uint32_t pdev_device_info_pid,
    const llcpp::fuchsia::sysmem2::BufferCollectionConstraints::Builder& constraints,
    uint32_t image_format_constraints_index_a, uint32_t image_format_constraints_index_b) {
  double cost_a = GetCost(pdev_device_info_vid, pdev_device_info_pid, constraints,
                          image_format_constraints_index_a);
  double cost_b = GetCost(pdev_device_info_vid, pdev_device_info_pid, constraints,
                          image_format_constraints_index_b);

  if (cost_a < cost_b) {
    return -1;
  } else if (cost_a > cost_b) {
    return 1;
  } else {
    return 0;
  }
}

}  // namespace sysmem_driver
