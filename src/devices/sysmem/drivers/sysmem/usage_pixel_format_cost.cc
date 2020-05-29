// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "usage_pixel_format_cost.h"

#include <fuchsia/sysmem/c/fidl.h>
#include <lib/image-format/image_format.h>
#include <zircon/assert.h>

#include <list>
#include <map>

#include <ddk/platform-defs.h>

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
  const fuchsia_sysmem_PixelFormat pixel_format;
  // A query's usage bits must contain all these usage bits for this entry to
  // be considered.
  const fuchsia_sysmem_BufferUsage required_buffer_usage_bits;
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

  const std::list<const UsagePixelFormatCostEntry> costs;
};

const std::list<const UsagePixelFormatCostEntry> kArm_Mali_Cost_Entries = {
    // AFBC TE is best.
    {
        // .pixel_format
        {
            // .type
            fuchsia_sysmem_PixelFormatType_BGRA32,
            // .has_format_modifier
            true,
            // .format_modifier.value
            {fuchsia_sysmem_FORMAT_MODIFIER_ARM_AFBC_16X16_TE},
        },
        // .required_buffer_usage_bits
        {
            // .none
            0,
            // .cpu
            0,
            // .vulkan
            0,
            // .display
            0,
            // .video
            0,
        },
        // .cost
        500.0L,
    },
    {
        // .pixel_format
        {
            // .type
            fuchsia_sysmem_PixelFormatType_R8G8B8A8,
            // .has_format_modifier
            true,
            // .format_modifier.value
            {fuchsia_sysmem_FORMAT_MODIFIER_ARM_AFBC_16X16_TE},
        },
        // .required_buffer_usage_bits
        {
            // .none
            0,
            // .cpu
            0,
            // .vulkan
            0,
            // .display
            0,
            // .video
            0,
        },
        // .cost
        500.0L,
    },
    {
        // .pixel_format
        {
            // .type
            fuchsia_sysmem_PixelFormatType_BGRA32,
            // .has_format_modifier
            true,
            // .format_modifier.value
            {fuchsia_sysmem_FORMAT_MODIFIER_ARM_AFBC_32X8_TE},
        },
        // .required_buffer_usage_bits
        {
            // .none
            0,
            // .cpu
            0,
            // .vulkan
            0,
            // .display
            0,
            // .video
            0,
        },
        // .cost
        500.0L,
    },
    {
        // .pixel_format
        {
            // .type
            fuchsia_sysmem_PixelFormatType_R8G8B8A8,
            // .has_format_modifier
            true,
            // .format_modifier.value
            {fuchsia_sysmem_FORMAT_MODIFIER_ARM_AFBC_32X8_TE},
        },
        // .required_buffer_usage_bits
        {
            // .none
            0,
            // .cpu
            0,
            // .vulkan
            0,
            // .display
            0,
            // .video
            0,
        },
        // .cost
        500.0L,
    },
    // AFBC always preferred when supported.
    {
        // .pixel_format
        {
            // .type
            fuchsia_sysmem_PixelFormatType_BGRA32,
            // .has_format_modifier
            true,
            // .format_modifier.value
            {fuchsia_sysmem_FORMAT_MODIFIER_ARM_AFBC_16X16},
        },
        // .required_buffer_usage_bits
        {
            // .none
            0,
            // .cpu
            0,
            // .vulkan
            0,
            // .display
            0,
            // .video
            0,
        },
        // .cost
        1000.0L,
    },
    {
        // .pixel_format
        {
            // .type
            fuchsia_sysmem_PixelFormatType_R8G8B8A8,
            // .has_format_modifier
            true,
            // .format_modifier.value
            {fuchsia_sysmem_FORMAT_MODIFIER_ARM_AFBC_16X16},
        },
        // .required_buffer_usage_bits
        {
            // .none
            0,
            // .cpu
            0,
            // .vulkan
            0,
            // .display
            0,
            // .video
            0,
        },
        // .cost
        1000.0L,
    },
    {
        // .pixel_format
        {
            // .type
            fuchsia_sysmem_PixelFormatType_BGRA32,
            // .has_format_modifier
            true,
            // .format_modifier.value
            {fuchsia_sysmem_FORMAT_MODIFIER_ARM_AFBC_32X8},
        },
        // .required_buffer_usage_bits
        {
            // .none
            0,
            // .cpu
            0,
            // .vulkan
            0,
            // .display
            0,
            // .video
            0,
        },
        // .cost
        1000.0L,
    },
    {
        // .pixel_format
        {
            // .type
            fuchsia_sysmem_PixelFormatType_R8G8B8A8,
            // .has_format_modifier
            true,
            // .format_modifier.value
            {fuchsia_sysmem_FORMAT_MODIFIER_ARM_AFBC_32X8},
        },
        // .required_buffer_usage_bits
        {
            // .none
            0,
            // .cpu
            0,
            // .vulkan
            0,
            // .display
            0,
            // .video
            0,
        },
        // .cost
        1000.0L,
    },
    // Linear TE is better than linear.
    {
        // .pixel_format
        {
            // .type
            fuchsia_sysmem_PixelFormatType_BGRA32,
            // .has_format_modifier
            true,
            // .format_modifier.value
            {fuchsia_sysmem_FORMAT_MODIFIER_ARM_LINEAR_TE},
        },
        // .required_buffer_usage_bits
        {
            // .none
            0,
            // .cpu
            0,
            // .vulkan
            0,
            // .display
            0,
            // .video
            0,
        },
        // .cost
        1500.0L,
    },
    {
        // .pixel_format
        {
            // .type
            fuchsia_sysmem_PixelFormatType_R8G8B8A8,
            // .has_format_modifier
            true,
            // .format_modifier.value
            {fuchsia_sysmem_FORMAT_MODIFIER_ARM_LINEAR_TE},
        },
        // .required_buffer_usage_bits
        {
            // .none
            0,
            // .cpu
            0,
            // .vulkan
            0,
            // .display
            0,
            // .video
            0,
        },
        // .cost
        1500.0L,
    },
    {
        // .pixel_format
        {
            // .type
            fuchsia_sysmem_PixelFormatType_BGRA32,
            // .has_format_modifier
            true,
            // .format_modifier.value
            {fuchsia_sysmem_FORMAT_MODIFIER_ARM_LINEAR_TE},
        },
        // .required_buffer_usage_bits
        {
            // .none
            0,
            // .cpu
            0,
            // .vulkan
            0,
            // .display
            0,
            // .video
            0,
        },
        // .cost
        1500.0L,
    },
    {
        // .pixel_format
        {
            // .type
            fuchsia_sysmem_PixelFormatType_R8G8B8A8,
            // .has_format_modifier
            true,
            // .format_modifier.value
            {fuchsia_sysmem_FORMAT_MODIFIER_ARM_LINEAR_TE},
        },
        // .required_buffer_usage_bits
        {
            // .none
            0,
            // .cpu
            0,
            // .vulkan
            0,
            // .display
            0,
            // .video
            0,
        },
        // .cost
        1500.0L,
    },
};

const PlatformCostsEntry kArm_Mali_Costs = {
    .platform = kPlatform_Arm_Mali,
    .next_platform = kPlatform_Generic,
    .costs = kArm_Mali_Cost_Entries,
};

const std::list<const UsagePixelFormatCostEntry> kAmlogic_Generic_Cost_Entries = {
    // NV12 weakly preferred for videoUsageHwDecoder.
    {
        // .pixel_format
        {
            // .type
            fuchsia_sysmem_PixelFormatType_NV12,
            // .has_format_modifier
            false,
            // .format_modifier.value
            {fuchsia_sysmem_FORMAT_MODIFIER_NONE},
        },
        // .required_buffer_usage_bits
        {
            // .none
            0,
            // .cpu
            0,
            // .vulkan
            0,
            // .display
            0,
            // .video
            fuchsia_sysmem_videoUsageHwDecoder,
        },
        // .cost
        100.0L,
    },
};

// These costs are expected to be true on every platform.
const std::list<const UsagePixelFormatCostEntry> kGeneric_Cost_Entries = {
    {
        // .pixel_format
        {
            // .type
            fuchsia_sysmem_PixelFormatType_BGRA32,
            // .has_format_modifier
            true,
            // .format_modifier.value
            {fuchsia_sysmem_FORMAT_MODIFIER_INTEL_I915_X_TILED},
        },
        // .required_buffer_usage_bits
        {
            // .none
            0,
            // .cpu
            0,
            // .vulkan
            0,
            // .display
            0,
            // .video
            0,
        },
        // .cost
        1000.0L,
    },
    {
        // .pixel_format
        {
            // .type
            fuchsia_sysmem_PixelFormatType_BGRA32,
            // .has_format_modifier
            true,
            // .format_modifier.value
            {fuchsia_sysmem_FORMAT_MODIFIER_INTEL_I915_YF_TILED},
        },
        // .required_buffer_usage_bits
        {
            // .none
            0,
            // .cpu
            0,
            // .vulkan
            0,
            // .display
            0,
            // .video
            0,
        },
        // .cost
        1000.0L,
    },
    {
        // .pixel_format
        {
            // .type
            fuchsia_sysmem_PixelFormatType_BGRA32,
            // .has_format_modifier
            true,
            // .format_modifier.value
            {fuchsia_sysmem_FORMAT_MODIFIER_INTEL_I915_Y_TILED},
        },
        // .required_buffer_usage_bits
        {
            // .none
            0,
            // .cpu
            0,
            // .vulkan
            0,
            // .display
            0,
            // .video
            0,
        },
        // .cost
        1000.0L,
    },
    {
        // .pixel_format
        {
            // .type
            fuchsia_sysmem_PixelFormatType_R8G8B8A8,
            // .has_format_modifier
            true,
            // .format_modifier.value
            {fuchsia_sysmem_FORMAT_MODIFIER_INTEL_I915_X_TILED},
        },
        // .required_buffer_usage_bits
        {
            // .none
            0,
            // .cpu
            0,
            // .vulkan
            0,
            // .display
            0,
            // .video
            0,
        },
        // .cost
        1000.0L,
    },
    {
        // .pixel_format
        {
            // .type
            fuchsia_sysmem_PixelFormatType_R8G8B8A8,
            // .has_format_modifier
            true,
            // .format_modifier.value
            {fuchsia_sysmem_FORMAT_MODIFIER_INTEL_I915_YF_TILED},
        },
        // .required_buffer_usage_bits
        {
            // .none
            0,
            // .cpu
            0,
            // .vulkan
            0,
            // .display
            0,
            // .video
            0,
        },
        // .cost
        1000.0L,
    },
    {
        // .pixel_format
        {
            // .type
            fuchsia_sysmem_PixelFormatType_R8G8B8A8,
            // .has_format_modifier
            true,
            // .format_modifier.value
            {fuchsia_sysmem_FORMAT_MODIFIER_INTEL_I915_Y_TILED},
        },
        // .required_buffer_usage_bits
        {
            // .none
            0,
            // .cpu
            0,
            // .vulkan
            0,
            // .display
            0,
            // .video
            0,
        },
        // .cost
        1000.0L,
    },
};

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
bool HasAllRequiredUsageBits(const fuchsia_sysmem_BufferUsage& a,
                             const fuchsia_sysmem_BufferUsage& r) {
  return HasAllRequiredBits(a.cpu, r.cpu) && HasAllRequiredBits(a.vulkan, r.vulkan) &&
         HasAllRequiredBits(a.display, r.display) && HasAllRequiredBits(a.video, r.video);
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

uint32_t SharedUsageBitsCount(const fuchsia_sysmem_BufferUsage& a,
                              const fuchsia_sysmem_BufferUsage& b) {
  return SharedBitsCount(a.cpu, b.cpu) + SharedBitsCount(a.vulkan, b.vulkan) +
         SharedBitsCount(a.display, b.display) + SharedBitsCount(a.video, b.video);
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
bool IsBetterMatch(const fuchsia_sysmem_BufferCollectionConstraints* constraints,
                   uint32_t image_format_constraints_index, const UsagePixelFormatCostEntry* a,
                   const UsagePixelFormatCostEntry* b) {
  ZX_DEBUG_ASSERT(constraints);
  ZX_DEBUG_ASSERT(a);
  ZX_DEBUG_ASSERT(image_format_constraints_index < constraints->image_format_constraints_count);
  // We intentionally allow b to be nullptr.

  if (!ImageFormatIsPixelFormatEqual(
          a->pixel_format,
          constraints->image_format_constraints[image_format_constraints_index].pixel_format))
    return false;

  const fuchsia_sysmem_BufferUsage& usage = constraints->usage;
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

double GetCostInternal(const fuchsia_sysmem_BufferCollectionConstraints* constraints,
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
               const fuchsia_sysmem_BufferCollectionConstraints* constraints,
               uint32_t image_format_constraints_index) {
  Platform platform = FindPlatform(pdev_device_info_vid, pdev_device_info_pid);
  if (platform == kPlatform_None) {
    return kDefaultCost;
  }
  return GetCostInternal(constraints, image_format_constraints_index, platform);
}

}  // namespace

int32_t UsagePixelFormatCost::Compare(uint32_t pdev_device_info_vid, uint32_t pdev_device_info_pid,
                                      const fuchsia_sysmem_BufferCollectionConstraints* constraints,
                                      uint32_t image_format_constraints_index_a,
                                      uint32_t image_format_constraints_index_b) {
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
