// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_DEVICES_BUS_DRIVERS_PCI_TEST_FAKES_FAKE_DEVICE_H_
#define SRC_DEVICES_BUS_DRIVERS_PCI_TEST_FAKES_FAKE_DEVICE_H_

#include <lib/zx/vmo.h>
#include <zircon/hw/pci.h>

#include <cstdint>

#include <fbl/algorithm.h>
#include <fbl/ref_counted.h>

#include "../../common.h"
#include "../../config.h"
#include "../../device.h"
#include "../../ref_counted.h"

class FakeDevice final : public pci::Device {
 public:
  FakeDevice(zx_device_t* parent, std::unique_ptr<pci::Config>&& config,
             pci::UpstreamNode* upstream, pci::BusDeviceInterface* bdi, bool is_bridge)
      : pci::Device(parent, std::move(config), upstream, bdi, is_bridge){};
  PCI_IMPLEMENT_REFCOUNTED;
};

#endif  // SRC_DEVICES_BUS_DRIVERS_PCI_TEST_FAKES_FAKE_DEVICE_H_
