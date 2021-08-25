// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_BIN_VMM_VCPU_H_
#define SRC_VIRTUALIZATION_BIN_VMM_VCPU_H_

#include <lib/async-loop/cpp/loop.h>
#include <lib/zx/vcpu.h>
#include <zircon/syscalls/port.h>

#include <future>

using zx_vcpu_state_t = struct zx_vcpu_state;

class Guest;
class IoMapping;

class Vcpu {
 public:
  Vcpu(const Vcpu&) = delete;
  Vcpu& operator=(const Vcpu&) = delete;
  Vcpu(uint64_t id, Guest* guest, zx_gpaddr_t entry, zx_gpaddr_t boot_ptr, async::Loop* loop);

  // Begins VCPU execution.
  zx_status_t Start();

  // Send virtual interrupt to the VCPU.
  zx_status_t Interrupt(uint32_t vector);

  uint64_t id() const { return id_; }
  const zx::vcpu& object() const { return vcpu_; }

  static Vcpu* GetCurrent();

 private:
  // Resume the VCPU and handle packets in a loop.
  zx_status_t Loop(std::promise<zx_status_t> barrier);

  // Guest packet handlers
  zx_status_t HandlePacket(const zx_port_packet_t& packet);
  zx_status_t HandleVcpu(const zx_packet_guest_vcpu_t& packet, uint64_t trap_key);
  zx_status_t HandleMem(const zx_packet_guest_mem_t& packet, uint64_t trap_key);

  // Architecture-specific handlers, implemented in the
  // src/virtualization/bin/vmm/arch/${ARCH}/vcpu.cc
  zx_status_t ArchHandleMem(const zx_packet_guest_mem_t& mem, IoMapping* device_mapping);
#if __x86_64__
  zx_status_t ArchHandleInput(const zx_packet_guest_io_t& io, IoMapping* device_mapping);
  zx_status_t ArchHandleOutput(const zx_packet_guest_io_t& io, IoMapping* device_mapping);
  zx_status_t ArchHandleIo(const zx_packet_guest_io_t& io, uint64_t trap_key);
#endif

  const uint64_t id_;
  Guest* const guest_;
  const zx_gpaddr_t entry_;
  const zx_gpaddr_t boot_ptr_;
  async::Loop* const loop_;

  zx::vcpu vcpu_;
};

#endif  // SRC_VIRTUALIZATION_BIN_VMM_VCPU_H_
