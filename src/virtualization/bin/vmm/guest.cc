// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/vmm/guest.h"

#include <fcntl.h>
#include <fuchsia/sysinfo/c/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/zx/channel.h>
#include <limits.h>
#include <string.h>
#include <unistd.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/hypervisor.h>
#include <zircon/syscalls/port.h>
#include <zircon/threads.h>

#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/string_printf.h"
#include "src/virtualization/bin/vmm/sysinfo.h"

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

static zx_status_t get_hypervisor_resource(const fuchsia::sysinfo::SysInfoSyncPtr& sysinfo,
                                           zx::resource* resource) {
  zx_status_t fidl_status;
  zx_status_t status = sysinfo->GetHypervisorResource(&fidl_status, resource);
  if (status != ZX_OK) {
    return status;
  }
  return fidl_status;
}

static constexpr uint32_t cache_policy(fuchsia::virtualization::MemoryPolicy policy) {
  switch (policy) {
    case fuchsia::virtualization::MemoryPolicy::HOST_DEVICE:
      return ZX_CACHE_POLICY_UNCACHED_DEVICE;
    default:
      return ZX_CACHE_POLICY_CACHED;
  }
}

zx_status_t Guest::Init(const std::vector<fuchsia::virtualization::MemorySpec>& memory) {
  fuchsia::sysinfo::SysInfoSyncPtr sysinfo = get_sysinfo();
  zx::resource hypervisor_resource;
  zx_status_t status = get_hypervisor_resource(sysinfo, &hypervisor_resource);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to get hypervisor resource " << status;
    return status;
  }
  status = zx::guest::create(hypervisor_resource, 0, &guest_, &vmar_);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to create guest " << status;
    return status;
  }

  zx::resource root_resource;
  for (const fuchsia::virtualization::MemorySpec& spec : memory) {
    zx::vmo vmo;
    switch (spec.policy) {
      case fuchsia::virtualization::MemoryPolicy::GUEST_CACHED:
        status = zx::vmo::create(spec.size, 0, &vmo);
        if (status != ZX_OK) {
          FXL_LOG(ERROR) << "Failed to create VMO " << status;
          return status;
        }
        break;
      case fuchsia::virtualization::MemoryPolicy::HOST_CACHED:
      case fuchsia::virtualization::MemoryPolicy::HOST_DEVICE:
        if (!root_resource) {
          status = get_root_resource(&root_resource);
          if (status != ZX_OK) {
            FXL_LOG(ERROR) << "Failed to get root resource " << status;
            return status;
          }
        }
        status = zx::vmo::create_physical(root_resource, spec.base, spec.size, &vmo);
        if (status != ZX_OK) {
          FXL_LOG(ERROR) << "Failed to create physical VMO " << status;
          return status;
        }
        status = vmo.set_cache_policy(cache_policy(spec.policy));
        if (status != ZX_OK) {
          FXL_LOG(ERROR) << "Failed to set cache policy on VMO " << status;
          return status;
        }
        break;
      default:
        FXL_LOG(ERROR) << "Unknown memory policy " << static_cast<uint32_t>(spec.policy);
        return ZX_ERR_INVALID_ARGS;
    }

    status = vmo.replace_as_executable(zx::handle(), &vmo);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to make VMO executable " << status;
      return status;
    }

    zx_gpaddr_t addr;
    status = vmar_.map(spec.base, vmo, 0, spec.size,
                       ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_PERM_EXECUTE | ZX_VM_SPECIFIC |
                           ZX_VM_REQUIRE_NON_RESIZABLE,
                       &addr);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to map guest physical memory " << status;
      return status;
    }
    if (!phys_mem_.vmo() && spec.policy == fuchsia::virtualization::MemoryPolicy::GUEST_CACHED) {
      status = phys_mem_.Init(std::move(vmo));
      if (status != ZX_OK) {
        FXL_LOG(ERROR) << "Failed to initialize guest physical memory " << status;
        return status;
      }
    }
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
  return vmar_.allocate(addr, size, ZX_VM_CAN_MAP_READ | ZX_VM_CAN_MAP_WRITE | ZX_VM_SPECIFIC, vmar,
                        &guest_addr);
}

zx_status_t Guest::StartVcpu(uint64_t id, zx_gpaddr_t entry, zx_gpaddr_t boot_ptr) {
  if (id >= kMaxVcpus) {
    FXL_LOG(ERROR) << "Failed to start VCPU-" << id << ", up to " << kMaxVcpus
                   << " VCPUs are supported";
    return ZX_ERR_OUT_OF_RANGE;
  }

  std::lock_guard<std::shared_mutex> lock(mutex_);
  if (vcpus_[0] == nullptr && id != 0) {
    FXL_LOG(ERROR) << "VCPU-0 must be started before other VCPUs";
    return ZX_ERR_BAD_STATE;
  }
  if (vcpus_[id] != nullptr) {
    // The guest might make multiple requests to start a particular VCPU. On
    // x86, the guest should send two START_UP IPIs but we initialize the VCPU
    // on the first. So, we ignore subsequent requests.
    return ZX_OK;
  }
  vcpus_[id] = std::make_unique<Vcpu>(id, this, entry, boot_ptr);
  vcpus_[id]->Start();
  return ZX_OK;
}

zx_status_t Guest::Interrupt(uint64_t mask, uint8_t vector) {
  std::shared_lock<std::shared_mutex> lock(mutex_);
  for (size_t id = 0; id != kMaxVcpus; ++id) {
    if (!(mask & (1ul << id)) || !vcpus_[id]) {
      continue;
    }
    zx_status_t status = vcpus_[id]->Interrupt(vector);
    if (status != ZX_OK) {
      return status;
    }
  }
  return ZX_OK;
}

zx_status_t Guest::Join() {
  std::shared_lock<std::shared_mutex> lock(mutex_);
  // We assume that the VCPU-0 thread will be started first, and that no
  // additional VCPUs will be brought up after it terminates.
  zx_status_t status = vcpus_[0]->Join();

  // Once the initial VCPU has terminated, wait for any additional VCPUs.
  for (size_t id = 1; id != kMaxVcpus; ++id) {
    if (vcpus_[id] != nullptr) {
      zx_status_t vcpu_status = vcpus_[id]->Join();
      if (vcpu_status != ZX_OK) {
        status = vcpu_status;
      }
    }
  }

  return status;
}
