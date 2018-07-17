// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MACHINA_GUEST_H_
#define GARNET_LIB_MACHINA_GUEST_H_

#include <fbl/intrusive_single_list.h>
#include <fbl/unique_ptr.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fit/function.h>
#include <zircon/types.h>

#include "garnet/lib/machina/phys_mem.h"
#include "garnet/lib/machina/vcpu.h"

namespace machina {

enum class TrapType {
  MMIO_SYNC = 0,
  MMIO_BELL = 1,
  PIO_SYNC = 2,
};

class IoHandler;
class IoMapping;

class Guest {
 public:
  using VcpuFactory = fit::function<zx_status_t(Guest* guest, uintptr_t entry,
                                                uint64_t id, Vcpu* vcpu)>;
  Guest();
  ~Guest();

  zx_status_t Init(size_t mem_size);

  const PhysMem& phys_mem() const { return phys_mem_; }
  zx_handle_t handle() const { return guest_; }
  async_dispatcher_t* device_dispatcher() const { return device_loop_.dispatcher(); }

  // Setup a trap to delegate accesses to an IO region to |handler|.
  zx_status_t CreateMapping(TrapType type, uint64_t addr, size_t size,
                            uint64_t offset, IoHandler* handler);

  // Setup a handler function to run when an additional VCPU is brought up. The
  // factory should call Start on the new VCPU to begin executing the guest on a
  // new thread.
  void RegisterVcpuFactory(VcpuFactory factory);

  // Initializes a VCPU by calling the VCPU factory. The first VCPU must have id
  // 0.
  zx_status_t StartVcpu(uintptr_t entry, uint64_t id);

  // Signals an interrupt to the VCPUs indicated by |mask|.
  zx_status_t SignalInterrupt(uint32_t mask, uint8_t vector);

  // Waits for all VCPUs associated with the guest to finish executing.
  zx_status_t Join();

 private:
  // TODO(alexlegg): Consolidate this constant with other definitions in Garnet.
  static constexpr size_t kMaxVcpus = 16u;

  fbl::Mutex mutex_;

  zx_handle_t guest_ = ZX_HANDLE_INVALID;
  PhysMem phys_mem_;

  fbl::SinglyLinkedList<fbl::unique_ptr<IoMapping>> mappings_;

  VcpuFactory vcpu_factory_ = [](Guest* guest, uintptr_t entry, uint64_t id,
                                 Vcpu* vcpu) { return ZX_ERR_BAD_STATE; };
  fbl::unique_ptr<Vcpu> vcpus_[kMaxVcpus] = {};

  async::Loop device_loop_;
};

}  // namespace machina

#endif  // GARNET_LIB_MACHINA_GUEST_H_
