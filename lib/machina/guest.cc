// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/machina/guest.h"

#include <fcntl.h>
#include <limits.h>
#include <string.h>
#include <unistd.h>

#include <fbl/unique_fd.h>
#include <fuchsia/sysinfo/c/fidl.h>
#include <lib/fdio/util.h>
#include <lib/zx/channel.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/hypervisor.h>
#include <zircon/syscalls/port.h>
#include <zircon/threads.h>

#include "garnet/lib/machina/io.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/strings/string_printf.h"

static constexpr char kSysInfoPath[] = "/dev/misc/sysinfo";
// Number of threads reading from the async device port.
static constexpr size_t kNumAsyncWorkers = 2;
static constexpr uint32_t kMapFlags =
    ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_PERM_EXECUTE | ZX_VM_SPECIFIC;
static constexpr uint32_t kAllocateFlags =
    ZX_VM_CAN_MAP_READ | ZX_VM_CAN_MAP_WRITE | ZX_VM_SPECIFIC;

template <zx_status_t (*GetResource)(zx_handle_t, zx_status_t*, zx_handle_t*)>
static zx_status_t get_resource(zx::resource* resource) {
  fbl::unique_fd fd(open(kSysInfoPath, O_RDWR));
  if (!fd) {
    return ZX_ERR_IO;
  }

  zx::channel channel;
  zx_status_t status =
      fdio_get_service_handle(fd.release(), channel.reset_and_get_address());
  if (status != ZX_OK) {
    return status;
  }

  zx_status_t fidl_status =
      GetResource(channel.get(), &status, resource->reset_and_get_address());
  if (fidl_status != ZX_OK) {
    return fidl_status;
  }
  return status;
}

static constexpr uint32_t trap_kind(machina::TrapType type) {
  switch (type) {
    case machina::TrapType::MMIO_SYNC:
      return ZX_GUEST_TRAP_MEM;
    case machina::TrapType::MMIO_BELL:
      return ZX_GUEST_TRAP_BELL;
    case machina::TrapType::PIO_SYNC:
      return ZX_GUEST_TRAP_IO;
    default:
      ZX_PANIC("Unhandled TrapType %d\n", static_cast<int>(type));
      return 0;
  }
}

namespace machina {

zx_status_t Guest::Init(size_t mem_size, bool host_memory) {
  zx::vmo vmo;
  if (host_memory) {
    zx::resource resource;
    zx_status_t status =
        get_resource<fuchsia_sysinfo_DeviceGetRootResource>(&resource);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to get root resource " << status;
      return status;
    }
    status = zx::vmo::create_physical(resource, 0, mem_size, &vmo);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to create physical VMO " << status;
      return status;
    }
    status = vmo.set_cache_policy(ZX_CACHE_POLICY_CACHED);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to set cache policy on VMO " << status;
      return status;
    }
  } else {
    zx_status_t status = zx::vmo::create(mem_size, ZX_VMO_NON_RESIZABLE, &vmo);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to create VMO " << status;
      return status;
    }
  }

  zx_status_t status = phys_mem_.Init(std::move(vmo));
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to initialize guest physical memory " << status;
    return status;
  }

  zx::resource resource;
  status = get_resource<fuchsia_sysinfo_DeviceGetHypervisorResource>(&resource);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to get hypervisor resource " << status;
    return status;
  }

  status = zx::guest::create(resource, 0, &guest_, &vmar_);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to create guest " << status;
    return status;
  }

  zx_gpaddr_t addr;
  status = vmar_.map(0, phys_mem_.vmo(), 0, mem_size, kMapFlags, &addr);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to map guest physical memory " << status;
    return status;
  }

  for (size_t i = 0; i < kNumAsyncWorkers; ++i) {
    auto name = fxl::StringPrintf("io-handler-%zu", i);
    status = device_loop_.StartThread(name.c_str());
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to create async worker " << status;
      return status;
    }
  }

  return ZX_OK;
}

zx_status_t Guest::CreateMapping(TrapType type, uint64_t addr, size_t size,
                                 uint64_t offset, IoHandler* handler) {
  uint32_t kind = trap_kind(type);
  mappings_.emplace_front(kind, addr, size, offset, handler);
  zx_status_t status = mappings_.front().SetTrap(this);
  if (status != ZX_OK) {
    mappings_.pop_front();
    return status;
  }
  return ZX_OK;
}

void Guest::RegisterVcpuFactory(VcpuFactory factory) {
  vcpu_factory_ = std::move(factory);
}

zx_status_t Guest::StartVcpu(uintptr_t entry, uint64_t id) {
  if (id >= kMaxVcpus) {
    return ZX_ERR_INVALID_ARGS;
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

  auto vcpu = std::make_unique<Vcpu>();
  zx_status_t status = vcpu_factory_(this, entry, id, vcpu.get());
  if (status != ZX_OK) {
    return status;
  }
  vcpus_[id] = std::move(vcpu);
  return ZX_OK;
}

zx_status_t Guest::Interrupt(uint64_t mask, uint8_t vector) {
  std::shared_lock<std::shared_mutex> lock(mutex_);
  for (size_t id = 0; id != kMaxVcpus; ++id) {
    if (vcpus_[id] == nullptr || !((1u << id) & mask)) {
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

zx_status_t Guest::CreateSubVmar(uint64_t addr, size_t size, zx::vmar* vmar) {
  uintptr_t guest_addr;
  return vmar_.allocate(addr, size, kAllocateFlags, vmar, &guest_addr);
}

}  // namespace machina
