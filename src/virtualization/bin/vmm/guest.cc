// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/vmm/guest.h"

#include <fcntl.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>
#include <zircon/syscalls/hypervisor.h>
#include <zircon/threads.h>

#include "src/lib/fxl/strings/string_printf.h"
#include "src/virtualization/bin/vmm/sysinfo.h"

#if __aarch64__
static constexpr uint8_t kSpiBase = 32;
#endif

static constexpr uint32_t trap_kind(TrapType type) {
  switch (type) {
    case TrapType::MMIO_SYNC:
      return ZX_GUEST_TRAP_MEM;
    case TrapType::MMIO_BELL:
      return ZX_GUEST_TRAP_BELL;
    case TrapType::PIO_SYNC:
      return ZX_GUEST_TRAP_IO;
    default:
      ZX_PANIC("Unhandled TrapType %d\n", static_cast<int>(type));
      return 0;
  }
}

// Static.
uint64_t Guest::GetPageAlignedGuestMemory(uint64_t guest_memory) {
  const uint32_t page_size = zx_system_get_page_size();
  uint32_t page_alignment = guest_memory % page_size;
  if (page_alignment != 0) {
    uint32_t padding = page_size - page_alignment;
    FX_LOGS(INFO) << "The requested guest memory (" << guest_memory
                  << " bytes) is not a multiple of system page size (" << page_size
                  << " bytes), so increasing guest memory by " << padding << " bytes.";
    guest_memory += padding;
  }

  return guest_memory;
}

// Static.
void Guest::GenerateGuestMemoryRegions(uint64_t guest_memory,
                                       std::vector<GuestMemoryRegion>* regions) {
  // There will only be one guest memory region during this refactoring. This matches the current
  // behavior used by the now deprecated memory specification logic.
  // TODO(fxb/94972): Calculate accurate guest memory regions.
  regions->push_back({.base = 0x0, .size = guest_memory});
}

zx_status_t Guest::Init(uint64_t guest_memory) {
  zx::resource hypervisor_resource;
  zx_status_t status = get_hypervisor_resource(&hypervisor_resource);
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to get hypervisor resource";
    return status;
  }
  status = zx::guest::create(hypervisor_resource, 0, &guest_, &vmar_);
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to create guest";
    return status;
  }

  // If unaligned, round up to the nearest page.
  guest_memory = Guest::GetPageAlignedGuestMemory(guest_memory);

  // Generate guest memory regions, avoiding device memory.
  Guest::GenerateGuestMemoryRegions(guest_memory, &memory_regions_);

  // The VMO is sized to include any device regions inclusive of the guest memory ranges so that
  // there will always be a valid offset for any guest memory address.
  uint64_t vmo_size = memory_regions_.back().base + memory_regions_.back().size;
  if (vmo_size > kFirstDynamicDeviceAddr) {
    // Avoid a collision between static and dynamic address assignment.
    FX_PLOGS(ERROR, ZX_ERR_INVALID_ARGS)
        << "Requested guest memory with inclusive device regions must be less than "
        << kFirstDynamicDeviceAddr;
    return ZX_ERR_INVALID_ARGS;
  }

  zx::vmo vmo;
  status = zx::vmo::create(vmo_size, 0, &vmo);
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to create VMO of size " << vmo_size;
    return status;
  }

  zx::resource vmex_resource;
  status = get_vmex_resource(&vmex_resource);
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to get VMEX resource";
    return status;
  }
  status = vmo.replace_as_executable(vmex_resource, &vmo);
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to make VMO executable";
    return status;
  }

  for (const GuestMemoryRegion& region : memory_regions_) {
    zx_gpaddr_t addr;
    status = vmar_.map(ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_PERM_EXECUTE | ZX_VM_SPECIFIC |
                           ZX_VM_REQUIRE_NON_RESIZABLE,
                       region.base, vmo, region.base, region.size, &addr);
    if (status != ZX_OK) {
      FX_PLOGS(ERROR, status) << "Failed to map guest physical memory region " << region.size << "@"
                              << region.base;
      return status;
    }
  }

  // TODO(fxb/94972): Use memory layout information in PhysMem.
  status = phys_mem_.Init(std::move(vmo));
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to initialize guest physical memory";
    return status;
  }

  return ZX_OK;
}

zx_status_t Guest::CreateMapping(TrapType type, uint64_t addr, size_t size, uint64_t offset,
                                 IoHandler* handler, async_dispatcher_t* dispatcher) {
  uint32_t kind = trap_kind(type);
  mappings_.emplace_front(kind, addr, size, offset, handler);
  zx_status_t status = mappings_.front().SetTrap(this, dispatcher);
  if (status != ZX_OK) {
    mappings_.pop_front();
    return status;
  }
  return ZX_OK;
}

zx_status_t Guest::CreateSubVmar(uint64_t addr, size_t size, zx::vmar* vmar) {
  uintptr_t guest_addr;
  return vmar_.allocate(ZX_VM_CAN_MAP_READ | ZX_VM_CAN_MAP_WRITE | ZX_VM_SPECIFIC, addr, size, vmar,
                        &guest_addr);
}

zx_status_t Guest::StartVcpu(uint64_t id, zx_gpaddr_t entry, zx_gpaddr_t boot_ptr,
                             async::Loop* loop) {
  if (id >= kMaxVcpus) {
    FX_PLOGS(ERROR, ZX_ERR_OUT_OF_RANGE)
        << "Failed to start VCPU-" << id << ", up to " << kMaxVcpus << " VCPUs are supported";
    return ZX_ERR_OUT_OF_RANGE;
  }

  std::lock_guard<std::shared_mutex> lock(mutex_);
  if (!vcpus_[0].has_value() && id != 0) {
    FX_PLOGS(ERROR, ZX_ERR_BAD_STATE) << "VCPU-0 must be started before other VCPUs";
    return ZX_ERR_BAD_STATE;
  }
  if (vcpus_[id].has_value()) {
    // The guest might make multiple requests to start a particular VCPU. On
    // x86, the guest should send two START_UP IPIs but we initialize the VCPU
    // on the first. So, we ignore subsequent requests.
    return ZX_OK;
  }
  vcpus_[id].emplace(id, this, entry, boot_ptr, loop);
  return vcpus_[id]->Start();
}

zx_status_t Guest::Interrupt(uint64_t mask, uint32_t vector) {
  std::shared_lock<std::shared_mutex> lock(mutex_);
  for (size_t id = 0; id != kMaxVcpus; ++id) {
    if (!(mask & (1ul << id)) || !vcpus_[id]) {
      continue;
    }
    zx_status_t status = vcpus_[id]->Interrupt(vector);
    if (status != ZX_OK) {
      return status;
    }
#if __aarch64__
    if (vector >= kSpiBase) {
      break;
    }
#endif
  }
  return ZX_OK;
}
