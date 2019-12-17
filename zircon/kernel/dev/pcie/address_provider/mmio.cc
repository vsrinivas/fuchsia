// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <trace.h>

#include <dev/address_provider/address_provider.h>
#include <dev/pci_common.h>
#include <kernel/range_check.h>
#include <ktl/move.h>

#define LOCAL_TRACE 0

MmioPcieAddressProvider::~MmioPcieAddressProvider() {
  // Unmap and free all of our mapped ECAM regions
  ecam_regions_.clear();
}

zx_status_t MmioPcieAddressProvider::Translate(uint8_t bus_id, uint8_t device_id,
                                               uint8_t function_id, vaddr_t* virt, paddr_t* phys) {
  // Find the region which would contain this bus_id, if any.
  // add does not overlap with any already defined regions.
  Guard<Mutex> guard{&ecam_region_lock_};
  auto iter = ecam_regions_.upper_bound(static_cast<uint8_t>(bus_id));
  --iter;

  if (!iter.IsValid()) {
    return ZX_ERR_NOT_FOUND;
  }

  if ((bus_id < iter->ecam().bus_start) || (bus_id > iter->ecam().bus_end)) {
    return ZX_ERR_NOT_FOUND;
  }

  bus_id = static_cast<uint8_t>(bus_id - iter->ecam().bus_start);
  size_t offset = (static_cast<size_t>(bus_id) << 20) | (static_cast<size_t>(device_id) << 15) |
                  (static_cast<size_t>(function_id) << 12);

  if (phys) {
    *phys = iter->ecam().phys_base + offset;
  }

  // TODO(cja): Move to a BDF based associative container for better lookup time
  // and insert or find behavior.
  *virt = reinterpret_cast<uintptr_t>(static_cast<uint8_t*>(iter->vaddr()) + offset);
  return ZX_OK;
}

zx_status_t MmioPcieAddressProvider::AddEcamRegion(const PciEcamRegion& ecam) {
  // Sanity check the region first.
  if (ecam.bus_start > ecam.bus_end) {
    return ZX_ERR_INVALID_ARGS;
  }

  size_t bus_count = static_cast<size_t>(ecam.bus_end) - ecam.bus_start + 1u;
  if (ecam.size != (PCIE_ECAM_BYTE_PER_BUS * bus_count)) {
    return ZX_ERR_INVALID_ARGS;
  }

  // Grab the ECAM lock and make certain that the region we have been asked to
  // add does not overlap with any already defined regions.
  Guard<Mutex> guard{&ecam_region_lock_};
  auto iter = ecam_regions_.upper_bound(ecam.bus_start);
  --iter;

  // If iter is valid, it now points to the region with the largest bus_start
  // which is <= ecam.bus_start.  If any region overlaps with the region we
  // are attempting to add, it will be this one.
  if (iter.IsValid()) {
    const uint8_t iter_start = iter->ecam().bus_start;
    const size_t iter_len = iter->ecam().bus_end - iter->ecam().bus_start + 1;

    const uint8_t bus_start = ecam.bus_start;
    const size_t bus_len = ecam.bus_end - ecam.bus_start + 1;

    if (Intersects(iter_start, iter_len, bus_start, bus_len)) {
      return ZX_ERR_BAD_STATE;
    }
  }

  // Looks good.  Attempt to allocate and map this ECAM region.
  fbl::AllocChecker ac;
  ktl::unique_ptr<MappedEcamRegion> region(new (&ac) MappedEcamRegion(ecam));
  if (!ac.check()) {
    TRACEF("Failed to allocate ECAM region for bus range [0x%02x, 0x%02x]\n", ecam.bus_start,
           ecam.bus_end);
    return ZX_ERR_NO_MEMORY;
  }

  zx_status_t res = region->MapEcam();
  if (res != ZX_OK) {
    TRACEF("Failed to map ECAM region for bus range [0x%02x, 0x%02x]\n", ecam.bus_start,
           ecam.bus_end);
    return res;
  }

  // Everything checks out.  Add the new region to our set of regions and we are done.
  ecam_regions_.insert(ktl::move(region));
  return ZX_OK;
}

fbl::RefPtr<PciConfig> MmioPcieAddressProvider::CreateConfig(const uintptr_t addr) {
  return PciConfig::Create(addr, PciAddrSpace::MMIO);
}
