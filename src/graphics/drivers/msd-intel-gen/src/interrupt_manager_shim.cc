// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "interrupt_manager.h"
#include "msd_intel_pci_device.h"

class InterruptManagerShim : public InterruptManager {
 public:
  InterruptManagerShim(Owner* owner) : owner_(owner) {}

  ~InterruptManagerShim();

  bool RegisterCallback(InterruptCallback callback, void* data, uint32_t interrupt_mask) override;

 private:
  MsdIntelPciDevice* pci_device() {
    return static_cast<MsdIntelPciDevice*>(owner_->platform_device());
  }

  Owner* owner_;
};

InterruptManagerShim::~InterruptManagerShim() { pci_device()->UnregisterInterruptCallback(); }

bool InterruptManagerShim::RegisterCallback(InterruptCallback callback, void* data,
                                            uint32_t interrupt_mask) {
  return pci_device()->RegisterInterruptCallback(callback, data, interrupt_mask);
}

///////////////////////////////////////////////////////////////////////////////////////////////////

std::unique_ptr<InterruptManager> InterruptManager::CreateShim(Owner* owner) {
  return std::make_unique<InterruptManagerShim>(owner);
}
