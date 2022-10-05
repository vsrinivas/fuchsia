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
#include "src/virtualization/bin/vmm/pci.h"
#include "src/virtualization/bin/vmm/sysinfo.h"

namespace {

#if __aarch64__
constexpr uint8_t kSpiBase = 32;
#endif

constexpr GuestMemoryRegion RestrictUntilEnd(zx_gpaddr_t start) {
  return {start, kGuestMemoryAllRemainingRange};
}

#if __x86_64__
constexpr uint64_t kOneKibibyte = 1ul << 10;
constexpr uint64_t kOneMebibyte = 1ul << 20;
constexpr uint64_t kOneGibibyte = 1ul << 30;

constexpr GuestMemoryRegion RestrictRegion(zx_gpaddr_t start, zx_gpaddr_t end) {
  return {start, end - start};
}
#endif

// Ranges to avoid allocating guest memory in. These regions must not overlap and must be
// sorted by increasing base address. These requirements are enforced by a static_assert
// below.
constexpr std::array kRestrictedRegions = {
#if __aarch64__
    // For ARM PCI devices are mapped in at a relatively high address, so it's reasonable to just
    // block off the rest of guest memory.
    RestrictUntilEnd(std::min(kDevicePhysBase, kFirstDynamicDeviceAddr)),
#elif __x86_64__
    // Reserve regions in the first MiB for use by the BIOS.
    RestrictRegion(0x0, 32 * kOneKibibyte),
    RestrictRegion(512 * kOneKibibyte, kOneMebibyte),
    // For x86 PCI devices are mapped in somewhere below 4 GiB, and the range extends to 4 GiB.
    RestrictRegion(kDevicePhysBase, 4 * kOneGibibyte),
    // Dynamic devices are mapped in at a very high address, so everything beyond that point
    // can be blocked off.
    RestrictUntilEnd(kFirstDynamicDeviceAddr),
#endif
};

constexpr bool CheckForOverlappingRestrictedRegions() {
  auto overlaps = [](const GuestMemoryRegion& first, const GuestMemoryRegion& second) -> bool {
    const auto& begin = std::min(first, second, GuestMemoryRegion::CompareMinByBase);
    const auto& end = std::max(first, second, GuestMemoryRegion::CompareMinByBase);
    return begin.base + begin.size >= end.base;
  };

  for (auto curr = kRestrictedRegions.begin(); curr != kRestrictedRegions.end(); curr++) {
    for (auto next = std::next(curr); next != kRestrictedRegions.end(); next++) {
      if (overlaps(*curr, *next)) {
        return false;
      }
    }
  }

  return true;
}

// Compile time check that no regions overlap in kRestrictedRegions. If adding a region that
// overlaps with another, just merge them into one larger region.
static_assert(CheckForOverlappingRestrictedRegions());

constexpr bool CheckRestrictedRegionsAreSorted() {
  for (auto curr = kRestrictedRegions.begin(); curr != kRestrictedRegions.end(); curr++) {
    if (std::next(curr) == kRestrictedRegions.end()) {
      break;
    }
    if (!GuestMemoryRegion::CompareMinByBase(*curr, *std::next(curr))) {
      return false;
    }
  }

  return true;
}

// Compile time check that regions in kRestrictedRegions are sorted by increasing base address.
static_assert(CheckRestrictedRegionsAreSorted());

constexpr uint32_t trap_kind(TrapType type) {
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

}  // namespace

// Static.
cpp20::span<const GuestMemoryRegion> Guest::GetDefaultRestrictionsForArchitecture() {
  return kRestrictedRegions;
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
bool Guest::PageAlignGuestMemoryRegion(GuestMemoryRegion& region) {
  const uint32_t page_size = zx_system_get_page_size();

  // This guest region is bounded by restricted regions, so size cannot be increased. If this
  // region is smaller than a page this region must just be discarded.
  if (region.size < page_size) {
    return false;
  }

  zx_gpaddr_t start = region.base;
  zx_gpaddr_t end = region.base + region.size;

  // Round the starting address up to the nearest page, and the ending address down to the nearest
  // page.
  if (start % page_size != 0) {
    start += page_size - (start % page_size);
  }
  if (end % page_size != 0) {
    end -= end % page_size;
  }

  // Require a valid region to be at least a single page in size after adjustments. Both start and
  // end have just been page aligned.
  if (start >= end) {
    return false;
  }

  region.base = start;
  region.size = end - start;

  return true;
}

// Static.
bool Guest::GenerateGuestMemoryRegions(uint64_t guest_memory,
                                       cpp20::span<const GuestMemoryRegion> restrictions,
                                       std::vector<GuestMemoryRegion>* regions) {
  // Special case where there's no restrictions. Currently this isn't true for any production
  // architecture due to the need to assign dynamic device addresses.
  if (restrictions.empty()) {
    regions->push_back({.base = 0x0, .size = guest_memory});
    return true;
  }

  bool first_region = true;
  GuestMemoryRegion current_region;
  auto restriction = restrictions.begin();
  fit::function<bool()> next_range = [&]() -> bool {
    if (first_region) {
      first_region = false;
      if (restriction->base != 0) {
        current_region = {0x0, restriction->base};
      } else {
        return next_range();
      }
    } else {
      if (restriction->size == kGuestMemoryAllRemainingRange) {
        return false;  // No remaining valid guest memory regions.
      }

      // The current unrestricted region extends from the end of the current restriction to the
      // start of the next restriction, or if this is the last restriction it extends to a very
      // large number.
      zx_gpaddr_t unrestricted_base_address = restriction->base + restriction->size;
      uint64_t unrestricted_size = std::next(restriction) == restrictions.end()
                                       ? kGuestMemoryAllRemainingRange - unrestricted_base_address
                                       : std::next(restriction)->base - unrestricted_base_address;

      current_region = {unrestricted_base_address, unrestricted_size};
      restriction++;
    }

    if (!Guest::PageAlignGuestMemoryRegion(current_region)) {
      return next_range();
    }

    return true;
  };

  uint64_t mem_required = guest_memory;
  while (mem_required > 0) {
    if (!next_range()) {
      FX_LOGS(ERROR) << "Unable to allocate enough guest memory due to guest memory restrictions. "
                        "Managed to allocate "
                     << guest_memory - mem_required << " of " << guest_memory << " bytes";
      return false;
    }

    uint64_t mem_used = std::min(current_region.size, mem_required);
    regions->push_back({current_region.base, mem_used});
    mem_required -= mem_used;
  }

  return true;
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
  if (!Guest::GenerateGuestMemoryRegions(
          guest_memory, Guest::GetDefaultRestrictionsForArchitecture(), &memory_regions_)) {
    FX_PLOGS(ERROR, ZX_ERR_INVALID_ARGS) << "Failed to place guest memory avoiding device memory "
                                            "ranges. Try requesting less memory.";
  }

  // The VMO is sized to include any device regions inclusive of the guest memory ranges so that
  // there will always be a valid offset for any guest memory address.
  uint64_t vmo_size = memory_regions_.back().base + memory_regions_.back().size;

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

  std::vector<GuestMemoryRegion> vmar_regions = memory_regions_;
#if __x86_64__
  // x86 has reserved memory from 0 to 32KiB, and 512KiB to 1MiB. While we will not allocate guest
  // memory in those regions, we still want to map these regions into the guest VMAR as they are
  // not devices and we do not wish to trap on them.
  vmar_regions.push_back({0, 32 * kOneKibibyte});
  vmar_regions.push_back({512 * kOneKibibyte, 512 * kOneKibibyte});
#endif

  for (const GuestMemoryRegion& region : vmar_regions) {
    zx_gpaddr_t addr;
    status = vmar_.map(ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_PERM_EXECUTE | ZX_VM_SPECIFIC |
                           ZX_VM_REQUIRE_NON_RESIZABLE,
                       region.base, vmo, region.base, region.size, &addr);
    if (status != ZX_OK) {
      FX_PLOGS(ERROR, status) << "Failed to map guest physical memory region " << region.base
                              << " - " << region.base + region.size;
      return status;
    }
  }

  status = phys_mem_.Init(vmar_regions, std::move(vmo));
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

zx_status_t Guest::StartVcpu(uint64_t id, zx_gpaddr_t entry, zx_gpaddr_t boot_ptr) {
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
  vcpus_[id].emplace(id, this, entry, boot_ptr);
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

void Guest::set_stop_callback(
    fit::function<void(fit::result<::fuchsia::virtualization::GuestError>)> stop_callback) {
  stop_callback_ = std::move(stop_callback);
}

void Guest::Stop(fit::result<::fuchsia::virtualization::GuestError> result) {
  FX_CHECK(stop_callback_);
  stop_callback_(result);
}
