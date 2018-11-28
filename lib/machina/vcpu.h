// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MACHINA_VCPU_H_
#define GARNET_LIB_MACHINA_VCPU_H_

#include <future>
#include <shared_mutex>

#include <zircon/syscalls/port.h>
#include <zx/vcpu.h>

typedef struct zx_vcpu_state zx_vcpu_state_t;

namespace machina {

class Guest;

class Vcpu {
 public:
  Vcpu(uint64_t id, Guest* guest, zx_gpaddr_t entry, zx_gpaddr_t boot_ptr);

  // Begins VCPU execution.
  void Start();

  // Waits for the VCPU to transition to a terminal state.
  zx_status_t Join();

  // Send virtual interrupt to the VCPU.
  zx_status_t Interrupt(uint32_t vector);

  uint64_t id() const { return id_; }

  static Vcpu* GetCurrent();

 private:
  // Resume the VCPU and handle packets in a loop.
  zx_status_t Loop();

  // Guest packet handlers
  zx_status_t HandlePacketLocked(const zx_port_packet_t& packet);
  zx_status_t HandleMemLocked(const zx_packet_guest_mem_t& mem,
                              uint64_t trap_key);
#if __x86_64__
  zx_status_t HandleInput(const zx_packet_guest_io_t& io, uint64_t trap_key);
  zx_status_t HandleOutput(const zx_packet_guest_io_t& io, uint64_t trap_key);
  zx_status_t HandleIo(const zx_packet_guest_io_t& io, uint64_t trap_key);
#endif
  zx_status_t HandleVcpu(const zx_packet_guest_vcpu_t& packet,
                         uint64_t trap_key);

  const uint64_t id_;
  Guest* guest_;
  const zx_gpaddr_t entry_;
  const zx_gpaddr_t boot_ptr_;

  std::future<zx_status_t> future_;
  std::shared_mutex mutex_;
  zx::vcpu vcpu_;
};

}  // namespace machina

#endif  // GARNET_LIB_MACHINA_VCPU_H_
