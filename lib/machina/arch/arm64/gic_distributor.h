// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MACHINA_ARCH_ARM64_GIC_DISTRIBUTOR_H_
#define GARNET_LIB_MACHINA_ARCH_ARM64_GIC_DISTRIBUTOR_H_

#include <fbl/mutex.h>
#include <hypervisor/io.h>

class Guest;
class Vcpu;
struct SoftwareGeneratedInterrupt;

namespace machina {

// Implements GIC distributor.
class GicDistributor : public IoHandler {
 public:
  zx_status_t Init(Guest* guest);

  zx_status_t Read(uint64_t addr, IoValue* value) const override;
  zx_status_t Write(uint64_t addr, const IoValue& value) override;

  zx_status_t RegisterVcpu(uint8_t vcpu_id, Vcpu* vcpu);
  zx_status_t Interrupt(uint32_t global_irq);

 private:
  // NOTE: This must match the same constant in arch/hypervisor.h within Zircon.
  static constexpr size_t kNumInterrupts = 256;
  static constexpr size_t kMaxVcpus = 8;

  mutable fbl::Mutex mutex_;
  uint8_t cpu_masks_[kNumInterrupts] __TA_GUARDED(mutex_) = {};
  Vcpu* vcpus_[kMaxVcpus] = {};

  zx_status_t TargetInterrupt(uint32_t global_irq, uint8_t cpu_mask);
};

}  // namespace machina

#endif  // GARNET_LIB_MACHINA_ARCH_ARM64_GIC_DISTRIBUTOR_H_
