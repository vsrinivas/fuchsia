// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MACHINA_ARCH_ARM64_GIC_DISTRIBUTOR_H_
#define GARNET_LIB_MACHINA_ARCH_ARM64_GIC_DISTRIBUTOR_H_

#include <limits.h>
#include <mutex>
#include <vector>

#include "garnet/lib/machina/io.h"
#include "garnet/lib/machina/platform_device.h"

namespace machina {

enum class GicVersion {
  V2 = 2,
  V3 = 3,
};

class Guest;
class Vcpu;

// Implements GIC redistributor.
class GicRedistributor : public IoHandler {
 public:
  GicRedistributor(uint16_t id, bool last) : id_(id), last_(last) {}

  zx_status_t Read(uint64_t addr, IoValue* value) const override;
  zx_status_t Write(uint64_t addr, const IoValue& value) override;

 private:
  uint16_t id_;
  bool last_;
};

// Implements GIC distributor.
class GicDistributor : public IoHandler, public PlatformDevice {
 public:
  zx_status_t Init(Guest* guest,
                   uint8_t num_cpus) __TA_NO_THREAD_SAFETY_ANALYSIS;

  zx_status_t Read(uint64_t addr, IoValue* value) const override;
  zx_status_t Write(uint64_t addr, const IoValue& value) override;

  zx_status_t ConfigureZbi(void* zbi_base, size_t zbi_max) const override;
  zx_status_t ConfigureDtb(void* dtb) const override;

  zx_status_t RegisterVcpu(uint8_t vcpu_num,
                           Vcpu* vcpu) __TA_NO_THREAD_SAFETY_ANALYSIS;

  zx_status_t Interrupt(uint32_t global_irq);

 private:
  // NOTE: This must match the same constant in arch/hypervisor.h within Zircon.
  static constexpr size_t kNumInterrupts = 256;
  static constexpr uint8_t kNumSgisAndPpis = 32;
  static constexpr uint8_t kMaxVcpus = 8;
  GicVersion gic_version_ = GicVersion::V2;

  mutable std::mutex mutex_;
  Vcpu* vcpus_[kMaxVcpus] __TA_GUARDED(mutex_) = {};
  bool affinity_routing_ __TA_GUARDED(mutex_) = false;
  std::vector<std::unique_ptr<GicRedistributor>> __TA_GUARDED(mutex_)
      redistributors_;

  // Tracks whether interrupts are enabled.
  //
  // NOTE(abdulla): This doesn't properly account for banked interrupts.
  uint8_t enabled_[kNumInterrupts / CHAR_BIT] __TA_GUARDED(mutex_) = {};

  // SPI routing without affinity routing uses these cpu masks.
  uint8_t cpu_masks_[kNumInterrupts] __TA_GUARDED(mutex_) = {};

  // SPI routing with affinity routing either sends the interrupt to all VCPUs
  // or routes to the VCPU specified (by its level 0 affinity) in cpu_routes_.
  bool broadcast_[kNumInterrupts - kNumSgisAndPpis] __TA_GUARDED(mutex_) = {};
  uint8_t cpu_routes_[kNumInterrupts - kNumSgisAndPpis] __TA_GUARDED(
      mutex_) = {};

  zx_status_t TargetInterrupt(uint32_t global_irq, uint8_t cpu_mask);
  zx_status_t RouteInterrupt(uint32_t global_irq);
};

}  // namespace machina

#endif  // GARNET_LIB_MACHINA_ARCH_ARM64_GIC_DISTRIBUTOR_H_
