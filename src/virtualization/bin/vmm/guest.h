// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_BIN_VMM_GUEST_H_
#define SRC_VIRTUALIZATION_BIN_VMM_GUEST_H_

#include <fuchsia/virtualization/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fit/result.h>
#include <lib/zx/guest.h>
#include <lib/zx/vmar.h>

#include <array>
#include <forward_list>
#include <shared_mutex>

#include "src/virtualization/bin/vmm/device/phys_mem.h"
#include "src/virtualization/bin/vmm/io.h"
#include "src/virtualization/bin/vmm/vcpu.h"

enum class TrapType {
  MMIO_SYNC = 0,
  MMIO_BELL = 1,
  PIO_SYNC = 2,
};

class Guest {
 public:
#if __aarch64__
  // hypervisor::IdAllocator<uint16_t, 8>
  static constexpr size_t kMaxVcpus = 8u;
#elif __x86_64__
  // hypervisor::IdAllocator<uint16_t, 64>
  static constexpr size_t kMaxVcpus = 64u;
#else
#error Unknown architecture.
#endif
  using VcpuArray = std::array<std::optional<Vcpu>, kMaxVcpus>;
  using IoMappingList = std::forward_list<IoMapping>;

  zx_status_t Init(uint64_t guest_memory);

  const PhysMem& phys_mem() const { return phys_mem_; }
  const zx::guest& object() { return guest_; }

  // Setup a trap to delegate accesses to an IO region to |handler|.
  zx_status_t CreateMapping(TrapType type, uint64_t addr, size_t size, uint64_t offset,
                            IoHandler* handler, async_dispatcher_t* dispatcher = nullptr);

  // Creates a VMAR for a specific region of guest memory.
  zx_status_t CreateSubVmar(uint64_t addr, size_t size, zx::vmar* vmar);

  // Starts a VCPU. The first VCPU must have an |id| of 0.
  zx_status_t StartVcpu(uint64_t id, zx_gpaddr_t entry, zx_gpaddr_t boot_ptr);

  // Signals an interrupt to the VCPUs indicated by |mask|.
  zx_status_t Interrupt(uint64_t mask, uint32_t vector);

  // Sets a stop callback that can be triggered by a VCPU to stop VMM execution.
  void set_stop_callback(
      fit::function<void(fit::result<::fuchsia::virtualization::GuestError>)> stop_callback);

  // Stop the VMM with the given error code.
  void Stop(fit::result<::fuchsia::virtualization::GuestError> result);

  // Returns zx_system_get_page_size aligned guest memory.
  static uint64_t GetPageAlignedGuestMemory(uint64_t guest_memory);

  // Attempts to page align the start and end of a guest memory region. Returns false if the
  // resulting region is zero (or less) pages long.
  static bool PageAlignGuestMemoryRegion(GuestMemoryRegion& region);

  // Returns a non-overlapping list of restricted memory regions, ordered by base address.
  static cpp20::span<const GuestMemoryRegion> GetDefaultRestrictionsForArchitecture();

  // Generates guest memory regions with total size |guest_memory|, avoiding any device memory.
  static bool GenerateGuestMemoryRegions(uint64_t guest_memory,
                                         cpp20::span<const GuestMemoryRegion> restrictions,
                                         std::vector<GuestMemoryRegion>* regions);

  const IoMappingList& mappings() const { return mappings_; }
  const VcpuArray& vcpus() const { return vcpus_; }
  const std::vector<GuestMemoryRegion>& memory_regions() const { return memory_regions_; }

 private:
  zx::guest guest_;
  zx::vmar vmar_;
  PhysMem phys_mem_;
  IoMappingList mappings_;
  std::vector<GuestMemoryRegion> memory_regions_;

  std::shared_mutex mutex_;
  VcpuArray vcpus_;
  fit::function<void(fit::result<::fuchsia::virtualization::GuestError>)> stop_callback_;
};

#endif  // SRC_VIRTUALIZATION_BIN_VMM_GUEST_H_
