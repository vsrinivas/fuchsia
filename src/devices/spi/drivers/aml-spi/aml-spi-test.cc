// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "aml-spi.h"

#include <endian.h>
#include <fuchsia/hardware/gpio/cpp/banjo-mock.h>
#include <fuchsia/hardware/platform/device/cpp/banjo.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/ddk/metadata.h>
#include <lib/fake-bti/bti.h>
#include <lib/fake_ddk/fake_ddk.h>
#include <lib/fzl/vmo-mapper.h>
#include <lib/zx/clock.h>
#include <lib/zx/vmo.h>

#include <memory>
#include <vector>

#include <fake-mmio-reg/fake-mmio-reg.h>
#include <zxtest/zxtest.h>

#include "registers.h"
#include "src/devices/bus/testing/fake-pdev/fake-pdev.h"
#include "src/devices/registers/testing/mock-registers/mock-registers.h"

namespace spi {

class FakeDdkSpi : public fake_ddk::Bind {
 public:
  struct ChildDevice {
    AmlSpi* device;
    void (*unbind_op)(void*);
  };

  static FakeDdkSpi* instance() { return static_cast<FakeDdkSpi*>(instance_); }

  explicit FakeDdkSpi(bool add_reset_fragment = true, bool add_interrupt = true)
      : loop_(&kAsyncLoopConfigNeverAttachToThread),
        registers_(loop_.dispatcher()),
        mmio_region_(mmio_registers_, sizeof(uint32_t),
                     sizeof(uint32_t) * std::size(mmio_registers_)) {
    fbl::Array<fake_ddk::FragmentEntry> fragments;
    if (add_reset_fragment) {
      fragments = fbl::Array(new fake_ddk::FragmentEntry[5], 5);
      fragments[4].name = "reset";
      fragments[4].protocols.emplace_back(fake_ddk::ProtocolEntry{
          ZX_PROTOCOL_REGISTERS, {.ops = registers_.proto()->ops, .ctx = registers_.proto()->ctx}});
    } else {
      fragments = fbl::Array(new fake_ddk::FragmentEntry[4], 4);
    }

    ASSERT_TRUE(fragments);
    fragments[0] = pdev_.fragment();
    fragments[1].name = "gpio-cs-2";
    fragments[1].protocols.emplace_back(
        fake_ddk::ProtocolEntry{ZX_PROTOCOL_GPIO, {gpio_.GetProto()->ops, &gpio_}});
    fragments[2].name = "gpio-cs-3";
    fragments[2].protocols.emplace_back(
        fake_ddk::ProtocolEntry{ZX_PROTOCOL_GPIO, {gpio_.GetProto()->ops, &gpio_}});
    fragments[3].name = "gpio-cs-5";
    fragments[3].protocols.emplace_back(
        fake_ddk::ProtocolEntry{ZX_PROTOCOL_GPIO, {gpio_.GetProto()->ops, &gpio_}});

    SetFragments(std::move(fragments));

    SetMetadata(DEVICE_METADATA_AMLSPI_CONFIG, kSpiConfig, sizeof(kSpiConfig));

    ASSERT_OK(
        mmio_mapper_.CreateAndMap(0x100, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, nullptr, &mmio_));

    pdev_.set_device_info(pdev_device_info_t{
        .mmio_count = 1,
        .irq_count = add_interrupt ? 1u : 0u,
    });

    zx::vmo dup;
    ASSERT_OK(mmio_.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup));

    pdev_.set_mmio(0, {
                          .vmo = std::move(dup),
                          .offset = 0,
                          .size = mmio_mapper_.size(),
                      });

    EXPECT_OK(loop_.StartThread("aml-spi-test-registers-thread"));
    registers_.fidl_service()->ExpectWrite<uint32_t>(0x1c, 1 << 1, 1 << 1);

    if (add_interrupt) {
      ASSERT_OK(zx::interrupt::create({}, 0, ZX_INTERRUPT_VIRTUAL, &interrupt_));
      zx::interrupt dut_interrupt;

      ASSERT_OK(interrupt_.duplicate(ZX_RIGHT_SAME_RIGHTS, &dut_interrupt));
      pdev_.set_interrupt(0, std::move(dut_interrupt));

      interrupt_.trigger(0, zx::clock::get_monotonic());
    }

    // Set the transfer complete bit so the driver doesn't get stuck waiting on the interrupt.
    mmio_region_[AML_SPI_STATREG].SetReadCallback(
        []() { return StatReg::Get().FromValue(0).set_tc(1).set_te(1).set_rr(1).reg_value(); });
  }

  ~FakeDdkSpi() override {
    // Call DdkRelease on any children that haven't been removed yet.
    for (ChildDevice& child : children_) {
      child.device->DdkRelease();
    }
  }

  const std::vector<ChildDevice>& children() { return children_; }
  ddk::MockGpio& gpio() { return gpio_; }
  ddk_fake::FakeMmioRegRegion& mmio() { return mmio_region_; }
  fake_pdev::FakePDev& pdev() { return pdev_; }

  zx_status_t DeviceAdd(zx_driver_t* drv, zx_device_t* parent, device_add_args_t* args,
                        zx_device_t** out) override {
    if (parent != fake_ddk::kFakeParent) {
      return ZX_ERR_BAD_STATE;
    }

    children_.push_back(ChildDevice{
        .device = reinterpret_cast<AmlSpi*>(args->ctx),
        .unbind_op = args->ops->unbind,
    });

    if (children_.size() == std::size(kSpiConfig)) {
      add_called_ = true;
    }

    *out = reinterpret_cast<zx_device_t*>(args->ctx);
    return ZX_OK;
  }

  zx_status_t DeviceRemove(zx_device_t* device) override {
    auto* const spi_device = reinterpret_cast<AmlSpi*>(device);
    for (auto it = children_.begin(); it != children_.end(); it++) {
      if (it->device == spi_device) {
        children_.erase(it);
        spi_device->DdkRelease();
        remove_called_ = children_.empty();
        return ZX_OK;
      }
    }

    bad_device_ = true;
    return ZX_ERR_NOT_FOUND;
  }

  void DeviceAsyncRemove(zx_device_t* device) override {
    auto* const spi_device = reinterpret_cast<AmlSpi*>(device);
    for (auto it = children_.begin(); it != children_.end(); it++) {
      if (it->device == spi_device) {
        if (it->unbind_op) {
          it->unbind_op(spi_device);
        } else {
          DeviceRemove(device);
        }
        return;
      }
    }

    bad_device_ = true;
  }

  zx_status_t DeviceAddMetadata(zx_device_t* device, uint32_t type, const void* data,
                                size_t length) override {
    auto* const spi_device = reinterpret_cast<AmlSpi*>(device);
    for (auto it = children_.begin(); it != children_.end(); it++) {
      if (it->device == spi_device) {
        // Pass through to the parent class but with device set to a value it expects.
        return fake_ddk::Bind::DeviceAddMetadata(fake_ddk::kFakeDevice, type, data, length);
      }
    }

    bad_device_ = true;
    return ZX_ERR_NOT_FOUND;
  }

  bool ControllerReset() {
    zx_status_t status = registers_.fidl_service()->VerifyAll();
    if (status == ZX_OK) {
      // Always keep a single expectation in the queue, that way we can verify when the controller
      // is not reset.
      registers_.fidl_service()->ExpectWrite<uint32_t>(0x1c, 1 << 1, 1 << 1);
    }
    return status == ZX_OK;
  }

 private:
  static constexpr amlogic_spi::amlspi_config_t kSpiConfig[] = {
      {
          .bus_id = 0,
          .cs_count = 3,
          .cs = {5, 3, amlogic_spi::amlspi_config_t::kCsClientManaged},
          .clock_divider_register_value = 0,
          .use_enhanced_clock_mode = false,
      },
  };

  async::Loop loop_;
  mock_registers::MockRegistersDevice registers_;
  std::vector<ChildDevice> children_;
  zx::vmo mmio_;
  fzl::VmoMapper mmio_mapper_;
  ddk::MockGpio gpio_;
  fake_pdev::FakePDev pdev_;
  zx::interrupt interrupt_;
  ddk_fake::FakeMmioReg mmio_registers_[17];
  ddk_fake::FakeMmioRegRegion mmio_region_;
};

}  // namespace spi

namespace ddk {

// Override MmioBuffer creation to avoid having to map with ZX_CACHE_POLICY_UNCACHED_DEVICE.
zx_status_t PDevMakeMmioBufferWeak(const pdev_mmio_t& pdev_mmio, std::optional<MmioBuffer>* mmio,
                                   uint32_t cache_policy) {
  spi::FakeDdkSpi* const instance = spi::FakeDdkSpi::instance();
  if (!instance) {
    return ZX_ERR_BAD_STATE;
  }

  mmio->emplace(instance->mmio().GetMmioBuffer());
  return ZX_OK;
}

}  // namespace ddk

namespace spi {

zx_koid_t GetVmoKoid(const zx::vmo& vmo) {
  zx_info_handle_basic_t info = {};
  size_t actual = 0;
  size_t available = 0;
  zx_status_t status = vmo.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), &actual, &available);
  if (status != ZX_OK || actual < 1) {
    return ZX_KOID_INVALID;
  }
  return info.koid;
}

TEST(AmlSpiTest, DdkLifecycle) {
  FakeDdkSpi bind;

  EXPECT_OK(AmlSpi::Create(nullptr, fake_ddk::kFakeParent));

  ASSERT_EQ(bind.children().size(), 1);
  device_async_remove(reinterpret_cast<zx_device_t*>(bind.children()[0].device));

  EXPECT_TRUE(bind.Ok());
}

TEST(AmlSpiTest, ChipSelectCount) {
  FakeDdkSpi bind;

  EXPECT_OK(AmlSpi::Create(nullptr, fake_ddk::kFakeParent));

  ASSERT_EQ(bind.children().size(), 1);
  AmlSpi& spi0 = *bind.children()[0].device;

  EXPECT_EQ(spi0.SpiImplGetChipSelectCount(), 3);
}

TEST(AmlSpiTest, Exchange) {
  constexpr uint8_t kTxData[] = {0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12};
  constexpr uint8_t kExpectedRxData[] = {0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab};

  FakeDdkSpi bind;

  EXPECT_OK(AmlSpi::Create(nullptr, fake_ddk::kFakeParent));

  ASSERT_EQ(bind.children().size(), 1);
  AmlSpi& spi0 = *bind.children()[0].device;

  bind.mmio()[AML_SPI_RXDATA].SetReadCallback([]() { return kExpectedRxData[0]; });

  uint64_t tx_data = 0;
  bind.mmio()[AML_SPI_TXDATA].SetWriteCallback([&tx_data](uint64_t value) { tx_data = value; });

  bind.gpio().ExpectWrite(ZX_OK, 0).ExpectWrite(ZX_OK, 1);

  uint8_t rxbuf[sizeof(kTxData)] = {};
  size_t rx_actual;
  EXPECT_OK(spi0.SpiImplExchange(0, kTxData, sizeof(kTxData), rxbuf, sizeof(rxbuf), &rx_actual));

  EXPECT_EQ(rx_actual, sizeof(rxbuf));
  EXPECT_BYTES_EQ(rxbuf, kExpectedRxData, rx_actual);
  EXPECT_EQ(tx_data, kTxData[0]);

  EXPECT_FALSE(bind.ControllerReset());

  ASSERT_NO_FATAL_FAILURE(bind.gpio().VerifyAndClear());
}

TEST(AmlSpiTest, ExchangeCsManagedByClient) {
  constexpr uint8_t kTxData[] = {0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12};
  constexpr uint8_t kExpectedRxData[] = {0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab};

  FakeDdkSpi bind;

  EXPECT_OK(AmlSpi::Create(nullptr, fake_ddk::kFakeParent));

  ASSERT_EQ(bind.children().size(), 1);
  AmlSpi& spi0 = *bind.children()[0].device;

  bind.mmio()[AML_SPI_RXDATA].SetReadCallback([]() { return kExpectedRxData[0]; });

  uint64_t tx_data = 0;
  bind.mmio()[AML_SPI_TXDATA].SetWriteCallback([&tx_data](uint64_t value) { tx_data = value; });

  uint8_t rxbuf[sizeof(kTxData)] = {};
  size_t rx_actual;
  EXPECT_OK(spi0.SpiImplExchange(2, kTxData, sizeof(kTxData), rxbuf, sizeof(rxbuf), &rx_actual));

  EXPECT_EQ(rx_actual, sizeof(rxbuf));
  EXPECT_BYTES_EQ(rxbuf, kExpectedRxData, rx_actual);
  EXPECT_EQ(tx_data, kTxData[0]);

  EXPECT_FALSE(bind.ControllerReset());

  // There should be no GPIO calls as the client manages CS for this device.
  ASSERT_NO_FATAL_FAILURE(bind.gpio().VerifyAndClear());
}

TEST(AmlSpiTest, RegisterVmo) {
  FakeDdkSpi bind;

  EXPECT_OK(AmlSpi::Create(nullptr, fake_ddk::kFakeParent));

  ASSERT_EQ(bind.children().size(), 1);
  AmlSpi& spi1 = *bind.children()[0].device;

  zx::vmo test_vmo;
  EXPECT_OK(zx::vmo::create(PAGE_SIZE, 0, &test_vmo));

  const zx_koid_t test_vmo_koid = GetVmoKoid(test_vmo);

  {
    zx::vmo vmo;
    EXPECT_OK(test_vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &vmo));
    EXPECT_OK(spi1.SpiImplRegisterVmo(0, 1, std::move(vmo), 0, PAGE_SIZE, SPI_VMO_RIGHT_READ));
  }

  {
    zx::vmo vmo;
    EXPECT_OK(test_vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &vmo));
    EXPECT_NOT_OK(spi1.SpiImplRegisterVmo(0, 1, std::move(vmo), 0, PAGE_SIZE, SPI_VMO_RIGHT_READ));
  }

  {
    zx::vmo vmo;
    EXPECT_OK(spi1.SpiImplUnregisterVmo(0, 1, &vmo));
    EXPECT_EQ(test_vmo_koid, GetVmoKoid(vmo));
  }

  {
    zx::vmo vmo;
    EXPECT_NOT_OK(spi1.SpiImplUnregisterVmo(0, 1, &vmo));
  }
}

TEST(AmlSpiTest, Transmit) {
  constexpr uint8_t kTxData[] = {0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5};

  FakeDdkSpi bind;

  EXPECT_OK(AmlSpi::Create(nullptr, fake_ddk::kFakeParent));

  ASSERT_EQ(bind.children().size(), 1);
  AmlSpi& spi1 = *bind.children()[0].device;

  zx::vmo test_vmo;
  EXPECT_OK(zx::vmo::create(PAGE_SIZE, 0, &test_vmo));

  {
    zx::vmo vmo;
    EXPECT_OK(test_vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &vmo));
    EXPECT_OK(
        spi1.SpiImplRegisterVmo(0, 1, std::move(vmo), 256, PAGE_SIZE - 256, SPI_VMO_RIGHT_READ));
  }

  bind.gpio().ExpectWrite(ZX_OK, 0).ExpectWrite(ZX_OK, 1);

  EXPECT_OK(test_vmo.write(kTxData, 512, sizeof(kTxData)));

  uint64_t tx_data = 0;
  bind.mmio()[AML_SPI_TXDATA].SetWriteCallback([&tx_data](uint64_t value) { tx_data = value; });

  EXPECT_OK(spi1.SpiImplTransmitVmo(0, 1, 256, sizeof(kTxData)));

  EXPECT_EQ(tx_data, kTxData[0]);

  EXPECT_FALSE(bind.ControllerReset());

  ASSERT_NO_FATAL_FAILURE(bind.gpio().VerifyAndClear());
}

TEST(AmlSpiTest, ReceiveVmo) {
  constexpr uint8_t kExpectedRxData[] = {0x78, 0x78, 0x78, 0x78, 0x78, 0x78, 0x78};

  FakeDdkSpi bind;

  EXPECT_OK(AmlSpi::Create(nullptr, fake_ddk::kFakeParent));

  ASSERT_EQ(bind.children().size(), 1);
  AmlSpi& spi1 = *bind.children()[0].device;

  zx::vmo test_vmo;
  EXPECT_OK(zx::vmo::create(PAGE_SIZE, 0, &test_vmo));

  {
    zx::vmo vmo;
    EXPECT_OK(test_vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &vmo));
    EXPECT_OK(spi1.SpiImplRegisterVmo(0, 1, std::move(vmo), 256, PAGE_SIZE - 256,
                                      SPI_VMO_RIGHT_READ | SPI_VMO_RIGHT_WRITE));
  }

  bind.mmio()[AML_SPI_RXDATA].SetReadCallback([]() { return kExpectedRxData[0]; });

  bind.gpio().ExpectWrite(ZX_OK, 0).ExpectWrite(ZX_OK, 1);

  EXPECT_OK(spi1.SpiImplReceiveVmo(0, 1, 512, sizeof(kExpectedRxData)));

  uint8_t rx_buffer[sizeof(kExpectedRxData)];
  EXPECT_OK(test_vmo.read(rx_buffer, 768, sizeof(rx_buffer)));
  EXPECT_BYTES_EQ(rx_buffer, kExpectedRxData, sizeof(rx_buffer));

  EXPECT_FALSE(bind.ControllerReset());

  ASSERT_NO_FATAL_FAILURE(bind.gpio().VerifyAndClear());
}

TEST(AmlSpiTest, ExchangeVmo) {
  constexpr uint8_t kTxData[] = {0xef, 0xef, 0xef, 0xef, 0xef, 0xef, 0xef};
  constexpr uint8_t kExpectedRxData[] = {0x78, 0x78, 0x78, 0x78, 0x78, 0x78, 0x78};

  FakeDdkSpi bind;

  EXPECT_OK(AmlSpi::Create(nullptr, fake_ddk::kFakeParent));

  ASSERT_EQ(bind.children().size(), 1);
  AmlSpi& spi1 = *bind.children()[0].device;

  zx::vmo test_vmo;
  EXPECT_OK(zx::vmo::create(PAGE_SIZE, 0, &test_vmo));

  {
    zx::vmo vmo;
    EXPECT_OK(test_vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &vmo));
    EXPECT_OK(spi1.SpiImplRegisterVmo(0, 1, std::move(vmo), 256, PAGE_SIZE - 256,
                                      SPI_VMO_RIGHT_READ | SPI_VMO_RIGHT_WRITE));
  }

  bind.mmio()[AML_SPI_RXDATA].SetReadCallback([]() { return kExpectedRxData[0]; });

  uint64_t tx_data = 0;
  bind.mmio()[AML_SPI_TXDATA].SetWriteCallback([&tx_data](uint64_t value) { tx_data = value; });

  bind.gpio().ExpectWrite(ZX_OK, 0).ExpectWrite(ZX_OK, 1);

  EXPECT_OK(test_vmo.write(kTxData, 512, sizeof(kTxData)));

  EXPECT_OK(spi1.SpiImplExchangeVmo(0, 1, 256, 1, 512, sizeof(kTxData)));

  uint8_t rx_buffer[sizeof(kExpectedRxData)];
  EXPECT_OK(test_vmo.read(rx_buffer, 768, sizeof(rx_buffer)));
  EXPECT_BYTES_EQ(rx_buffer, kExpectedRxData, sizeof(rx_buffer));

  EXPECT_EQ(tx_data, kTxData[0]);

  EXPECT_FALSE(bind.ControllerReset());

  ASSERT_NO_FATAL_FAILURE(bind.gpio().VerifyAndClear());
}

TEST(AmlSpiTest, TransfersOutOfRange) {
  FakeDdkSpi bind;

  EXPECT_OK(AmlSpi::Create(nullptr, fake_ddk::kFakeParent));

  ASSERT_EQ(bind.children().size(), 1);
  AmlSpi& spi0 = *bind.children()[0].device;

  zx::vmo test_vmo;
  EXPECT_OK(zx::vmo::create(PAGE_SIZE, 0, &test_vmo));

  {
    zx::vmo vmo;
    EXPECT_OK(test_vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &vmo));
    EXPECT_OK(spi0.SpiImplRegisterVmo(1, 1, std::move(vmo), PAGE_SIZE - 4, 4,
                                      SPI_VMO_RIGHT_READ | SPI_VMO_RIGHT_WRITE));
  }

  bind.gpio().ExpectWrite(ZX_OK, 0).ExpectWrite(ZX_OK, 1);

  EXPECT_OK(spi0.SpiImplExchangeVmo(1, 1, 0, 1, 2, 2));
  EXPECT_NOT_OK(spi0.SpiImplExchangeVmo(1, 1, 0, 1, 3, 2));
  EXPECT_NOT_OK(spi0.SpiImplExchangeVmo(1, 1, 3, 1, 0, 2));
  EXPECT_NOT_OK(spi0.SpiImplExchangeVmo(1, 1, 0, 1, 2, 3));

  bind.gpio().ExpectWrite(ZX_OK, 0).ExpectWrite(ZX_OK, 1);

  EXPECT_OK(spi0.SpiImplTransmitVmo(1, 1, 0, 4));
  EXPECT_NOT_OK(spi0.SpiImplTransmitVmo(1, 1, 0, 5));
  EXPECT_NOT_OK(spi0.SpiImplTransmitVmo(1, 1, 3, 2));
  EXPECT_NOT_OK(spi0.SpiImplTransmitVmo(1, 1, 4, 1));
  EXPECT_NOT_OK(spi0.SpiImplTransmitVmo(1, 1, 5, 1));

  bind.gpio().ExpectWrite(ZX_OK, 0).ExpectWrite(ZX_OK, 1);
  EXPECT_OK(spi0.SpiImplReceiveVmo(1, 1, 0, 4));

  bind.gpio().ExpectWrite(ZX_OK, 0).ExpectWrite(ZX_OK, 1);
  EXPECT_OK(spi0.SpiImplReceiveVmo(1, 1, 3, 1));

  EXPECT_NOT_OK(spi0.SpiImplReceiveVmo(1, 1, 3, 2));
  EXPECT_NOT_OK(spi0.SpiImplReceiveVmo(1, 1, 4, 1));
  EXPECT_NOT_OK(spi0.SpiImplReceiveVmo(1, 1, 5, 1));

  ASSERT_NO_FATAL_FAILURE(bind.gpio().VerifyAndClear());
}

TEST(AmlSpiTest, VmoBadRights) {
  FakeDdkSpi bind;

  EXPECT_OK(AmlSpi::Create(nullptr, fake_ddk::kFakeParent));

  ASSERT_EQ(bind.children().size(), 1);
  AmlSpi& spi1 = *bind.children()[0].device;

  zx::vmo test_vmo;
  EXPECT_OK(zx::vmo::create(PAGE_SIZE, 0, &test_vmo));

  {
    zx::vmo vmo;
    EXPECT_OK(test_vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &vmo));
    EXPECT_OK(spi1.SpiImplRegisterVmo(0, 1, std::move(vmo), 0, 256, SPI_VMO_RIGHT_READ));
  }

  {
    zx::vmo vmo;
    EXPECT_OK(test_vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &vmo));
    EXPECT_OK(spi1.SpiImplRegisterVmo(0, 2, std::move(vmo), 0, 256,
                                      SPI_VMO_RIGHT_READ | SPI_VMO_RIGHT_WRITE));
  }

  bind.gpio().ExpectWrite(ZX_OK, 0).ExpectWrite(ZX_OK, 1);

  EXPECT_OK(spi1.SpiImplExchangeVmo(0, 1, 0, 2, 128, 128));
  EXPECT_EQ(spi1.SpiImplExchangeVmo(0, 2, 0, 1, 128, 128), ZX_ERR_ACCESS_DENIED);
  EXPECT_EQ(spi1.SpiImplExchangeVmo(0, 1, 0, 1, 128, 128), ZX_ERR_ACCESS_DENIED);
  EXPECT_EQ(spi1.SpiImplReceiveVmo(0, 1, 0, 128), ZX_ERR_ACCESS_DENIED);

  ASSERT_NO_FATAL_FAILURE(bind.gpio().VerifyAndClear());
}

TEST(AmlSpiTest, Exchange64BitWords) {
  constexpr uint8_t kTxData[] = {
      0x3c, 0xa7, 0x5f, 0xc8, 0x4b, 0x0b, 0xdf, 0xef, 0xb9, 0xa0, 0xcb, 0xbd,
      0xd4, 0xcf, 0xa8, 0xbf, 0x85, 0xf2, 0x6a, 0xe3, 0xba, 0xf1, 0x49, 0x00,
  };
  constexpr uint8_t kExpectedRxData[] = {
      0xea, 0x2b, 0x8f, 0x8f, 0xea, 0x2b, 0x8f, 0x8f, 0xea, 0x2b, 0x8f, 0x8f,
      0xea, 0x2b, 0x8f, 0x8f, 0xea, 0x2b, 0x8f, 0x8f, 0xea, 0x2b, 0x8f, 0x8f,
  };

  FakeDdkSpi bind;

  EXPECT_OK(AmlSpi::Create(nullptr, fake_ddk::kFakeParent));

  ASSERT_EQ(bind.children().size(), 1);
  AmlSpi& spi0 = *bind.children()[0].device;

  // First (and only) word of kExpectedRxData with bytes swapped.
  bind.mmio()[AML_SPI_RXDATA].SetReadCallback([]() { return 0xea2b'8f8f; });

  uint64_t tx_data = 0;
  bind.mmio()[AML_SPI_TXDATA].SetWriteCallback([&tx_data](uint64_t value) { tx_data = value; });

  bind.gpio().ExpectWrite(ZX_OK, 0).ExpectWrite(ZX_OK, 1);

  uint8_t rxbuf[sizeof(kTxData)] = {};
  size_t rx_actual;
  EXPECT_OK(spi0.SpiImplExchange(0, kTxData, sizeof(kTxData), rxbuf, sizeof(rxbuf), &rx_actual));

  EXPECT_EQ(rx_actual, sizeof(rxbuf));
  EXPECT_BYTES_EQ(rxbuf, kExpectedRxData, rx_actual);
  // Last word of kTxData with bytes swapped.
  EXPECT_EQ(tx_data, 0xbaf1'4900);

  EXPECT_FALSE(bind.ControllerReset());

  ASSERT_NO_FATAL_FAILURE(bind.gpio().VerifyAndClear());
}

TEST(AmlSpiTest, Exchange64Then8BitWords) {
  constexpr uint8_t kTxData[] = {
      0x3c, 0xa7, 0x5f, 0xc8, 0x4b, 0x0b, 0xdf, 0xef, 0xb9, 0xa0, 0xcb,
      0xbd, 0xd4, 0xcf, 0xa8, 0xbf, 0x85, 0xf2, 0x6a, 0xe3, 0xba,
  };
  constexpr uint8_t kExpectedRxData[] = {
      0x00, 0x00, 0x00, 0xea, 0x00, 0x00, 0x00, 0xea, 0x00, 0x00, 0x00,
      0xea, 0x00, 0x00, 0x00, 0xea, 0xea, 0xea, 0xea, 0xea, 0xea,
  };

  FakeDdkSpi bind;

  EXPECT_OK(AmlSpi::Create(nullptr, fake_ddk::kFakeParent));

  ASSERT_EQ(bind.children().size(), 1);
  AmlSpi& spi0 = *bind.children()[0].device;

  bind.mmio()[AML_SPI_RXDATA].SetReadCallback([]() { return 0xea; });

  uint64_t tx_data = 0;
  bind.mmio()[AML_SPI_TXDATA].SetWriteCallback([&tx_data](uint64_t value) { tx_data = value; });

  bind.gpio().ExpectWrite(ZX_OK, 0).ExpectWrite(ZX_OK, 1);

  uint8_t rxbuf[sizeof(kTxData)] = {};
  size_t rx_actual;
  EXPECT_OK(spi0.SpiImplExchange(0, kTxData, sizeof(kTxData), rxbuf, sizeof(rxbuf), &rx_actual));

  EXPECT_EQ(rx_actual, sizeof(rxbuf));
  EXPECT_BYTES_EQ(rxbuf, kExpectedRxData, rx_actual);
  EXPECT_EQ(tx_data, 0xba);

  EXPECT_FALSE(bind.ControllerReset());

  ASSERT_NO_FATAL_FAILURE(bind.gpio().VerifyAndClear());
}

TEST(AmlSpiTest, ExchangeResetsController) {
  FakeDdkSpi bind;

  EXPECT_OK(AmlSpi::Create(nullptr, fake_ddk::kFakeParent));

  ASSERT_EQ(bind.children().size(), 1);
  AmlSpi& spi0 = *bind.children()[0].device;

  bind.gpio().ExpectWrite(ZX_OK, 0).ExpectWrite(ZX_OK, 1);

  uint8_t buf[17] = {};
  size_t rx_actual;
  EXPECT_OK(spi0.SpiImplExchange(0, buf, 17, buf, 17, &rx_actual));
  EXPECT_EQ(rx_actual, 17);
  EXPECT_FALSE(bind.ControllerReset());

  bind.gpio().ExpectWrite(ZX_OK, 0).ExpectWrite(ZX_OK, 1);

  // Controller should be reset because a 64-bit transfer was preceded by a transfer of an odd
  // number of bytes.
  EXPECT_OK(spi0.SpiImplExchange(0, buf, 16, buf, 16, &rx_actual));
  EXPECT_EQ(rx_actual, 16);
  EXPECT_TRUE(bind.ControllerReset());

  bind.gpio().ExpectWrite(ZX_OK, 0).ExpectWrite(ZX_OK, 1);

  EXPECT_OK(spi0.SpiImplExchange(0, buf, 3, buf, 3, &rx_actual));
  EXPECT_EQ(rx_actual, 3);
  EXPECT_FALSE(bind.ControllerReset());

  bind.gpio().ExpectWrite(ZX_OK, 0).ExpectWrite(ZX_OK, 1);

  EXPECT_OK(spi0.SpiImplExchange(0, buf, 6, buf, 6, &rx_actual));
  EXPECT_EQ(rx_actual, 6);
  EXPECT_FALSE(bind.ControllerReset());

  bind.gpio().ExpectWrite(ZX_OK, 0).ExpectWrite(ZX_OK, 1);

  EXPECT_OK(spi0.SpiImplExchange(0, buf, 8, buf, 8, &rx_actual));
  EXPECT_EQ(rx_actual, 8);
  EXPECT_TRUE(bind.ControllerReset());

  ASSERT_NO_FATAL_FAILURE(bind.gpio().VerifyAndClear());
}

TEST(AmlSpiTest, ExchangeWithNoResetFragment) {
  FakeDdkSpi bind(false);

  EXPECT_OK(AmlSpi::Create(nullptr, fake_ddk::kFakeParent));

  ASSERT_EQ(bind.children().size(), 1);
  AmlSpi& spi0 = *bind.children()[0].device;

  bind.gpio().ExpectWrite(ZX_OK, 0).ExpectWrite(ZX_OK, 1);

  uint8_t buf[17] = {};
  size_t rx_actual;
  EXPECT_OK(spi0.SpiImplExchange(0, buf, 17, buf, 17, &rx_actual));
  EXPECT_EQ(rx_actual, 17);
  EXPECT_FALSE(bind.ControllerReset());

  bind.gpio().ExpectWrite(ZX_OK, 0).ExpectWrite(ZX_OK, 1);

  // Controller should not be reset because no reset fragment was provided.
  EXPECT_OK(spi0.SpiImplExchange(0, buf, 16, buf, 16, &rx_actual));
  EXPECT_EQ(rx_actual, 16);
  EXPECT_FALSE(bind.ControllerReset());

  bind.gpio().ExpectWrite(ZX_OK, 0).ExpectWrite(ZX_OK, 1);

  EXPECT_OK(spi0.SpiImplExchange(0, buf, 3, buf, 3, &rx_actual));
  EXPECT_EQ(rx_actual, 3);
  EXPECT_FALSE(bind.ControllerReset());

  bind.gpio().ExpectWrite(ZX_OK, 0).ExpectWrite(ZX_OK, 1);

  EXPECT_OK(spi0.SpiImplExchange(0, buf, 6, buf, 6, &rx_actual));
  EXPECT_EQ(rx_actual, 6);
  EXPECT_FALSE(bind.ControllerReset());

  bind.gpio().ExpectWrite(ZX_OK, 0).ExpectWrite(ZX_OK, 1);

  EXPECT_OK(spi0.SpiImplExchange(0, buf, 8, buf, 8, &rx_actual));
  EXPECT_EQ(rx_actual, 8);
  EXPECT_FALSE(bind.ControllerReset());

  ASSERT_NO_FATAL_FAILURE(bind.gpio().VerifyAndClear());
}

TEST(AmlSpiTest, ReleaseVmos) {
  FakeDdkSpi bind;

  EXPECT_OK(AmlSpi::Create(nullptr, fake_ddk::kFakeParent));

  ASSERT_EQ(bind.children().size(), 1);
  AmlSpi& spi1 = *bind.children()[0].device;

  {
    zx::vmo vmo;
    EXPECT_OK(zx::vmo::create(PAGE_SIZE, 0, &vmo));
    EXPECT_OK(spi1.SpiImplRegisterVmo(0, 1, std::move(vmo), 0, PAGE_SIZE, SPI_VMO_RIGHT_READ));

    EXPECT_OK(zx::vmo::create(PAGE_SIZE, 0, &vmo));
    EXPECT_OK(spi1.SpiImplRegisterVmo(0, 2, std::move(vmo), 0, PAGE_SIZE, SPI_VMO_RIGHT_READ));
  }

  {
    zx::vmo vmo;
    EXPECT_OK(spi1.SpiImplUnregisterVmo(0, 2, &vmo));
  }

  // Release VMO 1 and make sure that a subsequent call to unregister it fails.
  spi1.SpiImplReleaseRegisteredVmos(0);

  {
    zx::vmo vmo;
    EXPECT_NOT_OK(spi1.SpiImplUnregisterVmo(0, 1, &vmo));
  }

  {
    zx::vmo vmo;
    EXPECT_OK(zx::vmo::create(PAGE_SIZE, 0, &vmo));
    EXPECT_OK(spi1.SpiImplRegisterVmo(0, 1, std::move(vmo), 0, PAGE_SIZE, SPI_VMO_RIGHT_READ));

    EXPECT_OK(zx::vmo::create(PAGE_SIZE, 0, &vmo));
    EXPECT_OK(spi1.SpiImplRegisterVmo(0, 2, std::move(vmo), 0, PAGE_SIZE, SPI_VMO_RIGHT_READ));
  }

  // Release both VMOs and make sure that they can be registered again.
  spi1.SpiImplReleaseRegisteredVmos(0);

  {
    zx::vmo vmo;
    EXPECT_OK(zx::vmo::create(PAGE_SIZE, 0, &vmo));
    EXPECT_OK(spi1.SpiImplRegisterVmo(0, 1, std::move(vmo), 0, PAGE_SIZE, SPI_VMO_RIGHT_READ));

    EXPECT_OK(zx::vmo::create(PAGE_SIZE, 0, &vmo));
    EXPECT_OK(spi1.SpiImplRegisterVmo(0, 2, std::move(vmo), 0, PAGE_SIZE, SPI_VMO_RIGHT_READ));
  }
}

TEST(AmlSpiTest, NormalClockMode) {
  constexpr amlogic_spi::amlspi_config_t kTestSpiConfig[] = {
      {
          .bus_id = 0,
          .cs_count = 2,
          .cs = {5, 3},
          .clock_divider_register_value = 0x5,
          .use_enhanced_clock_mode = false,
      },
  };

  // Must outlive bind.
  auto conreg = ConReg::Get().FromValue(0);
  auto enhanced_cntl = EnhanceCntl::Get().FromValue(0);
  auto testreg = TestReg::Get().FromValue(0);

  FakeDdkSpi bind;
  bind.SetMetadata(DEVICE_METADATA_AMLSPI_CONFIG, kTestSpiConfig, sizeof(kTestSpiConfig));

  bind.mmio()[AML_SPI_CONREG].SetWriteCallback(
      [&conreg](uint32_t value) { conreg.set_reg_value(value); });

  bind.mmio()[AML_SPI_CONREG].SetReadCallback([&conreg]() { return conreg.reg_value(); });

  bind.mmio()[AML_SPI_ENHANCE_CNTL].SetWriteCallback(
      [&enhanced_cntl](uint32_t value) { enhanced_cntl.set_reg_value(value); });

  bind.mmio()[AML_SPI_TESTREG].SetWriteCallback(
      [&testreg](uint32_t value) { testreg.set_reg_value(value); });

  EXPECT_OK(AmlSpi::Create(nullptr, fake_ddk::kFakeParent));

  EXPECT_EQ(conreg.data_rate(), 0x5);
  EXPECT_EQ(conreg.drctl(), 0);
  EXPECT_EQ(conreg.ssctl(), 0);
  EXPECT_EQ(conreg.smc(), 0);
  EXPECT_EQ(conreg.xch(), 0);
  EXPECT_EQ(conreg.mode(), ConReg::kModeMaster);
  EXPECT_EQ(conreg.en(), 1);

  EXPECT_EQ(enhanced_cntl.reg_value(), 0);

  EXPECT_EQ(testreg.dlyctl(), 0x15);
  EXPECT_EQ(testreg.clk_free_en(), 1);
}

TEST(AmlSpiTest, EnhancedClockMode) {
  constexpr amlogic_spi::amlspi_config_t kTestSpiConfig[] = {
      {
          .bus_id = 0,
          .cs_count = 2,
          .cs = {5, 3},
          .clock_divider_register_value = 0xa5,
          .use_enhanced_clock_mode = true,
          .delay_control = 0b00'11'00,
      },
  };

  // Must outlive bind.
  auto conreg = ConReg::Get().FromValue(0);
  auto enhanced_cntl = EnhanceCntl::Get().FromValue(0);
  auto testreg = TestReg::Get().FromValue(0);

  FakeDdkSpi bind;
  bind.SetMetadata(DEVICE_METADATA_AMLSPI_CONFIG, kTestSpiConfig, sizeof(kTestSpiConfig));

  bind.mmio()[AML_SPI_CONREG].SetWriteCallback(
      [&conreg](uint32_t value) { conreg.set_reg_value(value); });

  bind.mmio()[AML_SPI_CONREG].SetReadCallback([&conreg]() { return conreg.reg_value(); });

  bind.mmio()[AML_SPI_ENHANCE_CNTL].SetWriteCallback(
      [&enhanced_cntl](uint32_t value) { enhanced_cntl.set_reg_value(value); });

  bind.mmio()[AML_SPI_TESTREG].SetWriteCallback(
      [&testreg](uint32_t value) { testreg.set_reg_value(value); });

  EXPECT_OK(AmlSpi::Create(nullptr, fake_ddk::kFakeParent));

  EXPECT_EQ(conreg.data_rate(), 0);
  EXPECT_EQ(conreg.drctl(), 0);
  EXPECT_EQ(conreg.ssctl(), 0);
  EXPECT_EQ(conreg.smc(), 0);
  EXPECT_EQ(conreg.xch(), 0);
  EXPECT_EQ(conreg.mode(), ConReg::kModeMaster);
  EXPECT_EQ(conreg.en(), 1);

  EXPECT_EQ(enhanced_cntl.main_clock_always_on(), 0);
  EXPECT_EQ(enhanced_cntl.clk_cs_delay_enable(), 1);
  EXPECT_EQ(enhanced_cntl.cs_oen_enhance_enable(), 1);
  EXPECT_EQ(enhanced_cntl.clk_oen_enhance_enable(), 1);
  EXPECT_EQ(enhanced_cntl.mosi_oen_enhance_enable(), 1);
  EXPECT_EQ(enhanced_cntl.spi_clk_select(), 1);
  EXPECT_EQ(enhanced_cntl.enhance_clk_div(), 0xa5);
  EXPECT_EQ(enhanced_cntl.clk_cs_delay(), 0);

  EXPECT_EQ(testreg.dlyctl(), 0b00'11'00);
  EXPECT_EQ(testreg.clk_free_en(), 1);
}

TEST(AmlSpiTest, NormalClockModeInvalidDivider) {
  constexpr amlogic_spi::amlspi_config_t kTestSpiConfig[] = {
      {
          .bus_id = 0,
          .cs_count = 2,
          .cs = {5, 3},
          .clock_divider_register_value = 0xa5,
          .use_enhanced_clock_mode = false,
      },
  };

  FakeDdkSpi bind;
  bind.SetMetadata(DEVICE_METADATA_AMLSPI_CONFIG, kTestSpiConfig, sizeof(kTestSpiConfig));

  EXPECT_EQ(AmlSpi::Create(nullptr, fake_ddk::kFakeParent), ZX_ERR_INVALID_ARGS);
}

TEST(AmlSpiTest, EnhancedClockModeInvalidDivider) {
  constexpr amlogic_spi::amlspi_config_t kTestSpiConfig[] = {
      {
          .bus_id = 0,
          .cs_count = 2,
          .cs = {5, 3},
          .clock_divider_register_value = 0x1a5,
          .use_enhanced_clock_mode = true,
      },
  };

  FakeDdkSpi bind;
  bind.SetMetadata(DEVICE_METADATA_AMLSPI_CONFIG, kTestSpiConfig, sizeof(kTestSpiConfig));

  EXPECT_EQ(AmlSpi::Create(nullptr, fake_ddk::kFakeParent), ZX_ERR_INVALID_ARGS);
}

TEST(AmlSpiTest, ExchangeDma) {
  constexpr uint8_t kTxData[24] = {
      0x3c, 0xa7, 0x5f, 0xc8, 0x4b, 0x0b, 0xdf, 0xef, 0xb9, 0xa0, 0xcb, 0xbd,
      0xd4, 0xcf, 0xa8, 0xbf, 0x85, 0xf2, 0x6a, 0xe3, 0xba, 0xf1, 0x49, 0x00,
  };
  constexpr uint8_t kExpectedRxData[24] = {
      0xea, 0x2b, 0x8f, 0x8f, 0xea, 0x2b, 0x8f, 0x8f, 0xea, 0x2b, 0x8f, 0x8f,
      0xea, 0x2b, 0x8f, 0x8f, 0xea, 0x2b, 0x8f, 0x8f, 0xea, 0x2b, 0x8f, 0x8f,
  };

  uint8_t reversed_tx_data[24];
  for (size_t i = 0; i < sizeof(kTxData); i += sizeof(uint64_t)) {
    uint64_t tmp;
    memcpy(&tmp, kTxData + i, sizeof(tmp));
    tmp = htobe64(tmp);
    memcpy(reversed_tx_data + i, &tmp, sizeof(tmp));
  }

  uint8_t reversed_expected_rx_data[24];
  for (size_t i = 0; i < sizeof(kExpectedRxData); i += sizeof(uint64_t)) {
    uint64_t tmp;
    memcpy(&tmp, kExpectedRxData + i, sizeof(tmp));
    tmp = htobe64(tmp);
    memcpy(reversed_expected_rx_data + i, &tmp, sizeof(tmp));
  }

  FakeDdkSpi bind(true);

  constexpr zx_paddr_t kDmaPaddrs[] = {0x1212'0000, 0xabab'000};

  zx::bti bti;
  ASSERT_OK(
      fake_bti_create_with_paddrs(kDmaPaddrs, std::size(kDmaPaddrs), bti.reset_and_get_address()));

  zx::unowned_bti bti_local = bti.borrow();
  bind.pdev().set_bti(0, std::move(bti));

  EXPECT_OK(AmlSpi::Create(nullptr, fake_ddk::kFakeParent));

  ASSERT_EQ(bind.children().size(), 1);
  AmlSpi& spi0 = *bind.children()[0].device;

  fake_bti_pinned_vmo_info_t dma_vmos[2] = {};
  size_t actual_vmos = 0;
  EXPECT_OK(
      fake_bti_get_pinned_vmos(bti_local->get(), dma_vmos, std::size(dma_vmos), &actual_vmos));
  EXPECT_EQ(actual_vmos, std::size(dma_vmos));

  zx::vmo tx_dma_vmo(dma_vmos[0].vmo);
  zx::vmo rx_dma_vmo(dma_vmos[1].vmo);

  // Copy the reversed expected RX data to the RX VMO. The driver should copy this to the user
  // output buffer with the correct endianness.
  rx_dma_vmo.write(reversed_expected_rx_data, 0, sizeof(reversed_expected_rx_data));

  zx_paddr_t tx_paddr = 0;
  zx_paddr_t rx_paddr = 0;

  bind.mmio()[AML_SPI_DRADDR].SetWriteCallback([&tx_paddr](uint64_t value) { tx_paddr = value; });
  bind.mmio()[AML_SPI_DWADDR].SetWriteCallback([&rx_paddr](uint64_t value) { rx_paddr = value; });

  bind.gpio().ExpectWrite(ZX_OK, 0).ExpectWrite(ZX_OK, 1);

  uint8_t buf[24] = {};
  memcpy(buf, kTxData, sizeof(buf));

  size_t rx_actual;
  EXPECT_OK(spi0.SpiImplExchange(0, buf, sizeof(buf), buf, sizeof(buf), &rx_actual));
  EXPECT_EQ(rx_actual, sizeof(buf));
  EXPECT_BYTES_EQ(kExpectedRxData, buf, sizeof(buf));

  // Verify that the driver wrote the TX data to the TX VMO.
  EXPECT_OK(tx_dma_vmo.read(buf, 0, sizeof(buf)));
  EXPECT_BYTES_EQ(reversed_tx_data, buf, sizeof(buf));

  EXPECT_EQ(tx_paddr, kDmaPaddrs[0]);
  EXPECT_EQ(rx_paddr, kDmaPaddrs[1]);

  EXPECT_FALSE(bind.ControllerReset());
}

TEST(AmlSpiTest, ExchangeFallBackToPio) {
  constexpr uint8_t kTxData[15] = {
      0x3c, 0xa7, 0x5f, 0xc8, 0x4b, 0x0b, 0xdf, 0xef, 0xb9, 0xa0, 0xcb, 0xbd, 0xd4, 0xcf, 0xa8,
  };
  constexpr uint8_t kExpectedRxData[15] = {
      0xea, 0x2b, 0x8f, 0x8f, 0xea, 0x2b, 0x8f, 0x8f, 0x8f, 0x8f, 0x8f, 0x8f, 0x8f, 0x8f, 0x8f,
  };

  FakeDdkSpi bind(true);

  zx::bti bti;
  ASSERT_OK(fake_bti_create(bti.reset_and_get_address()));

  zx::unowned_bti bti_local = bti.borrow();
  bind.pdev().set_bti(0, std::move(bti));

  EXPECT_OK(AmlSpi::Create(nullptr, fake_ddk::kFakeParent));

  ASSERT_EQ(bind.children().size(), 1);
  AmlSpi& spi0 = *bind.children()[0].device;

  fake_bti_pinned_vmo_info_t dma_vmos[2] = {};
  size_t actual_vmos = 0;
  EXPECT_OK(
      fake_bti_get_pinned_vmos(bti_local->get(), dma_vmos, std::size(dma_vmos), &actual_vmos));
  EXPECT_EQ(actual_vmos, std::size(dma_vmos));

  zx_paddr_t tx_paddr = 0;
  zx_paddr_t rx_paddr = 0;

  bind.mmio()[AML_SPI_DRADDR].SetWriteCallback([&tx_paddr](uint64_t value) { tx_paddr = value; });
  bind.mmio()[AML_SPI_DWADDR].SetWriteCallback([&rx_paddr](uint64_t value) { rx_paddr = value; });

  bind.mmio()[AML_SPI_RXDATA].SetReadCallback([]() { return 0xea2b'8f8f; });

  uint64_t tx_data = 0;
  bind.mmio()[AML_SPI_TXDATA].SetWriteCallback([&tx_data](uint64_t value) { tx_data = value; });

  bind.gpio().ExpectWrite(ZX_OK, 0).ExpectWrite(ZX_OK, 1);

  uint8_t buf[15] = {};
  memcpy(buf, kTxData, sizeof(buf));

  size_t rx_actual;
  EXPECT_OK(spi0.SpiImplExchange(0, buf, sizeof(buf), buf, sizeof(buf), &rx_actual));
  EXPECT_EQ(rx_actual, sizeof(buf));
  EXPECT_BYTES_EQ(kExpectedRxData, buf, sizeof(buf));
  EXPECT_EQ(tx_data, kTxData[14]);

  // Verify that DMA was not used.
  EXPECT_EQ(tx_paddr, 0);
  EXPECT_EQ(rx_paddr, 0);

  EXPECT_FALSE(bind.ControllerReset());
}

TEST(AmlSpiTest, InterruptRequired) {
  FakeDdkSpi bind(/*add_reset_fragment=*/true, /*add_interrupt=*/false);

  // Bind should fail if no interrupt was provided.
  EXPECT_NOT_OK(AmlSpi::Create(nullptr, fake_ddk::kFakeParent));
}

TEST(AmlSpiTest, ExchangeDmaClientReversesBuffer) {
  constexpr uint8_t kTxData[24] = {
      0x3c, 0xa7, 0x5f, 0xc8, 0x4b, 0x0b, 0xdf, 0xef, 0xb9, 0xa0, 0xcb, 0xbd,
      0xd4, 0xcf, 0xa8, 0xbf, 0x85, 0xf2, 0x6a, 0xe3, 0xba, 0xf1, 0x49, 0x00,
  };
  constexpr uint8_t kExpectedRxData[24] = {
      0xea, 0x2b, 0x8f, 0x8f, 0xea, 0x2b, 0x8f, 0x8f, 0xea, 0x2b, 0x8f, 0x8f,
      0xea, 0x2b, 0x8f, 0x8f, 0xea, 0x2b, 0x8f, 0x8f, 0xea, 0x2b, 0x8f, 0x8f,
  };

  FakeDdkSpi bind(true);

  constexpr zx_paddr_t kDmaPaddrs[] = {0x1212'0000, 0xabab'000};

  zx::bti bti;
  ASSERT_OK(
      fake_bti_create_with_paddrs(kDmaPaddrs, std::size(kDmaPaddrs), bti.reset_and_get_address()));

  zx::unowned_bti bti_local = bti.borrow();
  bind.pdev().set_bti(0, std::move(bti));

  constexpr amlogic_spi::amlspi_config_t kSpiConfig[] = {
      {
          .bus_id = 0,
          .cs_count = 3,
          .cs = {5, 3, amlogic_spi::amlspi_config_t::kCsClientManaged},
          .clock_divider_register_value = 0,
          .use_enhanced_clock_mode = false,
          .client_reverses_dma_transfers = true,
      },
  };
  bind.SetMetadata(DEVICE_METADATA_AMLSPI_CONFIG, kSpiConfig, sizeof(kSpiConfig));

  EXPECT_OK(AmlSpi::Create(nullptr, fake_ddk::kFakeParent));

  ASSERT_EQ(bind.children().size(), 1);
  AmlSpi& spi0 = *bind.children()[0].device;

  fake_bti_pinned_vmo_info_t dma_vmos[2] = {};
  size_t actual_vmos = 0;
  EXPECT_OK(
      fake_bti_get_pinned_vmos(bti_local->get(), dma_vmos, std::size(dma_vmos), &actual_vmos));
  EXPECT_EQ(actual_vmos, std::size(dma_vmos));

  zx::vmo tx_dma_vmo(dma_vmos[0].vmo);
  zx::vmo rx_dma_vmo(dma_vmos[1].vmo);

  rx_dma_vmo.write(kExpectedRxData, 0, sizeof(kExpectedRxData));

  zx_paddr_t tx_paddr = 0;
  zx_paddr_t rx_paddr = 0;

  bind.mmio()[AML_SPI_DRADDR].SetWriteCallback([&tx_paddr](uint64_t value) { tx_paddr = value; });
  bind.mmio()[AML_SPI_DWADDR].SetWriteCallback([&rx_paddr](uint64_t value) { rx_paddr = value; });

  bind.gpio().ExpectWrite(ZX_OK, 0).ExpectWrite(ZX_OK, 1);

  uint8_t buf[sizeof(kTxData)] = {};
  memcpy(buf, kTxData, sizeof(buf));

  size_t rx_actual;
  EXPECT_OK(spi0.SpiImplExchange(0, buf, sizeof(buf), buf, sizeof(buf), &rx_actual));
  EXPECT_EQ(rx_actual, sizeof(buf));
  EXPECT_BYTES_EQ(kExpectedRxData, buf, sizeof(buf));

  // Verify that the driver wrote the TX data to the TX VMO with the original byte order.
  EXPECT_OK(tx_dma_vmo.read(buf, 0, sizeof(buf)));
  EXPECT_BYTES_EQ(kTxData, buf, sizeof(buf));

  EXPECT_EQ(tx_paddr, kDmaPaddrs[0]);
  EXPECT_EQ(rx_paddr, kDmaPaddrs[1]);

  EXPECT_FALSE(bind.ControllerReset());
}

TEST(AmlSpiTest, Shutdown) {
  // Must outlive bind.
  bool dmareg_cleared = false;
  bool conreg_cleared = false;

  FakeDdkSpi bind;

  EXPECT_OK(AmlSpi::Create(nullptr, fake_ddk::kFakeParent));

  ASSERT_EQ(bind.children().size(), 1);
  AmlSpi& spi0 = *bind.children()[0].device;

  bind.gpio().ExpectWrite(ZX_OK, 0).ExpectWrite(ZX_OK, 1);

  uint8_t buf[16] = {};
  size_t rx_actual;
  EXPECT_OK(spi0.SpiImplExchange(0, buf, sizeof(buf), buf, sizeof(buf), &rx_actual));

  bind.mmio()[AML_SPI_DMAREG].SetWriteCallback(
      [&dmareg_cleared](uint64_t value) { dmareg_cleared = value == 0; });

  bind.mmio()[AML_SPI_CONREG].SetWriteCallback(
      [&conreg_cleared](uint64_t value) { conreg_cleared = value == 0; });

  spi0.DdkUnbind(ddk::UnbindTxn{spi0.zxdev()});

  EXPECT_TRUE(dmareg_cleared);
  EXPECT_TRUE(conreg_cleared);

  // All SPI devices have been released at this point, so no further calls can be made.

  EXPECT_FALSE(bind.ControllerReset());

  ASSERT_NO_FATAL_FAILURE(bind.gpio().VerifyAndClear());
}

}  // namespace spi
