// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_BIN_VMM_GUEST_H_
#define SRC_VIRTUALIZATION_BIN_VMM_GUEST_H_

#include <fuchsia/virtualization/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
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

#ifdef __aarch64__
  // hypervisor::IdAllocator<uint16_t, 8>
  static constexpr size_t kMaxVcpus = 8u;
#else
  // hypervisor::IdAllocator<uint16_t, 64>
  static constexpr size_t kMaxVcpus = 64u;
#endif
  using VcpuArray = std::array<std::unique_ptr<Vcpu>, kMaxVcpus>;
  using IoMappingList = std::forward_list<IoMapping>;

  zx_status_t Init(const std::vector<fuchsia::virtualization::MemorySpec>& memory);

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
  zx_status_t Interrupt(uint64_t mask, uint8_t vector);

  // Waits for all VCPUs associated with the guest to finish executing.
  zx_status_t Join();

  const IoMappingList& mappings() const { return mappings_; }
  const VcpuArray& vcpus() const { return vcpus_; }

 private:
  zx::guest guest_;
  zx::vmar vmar_;
  PhysMem phys_mem_;
  IoMappingList mappings_;

  std::shared_mutex mutex_;
  VcpuArray vcpus_;
};

#endif  // SRC_VIRTUALIZATION_BIN_VMM_GUEST_H_
