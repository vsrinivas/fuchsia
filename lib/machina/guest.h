// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MACHINA_GUEST_H_
#define GARNET_LIB_MACHINA_GUEST_H_

#include <forward_list>
#include <shared_mutex>

#include <lib/async-loop/cpp/loop.h>
#include <zx/guest.h>
#include <zx/vmar.h>

#include "garnet/lib/machina/device/phys_mem.h"
#include "garnet/lib/machina/io.h"
#include "garnet/lib/machina/vcpu.h"

namespace machina {

enum class TrapType {
  MMIO_SYNC = 0,
  MMIO_BELL = 1,
  PIO_SYNC = 2,
};

class Guest {
 public:
  using IoMappingList = std::forward_list<IoMapping>;

  zx_status_t Init(size_t mem_size, bool host_memory);

  const PhysMem& phys_mem() const { return phys_mem_; }
  zx::guest* object() { return &guest_; }
  async_dispatcher_t* device_dispatcher() const {
    return device_loop_.dispatcher();
  }

  // Setup a trap to delegate accesses to an IO region to |handler|.
  zx_status_t CreateMapping(TrapType type, uint64_t addr, size_t size,
                            uint64_t offset, IoHandler* handler);

  // Initializes a VCPU by calling the VCPU factory. The first VCPU must have id
  // 0.
  zx_status_t StartVcpu(uint64_t id, zx_gpaddr_t entry, zx_gpaddr_t boot_ptr);

  // Signals an interrupt to the VCPUs indicated by |mask|.
  zx_status_t Interrupt(uint64_t mask, uint8_t vector);

  // Waits for all VCPUs associated with the guest to finish executing.
  zx_status_t Join();

  // Creates a vmar for a specific region of guest memory.
  zx_status_t CreateSubVmar(uint64_t addr, size_t size, zx::vmar* vmar);

  IoMappingList::const_iterator mappings_begin() const {
    return mappings_.begin();
  }
  IoMappingList::const_iterator mappings_end() const { return mappings_.end(); }

 private:
  // TODO(alexlegg): Consolidate this constant with other definitions in Garnet.
  static constexpr size_t kMaxVcpus = 16u;

  zx::guest guest_;
  zx::vmar vmar_;
  PhysMem phys_mem_;
  IoMappingList mappings_;

  std::shared_mutex mutex_;
  std::unique_ptr<Vcpu> vcpus_[kMaxVcpus] = {};

  async::Loop device_loop_{&kAsyncLoopConfigNoAttachToThread};
};

}  // namespace machina

#endif  // GARNET_LIB_MACHINA_GUEST_H_
