// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/pciroot/cpp/banjo.h>
#include <lib/fake_ddk/fake_ddk.h>
#include <lib/mmio/mmio.h>
#include <lib/zx/bti.h>
#include <lib/zx/status.h>
#include <zircon/errors.h>
#include <zircon/limits.h>
#include <zircon/syscalls/object.h>

#include <memory>

#include <fbl/ref_ptr.h>
#include <zxtest/zxtest.h>

#include "src/devices/bus/drivers/pci/upstream_node.h"
#include "src/devices/bus/drivers/pci/test/fakes/fake_ecam.h"
#include "src/devices/bus/drivers/pci/test/fakes/fake_pciroot.h"
#include "src/devices/bus/drivers/pci/test/fakes/fake_upstream_node.h"
#include "src/devices/bus/drivers/pci/bus.h"
#include "src/devices/bus/drivers/pci/common.h"

namespace pci {

class PciBusTests : public zxtest::Test {
 public:
  PciBusTests() : pciroot_(0, 1) {
    fbl::Array<fake_ddk::ProtocolEntry> protocols(new fake_ddk::ProtocolEntry[1], 1);
    protocols[0] = {ZX_PROTOCOL_PCIROOT,
                    *reinterpret_cast<const fake_ddk::Protocol*>(pciroot_.proto())};
    bind_.SetProtocols(std::move(protocols));
  }

 protected:
  // Sets up 5 devices, including two under a bridge.
  uint32_t SetupTopology() {
    uint8_t idx = 1;
    auto& ecam = pciroot_.ecam();
    ecam.get({0, 0, idx}).device.set_vendor_id(idx).set_device_id(idx++);
    ecam.get({0, 0, idx}).device.set_vendor_id(idx).set_device_id(idx++);
    ecam.get({0, 1, idx})
        .bridge.set_vendor_id(idx)
        .set_device_id(idx++)
        .set_header_type(PCI_HEADER_TYPE_BRIDGE)
        .set_secondary_bus_number(1);
    ecam.get({1, 0, idx}).device.set_vendor_id(idx).set_device_id(idx++);
    ecam.get({1, 0, idx}).device.set_vendor_id(idx).set_device_id(idx);
    return idx;
  }
  void SetUp() final { pciroot_.ecam().reset(); }
  auto& pciroot() { return pciroot_; }

 private:
  FakePciroot pciroot_;
  fake_ddk::Bind bind_;
};

// An encapsulated pci::Bus to allow inspection of some internal state.
class TestBus : public pci::Bus {
 public:
  TestBus(zx_device_t* parent, const pciroot_protocol_t* pciroot, const pci_platform_info_t info,
          std::optional<ddk::MmioBuffer> ecam)
      : pci::Bus(parent, pciroot, info, std::move(ecam)) {}

  size_t GetDeviceCount() {
    fbl::AutoLock _(devices_lock());
    return devices().size();
  }

  pci::Device* GetDevice(pci_bdf_t bdf) {
    fbl::AutoLock _(devices_lock());
    auto iter = devices().find(bdf);
    return &*iter;
  }

  size_t GetSharedIrqCount() {
    fbl::AutoLock _(devices_lock());
    return shared_irqs().size();
  }

  size_t GetLegacyIrqCount() {
    fbl::AutoLock _(devices_lock());
    return legacy_irqs().size();
  }
};

// Bind tests the entire initialization path using an ECAM included via platform information.
// TODO(66253): disabled until fake_ddk handles the device lifecycle contract
// better and provides a method so we can force the unbind. As it is now, ASAN
// will notice the allocation leaks from the Bus construction.
TEST_F(PciBusTests, DISABLED_Bind) {
  SetupTopology();
  ASSERT_OK(pci_bus_bind(nullptr, fake_ddk::kFakeParent));
}

// The lifecycle test is done through Proxy configs to ensure we don't need to worry
// about ownership of the vmo the MmioBuffers would share.
TEST_F(PciBusTests, Lifecycle) {
  uint32_t dev_cnt = SetupTopology();
  auto bus = std::make_unique<TestBus>(fake_ddk::kFakeParent, pciroot().proto(), pciroot().info(),
                                       std::nullopt);
  ASSERT_OK(bus->Initialize());
  ASSERT_EQ(bus->GetDeviceCount(), dev_cnt);
}

TEST_F(PciBusTests, BdiGetBti) {
  pciroot().ecam().get(pci_bdf_t{}).device.set_vendor_id(8086).set_device_id(8086);
  auto bus = std::make_unique<TestBus>(fake_ddk::kFakeParent, pciroot().proto(), pciroot().info(),
                                       pciroot().ecam().CopyEcam());
  ASSERT_OK(bus->Initialize());
  ASSERT_EQ(bus->GetDeviceCount(), 1);

  zx::bti bti = {};
  ASSERT_EQ(bus->GetBti(nullptr, 0, &bti), ZX_ERR_INVALID_ARGS);
  ASSERT_OK(bus->GetBti(bus->GetDevice(pci_bdf_t{}), 0, &bti));

  zx_info_bti_t info = {};
  zx_info_bti_t info2 = {};
  ASSERT_OK(bti.get_info(ZX_INFO_BTI, &info, sizeof(info), nullptr, nullptr));
  ASSERT_OK(pciroot().bti().get_info(ZX_INFO_BTI, &info2, sizeof(info2), nullptr, nullptr));
  ASSERT_EQ(info.aspace_size, info2.aspace_size);
  ASSERT_EQ(info.minimum_contiguity, info2.minimum_contiguity);
  ASSERT_EQ(info.pmo_count, info2.pmo_count);
  ASSERT_EQ(info.quarantine_count, info2.quarantine_count);
}

TEST_F(PciBusTests, BdiAllocateMsi) {
  auto bus = std::make_unique<TestBus>(fake_ddk::kFakeParent, pciroot().proto(), pciroot().info(),
                                       pciroot().ecam().CopyEcam());
  ASSERT_OK(bus->Initialize());

  for (uint32_t cnt = 1; cnt <= 32; cnt *= 2) {
    zx::msi msi = {};
    bus->AllocateMsi(cnt, &msi);

    zx_info_msi_t info = {};
    ASSERT_OK(msi.get_info(ZX_INFO_MSI, &info, sizeof(info), nullptr, nullptr));
    EXPECT_EQ(info.num_irq, cnt);
  }
}

TEST_F(PciBusTests, BdiLinkUnlinkDevice) {
  pciroot().ecam().get(pci_bdf_t{}).device.set_vendor_id(8086).set_device_id(8086);
  auto bus = std::make_unique<TestBus>(fake_ddk::kFakeParent, pciroot().proto(), pciroot().info(),
                                       pciroot().ecam().CopyEcam());
  ASSERT_OK(bus->Initialize());
  ASSERT_EQ(bus->GetDeviceCount(), 1);

  auto device = bus->GetDevice(pci_bdf_t{});
  auto reffed_device = fbl::RefPtr(bus->GetDevice(pci_bdf_t{}));
  EXPECT_EQ(bus->LinkDevice(reffed_device), ZX_ERR_ALREADY_EXISTS);
  EXPECT_OK(bus->UnlinkDevice(device));
  EXPECT_EQ(bus->GetDeviceCount(), 0);
  EXPECT_EQ(bus->UnlinkDevice(device), ZX_ERR_NOT_FOUND);

  // Insert the device back into the bus topology so the disable / unplug
  // lifecycle runs. Otherwise, the normal teardown path of Device will assert
  // that it was never disabled.
  ASSERT_OK(bus->LinkDevice(fbl::RefPtr(device)));
  ASSERT_EQ(bus->GetDeviceCount(), 1);
}

TEST_F(PciBusTests, IrqRoutingEntries) {
  // Add |int_cnt| interrupts, but make them share vectors based on |int_mod|. This ensures that we
  // handle duplicate IRQ entries properly.
  const size_t int_cnt = 5;
  const uint32_t int_mod = 3;
  zx::interrupt interrupt = {};
  for (uint32_t i = 0; i < int_cnt; i++) {
    ASSERT_OK(zx::interrupt::create(*zx::unowned_resource(ZX_HANDLE_INVALID), i,
                                    ZX_INTERRUPT_VIRTUAL, &interrupt));
    pciroot().legacy_irqs().push_back(
        pci_legacy_irq_t{.interrupt = interrupt.get(), .vector = i % int_mod});
    // The bus will take ownership of this.
    (void)interrupt.release();
  }
  pciroot().ecam().get(pci_bdf_t{}).device.set_vendor_id(1).set_device_id(2).set_interrupt_pin(1);

  pciroot().routing_entries().push_back(
      pci_irq_routing_entry_t{.port_device_id = PCI_IRQ_ROUTING_NO_PARENT,
                              .port_function_id = PCI_IRQ_ROUTING_NO_PARENT,
                              .device_id = 0,
                              .pins = {1, 2, 3, 4}});

  auto bus = std::make_unique<TestBus>(fake_ddk::kFakeParent, pciroot().proto(), pciroot().info(),
                                       pciroot().ecam().CopyEcam());
  ASSERT_OK(bus->Initialize());
  ASSERT_EQ(bus->GetSharedIrqCount(), int_mod);
}

}  // namespace pci
