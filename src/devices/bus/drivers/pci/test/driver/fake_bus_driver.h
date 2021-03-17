// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_DEVICES_BUS_DRIVERS_PCI_TEST_DRIVER_FAKE_BUS_DRIVER_H_
#define SRC_DEVICES_BUS_DRIVERS_PCI_TEST_DRIVER_FAKE_BUS_DRIVER_H_

#include <lib/inspect/cpp/inspector.h>

#include <ddktl/device.h>

#include "src/devices/bus/drivers/pci/test/driver/driver_tests.h"
#include "src/devices/bus/drivers/pci/test/fakes/fake_bus.h"
#include "src/devices/bus/drivers/pci/test/fakes/fake_config.h"
#include "src/devices/bus/drivers/pci/test/fakes/fake_pciroot.h"
#include "src/devices/bus/drivers/pci/test/fakes/fake_upstream_node.h"

namespace pci {

class FakeBusDriver;
using FakeBusDriverType = ddk::Device<FakeBusDriver>;
class FakeBusDriver : public FakeBusDriverType {
 public:
  static zx_status_t Create(zx_device_t* parent, const char* name, uint8_t start_bus = 0,
                            uint8_t end_bus = 0);
  ~FakeBusDriver() = default;
  zx_status_t CreateDevice(pci_bdf_t bdf, uint8_t* base_cfg, size_t base_cfg_size,
                           uint16_t vid = PCI_TEST_DRIVER_VID, uint16_t did = PCI_TEST_DRIVER_DID);

  FakePciType0Config& GetDevice(pci_bdf_t bdf) { return bus().pciroot().ecam().get(bdf).device; }
  FakePciType1Config& GetBridge(pci_bdf_t bdf) { return bus().pciroot().ecam().get(bdf).bridge; }
  uint8_t* GetRawConfig(pci_bdf_t bdf) { return bus().pciroot().ecam().get(bdf).config; }
  uint8_t* GetRawExtConfig(pci_bdf_t bdf) { return bus().pciroot().ecam().get(bdf).config; }

  FakeUpstreamNode& upstream() { return upstream_; }
  FakeBus& bus() { return bus_; }
  pci_bdf_t test_bdf() { return test_bdf_; }
  void DdkRelease() { delete this; }

 private:
  FakeBusDriver(zx_device_t* parent, uint8_t bus_start, uint8_t bus_end)
      : FakeBusDriverType(parent),
        upstream_(UpstreamNode::Type::ROOT, 0),
        bus_(bus_start, bus_end) {}

  FakeUpstreamNode upstream_;
  FakeBus bus_;
  const pci_bdf_t test_bdf_ = {PCI_TEST_BUS_ID, PCI_TEST_DEV_ID, PCI_TEST_FUNC_ID};
  inspect::Inspector inspector_;
};
}  // namespace pci

#endif  // SRC_DEVICES_BUS_DRIVERS_PCI_TEST_DRIVER_FAKE_BUS_DRIVER_H_
