// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GTT_H
#define GTT_H

#include <memory>
#include <mutex>

#include "address_space.h"
#include "platform_pci_device.h"

class Gtt : public AddressSpace {
 public:
  class Owner : public magma::AddressSpaceOwner {
   public:
    virtual magma::PlatformPciDevice* platform_device() = 0;
  };

  Gtt(Owner* owner) : AddressSpace(owner, ADDRESS_SPACE_GGTT) {}

  virtual bool Init(uint64_t gtt_size) = 0;

  bool InsertLocked(uint64_t addr, magma::PlatformBusMapper::BusMapping* bus_mapping,
                    uint32_t guard_page_count) override {
    DASSERT(false);
    return false;
  }

  bool InsertLocked(uint64_t addr, magma::PlatformBuffer* buffer, uint64_t page_offset,
                    uint64_t page_count) override {
    return DRETF(false, "Must be overridden by derived class");
  }

  static std::unique_ptr<Gtt> CreateShim(Owner* owner);

  friend class TestGtt;
};

#endif  // GTT_H
