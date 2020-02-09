// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtt.h"
#include "msd_intel_pci_device.h"

class GttShim : public Gtt {
 public:
  GttShim(Owner* owner) : Gtt(owner), owner_(owner) {}

  uint64_t Size() const override { return pci_device()->GetGtt()->Size(); }

  // Init only on core gtt
  bool Init(uint64_t gtt_size) override {
    DASSERT(false);
    return false;
  }

 private:
  // AddressSpace overrides
  bool AllocLocked(size_t size, uint8_t align_pow2, uint64_t* addr_out) override {
    return pci_device()->GetGtt()->Alloc(size, align_pow2, addr_out);
  }
  bool FreeLocked(uint64_t addr) override { return pci_device()->GetGtt()->Free(addr); }

  bool ClearLocked(uint64_t addr, magma::PlatformBusMapper::BusMapping* bus_mapping) override {
    // Never a bus mapping here, GTT insertions performed via GlobalGttInsertLocked below.
    DASSERT(!bus_mapping);
    return pci_device()->GetGtt()->Clear(addr, nullptr);
  }

  bool InsertLocked(uint64_t addr, magma::PlatformBuffer* buffer, uint64_t page_offset,
                    uint64_t page_count) override {
    return pci_device()->GetGtt()->Insert(addr, buffer, page_offset, page_count);
  }

  MsdIntelPciDevice* pci_device() const {
    return static_cast<MsdIntelPciDevice*>(owner_->platform_device());
  }

  Owner* owner_;
};

std::unique_ptr<Gtt> Gtt::CreateShim(Owner* owner) { return std::make_unique<GttShim>(owner); }
