// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/pci/c/banjo.h>
#include <fuchsia/hardware/pciroot/cpp/banjo.h>
#include <lib/fake_ddk/fake_ddk.h>
#include <lib/mmio/mmio.h>
#include <lib/zx/bti.h>
#include <lib/zx/clock.h>
#include <lib/zx/status.h>
#include <lib/zx/time.h>
#include <zircon/errors.h>
#include <zircon/limits.h>
#include <zircon/syscalls/object.h>
#include <zircon/syscalls/port.h>

#include <memory>

#include <fbl/ref_ptr.h>
#include <zxtest/zxtest.h>

#include "src/devices/bus/drivers/pci/bus.h"
#include "src/devices/bus/drivers/pci/common.h"
#include "src/devices/bus/drivers/pci/test/fakes/fake_ecam.h"
#include "src/devices/bus/drivers/pci/test/fakes/fake_pciroot.h"
#include "src/devices/bus/drivers/pci/test/fakes/fake_upstream_node.h"
#include "src/devices/bus/drivers/pci/upstream_node.h"

namespace pci {

class PciBusTests : public zxtest::Test {
 public:
  PciBusTests() : pciroot_(0, 1) { bind_.SetProtocol(ZX_PROTOCOL_PCIROOT, pciroot_.proto()); }

 protected:
  // Sets up 5 devices, including two under a bridge.
  uint32_t SetupTopology() {
    uint8_t idx = 1;
    auto& ecam = pciroot_.ecam();
    ecam.get({0, 0, 0}).device.set_vendor_id(0x8086).set_device_id(idx++);
    ecam.get({0, 0, 1}).device.set_vendor_id(0x8086).set_device_id(idx++);
    ecam.get({0, 1, 0})
        .bridge.set_vendor_id(0x8086)
        .set_device_id(idx++)
        .set_header_type(PCI_HEADER_TYPE_BRIDGE)
        .set_io_base(0x10)
        .set_io_limit(0x0FFF)
        .set_memory_base(0x1000)
        .set_memory_limit(0xFFFFFFFF)
        .set_secondary_bus_number(1);
    ecam.get({1, 0, 0}).device.set_vendor_id(0x8086).set_device_id(idx++);
    ecam.get({1, 0, 1}).device.set_vendor_id(0x8086).set_device_id(idx);
    return idx;
  }

  zx::interrupt AddLegacyIrqToBus(uint8_t vector) {
    zx::interrupt interrupt;
    ZX_ASSERT(zx::interrupt::create(*zx::unowned_resource(ZX_HANDLE_INVALID), vector,
                                    ZX_INTERRUPT_VIRTUAL, &interrupt) == ZX_OK);
    pciroot_.legacy_irqs().push_back(
        pci_legacy_irq_t{.interrupt = interrupt.get(), .vector = vector});

    return interrupt;
  }

  void AddRoutingEntryToBus(std::optional<uint8_t> p_dev, std::optional<uint8_t> p_func,
                            uint8_t dev_id, uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
    pciroot_.routing_entries().push_back(pci_irq_routing_entry_t{
        .port_device_id = (p_dev) ? *p_dev : static_cast<uint8_t>(PCI_IRQ_ROUTING_NO_PARENT),
        .port_function_id = (p_func) ? *p_func : static_cast<uint8_t>(PCI_IRQ_ROUTING_NO_PARENT),
        .device_id = dev_id,
        .pins = {a, b, c, d}});
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
  // Add |int_cnt| interrupts, but make them share vectors based on |int_mod|. This ensures that
  // we handle duplicate IRQ entries properly.
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
  ASSERT_EQ(int_mod, bus->GetSharedIrqCount());
}

TEST_F(PciBusTests, LegacyIrqSignalTest) {
  // Establish the IRQ in the Pciroot implementation so that the bus will configure our device to
  // use it if the device id is 0x1 and it uses pin B.
  uint32_t vector = 0xA;
  zx::interrupt interrupt = AddLegacyIrqToBus(vector);
  AddRoutingEntryToBus(/*p_dev=*/std::nullopt, /*p_func=*/std::nullopt, /*dev_id=*/0, /*a=*/vector,
                       /*b=*/vector, /*c=*/0, /*d=*/0);
  // Have the routing table target device 0, pin B. This is configured in
  // SetupTopology for the device itself.
  SetupTopology();
  // These devices need interrupt pins mapped before Bus scans the topology.
  pciroot().ecam().get({0, 0, 0}).device.set_interrupt_pin(0x1);
  pciroot().ecam().get({0, 0, 1}).device.set_interrupt_pin(0x2);
  auto bus = std::make_unique<TestBus>(fake_ddk::kFakeParent, pciroot().proto(), pciroot().info(),
                                       pciroot().ecam().CopyEcam());
  ASSERT_OK(bus->Initialize());
  ASSERT_EQ(1, bus->GetSharedIrqCount());

  zx::interrupt dev_interrupt[2];
  // Configure both devices and map their driver facing interrupts. They have
  // different pins, but the pins are mapped to the same vector.
  for (uint8_t i = 0; i < 2; i++) {
    auto* bus_device = bus->GetDevice({0, 0, i});
    ASSERT_OK(bus_device->SetIrqMode(PCI_IRQ_MODE_LEGACY, 1));
    // Map the interrupt the same way a driver would.
    auto result = bus->GetDevice({0, 0, i})->MapInterrupt(0);
    ASSERT_TRUE(result.is_ok());
    dev_interrupt[i] = std::move(result.value());
  }

  // Bind device 00:00.0's interrupt to a port so we can "peek" at the interrupt
  // status via a port wait.
  zx::port port;
  ASSERT_OK(zx::port::create(ZX_PORT_BIND_TO_INTERRUPT, &port));
  ASSERT_OK(dev_interrupt[0].bind(port, 1, ZX_INTERRUPT_BIND));

  // Here we simulate triggering the hardware vector and track it all the way to
  // the interrupt event a downstream driver bound to this device would get.
  // Timestamps of the original vector must match.
  zx::time receive_time;
  zx::time trigger_time = zx::clock::get_monotonic();
  pciroot().ecam().get({0, 0, 1}).device.set_status(PCI_STATUS_INTERRUPT);
  ASSERT_OK(interrupt.trigger(0, trigger_time));

  // Only the device at 00:00.1 should trigger because 00:00.0 does not have the interrupt status
  // bit set in its config space. The interrupt time the driver receives must match the time the
  // interrupt dispatcher logged.
  ASSERT_OK(dev_interrupt[1].wait(&receive_time));
  ASSERT_EQ(trigger_time, receive_time);

  // If we handled the interrupt status check then there should be no packet on this port.
  zx_port_packet_t packet{};
  ASSERT_EQ(ZX_ERR_TIMED_OUT, port.wait(zx::deadline_after(zx::sec(0)), &packet));
}

TEST_F(PciBusTests, LegacyIrqNoAckTest) {
  // 00:00.0 is a valid device using legacy pin A.
  pci_bdf_t device = {0, 0, 0};
  pciroot()
      .ecam()
      .get(device)
      .device.set_vendor_id(0x8086)
      .set_device_id(0x8086)
      .set_interrupt_pin(0x1)
      .set_status(PCI_STATUS_INTERRUPT);
  // Route pin A to vector 16.
  uint8_t vector = 0x10;
  zx::interrupt bus_interrupt = AddLegacyIrqToBus(vector);
  AddRoutingEntryToBus(/*p_dev=*/std::nullopt, /*p_func=*/std::nullopt, /*dev_id=*/0, /*a=*/vector,
                       /*b=*/0, /*c=*/0, /*d=*/0);
  auto bus = std::make_unique<TestBus>(fake_ddk::kFakeParent, pciroot().proto(), pciroot().info(),
                                       pciroot().ecam().CopyEcam());
  ASSERT_OK(bus->Initialize());
  ASSERT_OK(bus->GetDevice(device)->SetIrqMode(PCI_IRQ_MODE_LEGACY_NOACK, 1));

  auto* bus_device = bus->GetDevice(device);
  // Quick method to check if the disabled flag is set for a legacy interrupt.
  auto check_disabled = [&bus_device]() {
    fbl::AutoLock _(bus_device->dev_lock());
    return bus_device->irqs().legacy_disabled;
  };

  // By tying the trigger/wait in the same thread we can avoid pitfalls with
  // racing the IRQ worker thread. When we send at least kMaxIrqsPerNoAckPeriod
  // IRQs the device's IRQ should be disabled.
  zx::port port;
  ASSERT_OK(zx::port::create(ZX_PORT_BIND_TO_INTERRUPT, &port));
  zx::interrupt dev_interrupt = bus_device->MapInterrupt(0).value();
  ASSERT_OK(dev_interrupt.bind(port, 1, ZX_INTERRUPT_BIND));
  ASSERT_FALSE(check_disabled());

  zx::time current_time = zx::clock::get_monotonic();
  uint32_t irq_cnt = 0;
  zx_port_packet_t packet;
  while (irq_cnt < kMaxIrqsPerNoAckPeriod) {
    ASSERT_OK(bus_interrupt.trigger(0, current_time));
    ASSERT_OK(port.wait(zx::time::infinite(), &packet));
    // Normally a driver would ack their interrupt object after a port wait so
    // we need to do it manually here.
    ASSERT_OK(dev_interrupt.ack());
    irq_cnt++;
  }
  ASSERT_TRUE(check_disabled());
}

}  // namespace pci
