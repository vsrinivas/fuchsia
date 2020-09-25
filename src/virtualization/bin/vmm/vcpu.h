// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_BIN_VMM_VCPU_H_
#define SRC_VIRTUALIZATION_BIN_VMM_VCPU_H_

#include <lib/zx/vcpu.h>
#include <zircon/syscalls/port.h>

#include <future>
#include <shared_mutex>

typedef struct zx_vcpu_state zx_vcpu_state_t;

class Guest;

class Vcpu {
 public:
  Vcpu(uint64_t id, Guest* guest, zx_gpaddr_t entry, zx_gpaddr_t boot_ptr);

  // Begins VCPU execution.
  zx_status_t Start();

  // Waits for the VCPU to transition to a terminal state.
  zx_status_t Join();

  // Send virtual interrupt to the VCPU.
  zx_status_t Interrupt(uint32_t vector);

  uint64_t id() const { return id_; }
  const zx::vcpu& object() { return vcpu_; }

  static Vcpu* GetCurrent();

 private:
  // Resume the VCPU and handle packets in a loop.
  zx_status_t Loop(std::promise<zx_status_t> barrier);

  // Guest packet handlers
  zx_status_t HandlePacketLocked(const zx_port_packet_t& packet);
  zx_status_t HandleMemLocked(const zx_packet_guest_mem_t& mem, uint64_t trap_key);
#if __x86_64__
  zx_status_t HandleInput(const zx_packet_guest_io_t& io, uint64_t trap_key);
  zx_status_t HandleOutput(const zx_packet_guest_io_t& io, uint64_t trap_key);
  zx_status_t HandleIo(const zx_packet_guest_io_t& io, uint64_t trap_key);
#endif
  zx_status_t HandleVcpu(const zx_packet_guest_vcpu_t& packet, uint64_t trap_key);

  const uint64_t id_;
  Guest* guest_;
  const zx_gpaddr_t entry_;
  const zx_gpaddr_t boot_ptr_;

  std::future<zx_status_t> future_;
  zx::vcpu vcpu_;
};

#endif  // SRC_VIRTUALIZATION_BIN_VMM_VCPU_H_
