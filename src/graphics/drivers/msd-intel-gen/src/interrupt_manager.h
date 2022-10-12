// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef INTERRUPT_MANAGER_H
#define INTERRUPT_MANAGER_H

#include <type_traits>

#include "msd_intel_register_io.h"
#include "platform_interrupt.h"
#include "platform_pci_device.h"

class InterruptManager {
 public:
  class Owner {
   public:
    virtual MsdIntelRegisterIo* register_io_for_interrupt() = 0;
    virtual magma::PlatformPciDevice* platform_device() = 0;
  };

  virtual ~InterruptManager() {}

  using InterruptCallback =
      std::add_pointer_t<void(void* data, uint32_t master_interrupt_control, uint64_t timestamp)>;

  virtual bool RegisterCallback(InterruptCallback callback, void* data,
                                uint32_t interrupt_mask) = 0;

  static std::unique_ptr<InterruptManager> CreateShim(Owner* owner);
};

#endif  // INTERRUPT_MANAGER_H
