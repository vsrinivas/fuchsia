// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/vmm/vcpu.h"

#include <lib/fit/defer.h>
#include <lib/fit/function.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>
#include <lib/zx/thread.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <zircon/process.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/hypervisor.h>
#include <zircon/syscalls/port.h>

#include "src/lib/fxl/strings/string_printf.h"
#include "src/virtualization/bin/vmm/guest.h"
#include "src/virtualization/bin/vmm/io.h"

static thread_local Vcpu* thread_vcpu = nullptr;

Vcpu::Vcpu(uint64_t id, Guest* guest, zx_gpaddr_t entry, zx_gpaddr_t boot_ptr, async::Loop* loop)
    : id_(id), guest_(guest), entry_(entry), boot_ptr_(boot_ptr), loop_(loop) {}

zx_status_t Vcpu::Start() {
  std::promise<zx_status_t> barrier;
  std::future<zx_status_t> barrier_future = barrier.get_future();
  std::thread(fit::bind_member(this, &Vcpu::Loop), std::move(barrier)).detach();
  barrier_future.wait();
  return barrier_future.get();
}

Vcpu* Vcpu::GetCurrent() {
  FX_DCHECK(thread_vcpu != nullptr) << "Thread does not have a VCPU";
  return thread_vcpu;
}

zx_status_t Vcpu::Loop(std::promise<zx_status_t> barrier) {
  FX_DCHECK(thread_vcpu == nullptr) << "Thread has multiple VCPUs";

  // Set the thread state.
  {
    thread_vcpu = this;
    auto name = fxl::StringPrintf("vcpu-%lu", id_);
    zx_status_t status = zx::thread::self()->set_property(ZX_PROP_NAME, name.c_str(), name.size());
    if (status != ZX_OK) {
      FX_PLOGS(WARNING, status) << "Failed to set VCPU " << id_ << " thread name";
    }
  }

  // Create the VCPU.
  {
    zx_status_t status = zx::vcpu::create(guest_->object(), 0, entry_, &vcpu_);
    if (status != ZX_OK) {
      FX_PLOGS(ERROR, status) << "Failed to create VCPU " << id_;
      barrier.set_value(status);
      return status;
    }
  }

  // Set the initial VCPU state.
  {
    zx_vcpu_state_t vcpu_state = {};
#if __aarch64__
    vcpu_state.x[0] = boot_ptr_;
#elif __x86_64__
    vcpu_state.rsi = boot_ptr_;
#else
#error Unknown architecture.
#endif

    zx_status_t status = vcpu_.write_state(ZX_VCPU_STATE, &vcpu_state, sizeof(vcpu_state));
    if (status != ZX_OK) {
      FX_PLOGS(ERROR, status) << "Failed to set VCPU " << id_ << " state";
      barrier.set_value(status);
      return status;
    }
  }

  // Unblock VCPU startup barrier.
  barrier.set_value(ZX_OK);

  // Quit the main loop if we return.
  auto deferred = fit::defer([this] { loop_->Quit(); });

  while (true) {
    zx_port_packet_t packet;
    zx_status_t status = vcpu_.resume(&packet);
    switch (status) {
      case ZX_OK:
        break;
      default:
        FX_LOGS(ERROR) << "Fatal error attempting to resume VCPU " << id_ << ": "
                       << zx_status_get_string(status) << ". Shutting down VM.";
        return status;
    }

    status = HandlePacket(packet);
    switch (status) {
      case ZX_OK:
        break;
      case ZX_ERR_CANCELED:
        // Gracefully shut down the entire VM.
        FX_LOGS(INFO) << "Guest requested shutdown";
        return ZX_OK;
      default:
        FX_LOGS(ERROR) << "Fatal error handling packet of type " << packet.type << ": "
                       << zx_status_get_string(status) << ". Shutting down VM.";
        return status;
    }
  }
}

zx_status_t Vcpu::Interrupt(uint32_t vector) { return vcpu_.interrupt(vector); }

zx_status_t Vcpu::HandleMem(const zx_packet_guest_mem_t& packet, uint64_t trap_key) {
  IoMapping* device_mapping = IoMapping::FromPortKey(trap_key);
  zx_status_t status = ArchHandleMem(packet, device_mapping);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << std::hex << "Device '" << device_mapping->handler()->Name()
                   << "' returned status " << zx_status_get_string(status)
                   << " while attempting to handle MMIO access at paddr 0x" << packet.addr
                   << " (mapping offset 0x" << (packet.addr - device_mapping->base()) << ").";
    return status;
  }

  return ZX_OK;
}

zx_status_t Vcpu::HandlePacket(const zx_port_packet_t& packet) {
  switch (packet.type) {
    case ZX_PKT_TYPE_GUEST_MEM:
      return HandleMem(packet.guest_mem, packet.key);
#if __x86_64__
    case ZX_PKT_TYPE_GUEST_IO:
      return ArchHandleIo(packet.guest_io, packet.key);
#endif  // __x86_64__
    case ZX_PKT_TYPE_GUEST_VCPU:
      return HandleVcpu(packet.guest_vcpu, packet.key);
    default:
      FX_LOGS(ERROR) << "Unhandled guest packet " << packet.type;
      return ZX_ERR_NOT_SUPPORTED;
  }
}

zx_status_t Vcpu::HandleVcpu(const zx_packet_guest_vcpu_t& packet, uint64_t trap_key) {
  switch (packet.type) {
    case ZX_PKT_GUEST_VCPU_INTERRUPT:
      return guest_->Interrupt(packet.interrupt.mask, packet.interrupt.vector);
    case ZX_PKT_GUEST_VCPU_STARTUP:
      return guest_->StartVcpu(packet.startup.id, packet.startup.entry, boot_ptr_, loop_);
    default:
      return ZX_ERR_NOT_SUPPORTED;
  }
}
