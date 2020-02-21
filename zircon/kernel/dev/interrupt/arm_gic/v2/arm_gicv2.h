// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_DEV_INTERRUPT_ARM_GIC_V2_ARM_GICV2_H_
#define ZIRCON_KERNEL_DEV_INTERRUPT_ARM_GIC_V2_ARM_GICV2_H_

#include <fbl/function.h>
#include <kernel/cpu.h>

namespace arm_gicv2 {

// Maintains a map of logical cpu numbers, that the kernel uses internally, to
// GIC cpu masks that are used by the ARM GIC system to specify interrupt
// targets.
class CpuMaskTranslator {
 public:
  cpu_mask_t LogicalMaskToGic(const cpu_mask_t logical) {
    // Special case for only one cpu set since it is pretty common.
    if (OnlyOneCpu(logical)) {
      return GetGicMask(lowest_cpu_set(logical));
    }

    cpu_mask_t out = 0;
    for (int i = 0; i < 8; i++) {
      if ((logical >> i) & 1) {
        out |= GetGicMask(i);
      }
    }
    return out;
  }

  cpu_mask_t GetGicMask(const cpu_num_t logical_id) {
    return cpu_num_to_mask(logical_to_gic_[logical_id]);
  }

  void SetGicIdForLogicalId(const cpu_num_t logical_id, const cpu_num_t gic_id) {
    DEBUG_ASSERT(logical_id < kMapSize);
    DEBUG_ASSERT(gic_id == (gic_id & 0xFF));
    logical_to_gic_[logical_id] = static_cast<uint8_t>(gic_id);
  }

 private:
  bool OnlyOneCpu(const cpu_mask_t mask) {
    // In the general case mask & mask-1 would tell you if there is more than
    // one bit set, but we also account for the value being 0.
    return mask && !(mask & (mask - 1));
  }

  // GIC v2 only allows 8 cpus.
  static constexpr size_t kMapSize = 8;

  // Lookup gic_cpu_num by logical cpu number (not mask).
  uint8_t logical_to_gic_[kMapSize] = {0};
};

}  // namespace arm_gicv2

// Exposed for testing.
uint8_t gic_determine_local_mask(fbl::Function<uint32_t(int)> fetch_gicd_targetsr_reg);

#endif  // ZIRCON_KERNEL_DEV_INTERRUPT_ARM_GIC_V2_ARM_GICV2_H_
