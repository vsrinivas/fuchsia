// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "aml-spi.h"

#include <fuchsia/hardware/gpio/cpp/banjo-mock.h>
#include <fuchsia/hardware/platform/device/cpp/banjo.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/ddk/metadata.h>
#include <lib/fake_ddk/fake_ddk.h>
#include <lib/fzl/vmo-mapper.h>
#include <lib/zx/vmo.h>

#include <memory>
#include <vector>

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

  explicit FakeDdkSpi(bool add_reset_fragment = true)
      : loop_(&kAsyncLoopConfigNeverAttachToThread), registers_(loop_.dispatcher()) {
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
        .irq_count = 1,
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
  }

  ~FakeDdkSpi() override {
    // Call DdkRelease on any children that haven't been removed yet.
    for (ChildDevice& child : children_) {
      child.device->DdkRelease();
    }
  }

  const std::vector<ChildDevice>& children() { return children_; }
  uint32_t* mmio() { return reinterpret_cast<uint32_t*>(mmio_mapper_.start()); }
  ddk::MockGpio& gpio() { return gpio_; }

  zx_status_t DeviceAdd(zx_driver_t* drv, zx_device_t* parent, device_add_args_t* args,
                        zx_device_t** out) override {
    if (parent != fake_ddk::kFakeParent) {
      return ZX_ERR_BAD_STATE;
    }

    children_.push_back(ChildDevice{
        .device = reinterpret_cast<AmlSpi*>(args->ctx),
        .unbind_op = args->ops->unbind,
    });

    if (children_.size() == countof(kSpiConfig)) {
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
  static constexpr amlspi_config_t kSpiConfig[] = {
      {.bus_id = 0, .cs_count = 2, .cs = {5, 3}},
  };

  async::Loop loop_;
  mock_registers::MockRegistersDevice registers_;
  std::vector<ChildDevice> children_;
  zx::vmo mmio_;
  fzl::VmoMapper mmio_mapper_;
  ddk::MockGpio gpio_;
  fake_pdev::FakePDev pdev_;
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

  const mmio_buffer_t mmio_buffer = {
      .vaddr = reinterpret_cast<MMIO_PTR void*>(reinterpret_cast<uintptr_t>(instance->mmio())),
      .offset = pdev_mmio.offset,
      .size = pdev_mmio.size,
      .vmo = pdev_mmio.vmo,
  };

  mmio->emplace(mmio_buffer);
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

  EXPECT_EQ(spi0.SpiImplGetChipSelectCount(), 2);
}

TEST(AmlSpiTest, Exchange) {
  constexpr uint8_t kTxData[] = {0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12};
  constexpr uint8_t kExpectedRxData[] = {0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab};

  FakeDdkSpi bind;

  EXPECT_OK(AmlSpi::Create(nullptr, fake_ddk::kFakeParent));

  ASSERT_EQ(bind.children().size(), 1);
  AmlSpi& spi0 = *bind.children()[0].device;

  // Zero out rxcnt and txcnt just in case.
  bind.mmio()[AML_SPI_TESTREG / sizeof(uint32_t)] = 0;

  bind.mmio()[AML_SPI_RXDATA / sizeof(uint32_t)] = kExpectedRxData[0];

  bind.gpio().ExpectWrite(ZX_OK, 0).ExpectWrite(ZX_OK, 1);

  uint8_t rxbuf[sizeof(kTxData)] = {};
  size_t rx_actual;
  EXPECT_OK(spi0.SpiImplExchange(0, kTxData, sizeof(kTxData), rxbuf, sizeof(rxbuf), &rx_actual));

  EXPECT_EQ(rx_actual, sizeof(rxbuf));
  EXPECT_BYTES_EQ(rxbuf, kExpectedRxData, rx_actual);
  EXPECT_EQ(bind.mmio()[AML_SPI_TXDATA / sizeof(uint32_t)], kTxData[0]);

  EXPECT_FALSE(bind.ControllerReset());

  ASSERT_NO_FATAL_FAILURES(bind.gpio().VerifyAndClear());
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

  bind.mmio()[AML_SPI_TESTREG / sizeof(uint32_t)] = 0;

  bind.gpio().ExpectWrite(ZX_OK, 0).ExpectWrite(ZX_OK, 1);

  EXPECT_OK(test_vmo.write(kTxData, 512, sizeof(kTxData)));

  EXPECT_OK(spi1.SpiImplTransmitVmo(0, 1, 256, sizeof(kTxData)));

  EXPECT_EQ(bind.mmio()[AML_SPI_TXDATA / sizeof(uint32_t)], kTxData[0]);

  EXPECT_FALSE(bind.ControllerReset());

  ASSERT_NO_FATAL_FAILURES(bind.gpio().VerifyAndClear());
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

  bind.mmio()[AML_SPI_TESTREG / sizeof(uint32_t)] = 0;

  bind.mmio()[AML_SPI_RXDATA / sizeof(uint32_t)] = kExpectedRxData[0];

  bind.gpio().ExpectWrite(ZX_OK, 0).ExpectWrite(ZX_OK, 1);

  EXPECT_OK(spi1.SpiImplReceiveVmo(0, 1, 512, sizeof(kExpectedRxData)));

  uint8_t rx_buffer[sizeof(kExpectedRxData)];
  EXPECT_OK(test_vmo.read(rx_buffer, 768, sizeof(rx_buffer)));
  EXPECT_BYTES_EQ(rx_buffer, kExpectedRxData, sizeof(rx_buffer));

  EXPECT_FALSE(bind.ControllerReset());

  ASSERT_NO_FATAL_FAILURES(bind.gpio().VerifyAndClear());
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

  bind.mmio()[AML_SPI_TESTREG / sizeof(uint32_t)] = 0;

  bind.mmio()[AML_SPI_RXDATA / sizeof(uint32_t)] = kExpectedRxData[0];

  bind.gpio().ExpectWrite(ZX_OK, 0).ExpectWrite(ZX_OK, 1);

  EXPECT_OK(test_vmo.write(kTxData, 512, sizeof(kTxData)));

  EXPECT_OK(spi1.SpiImplExchangeVmo(0, 1, 256, 1, 512, sizeof(kTxData)));

  uint8_t rx_buffer[sizeof(kExpectedRxData)];
  EXPECT_OK(test_vmo.read(rx_buffer, 768, sizeof(rx_buffer)));
  EXPECT_BYTES_EQ(rx_buffer, kExpectedRxData, sizeof(rx_buffer));

  EXPECT_EQ(bind.mmio()[AML_SPI_TXDATA / sizeof(uint32_t)], kTxData[0]);

  EXPECT_FALSE(bind.ControllerReset());

  ASSERT_NO_FATAL_FAILURES(bind.gpio().VerifyAndClear());
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

  bind.mmio()[AML_SPI_TESTREG / sizeof(uint32_t)] = 0;

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

  ASSERT_NO_FATAL_FAILURES(bind.gpio().VerifyAndClear());
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

  bind.mmio()[AML_SPI_TESTREG / sizeof(uint32_t)] = 0;

  bind.gpio().ExpectWrite(ZX_OK, 0).ExpectWrite(ZX_OK, 1);

  EXPECT_OK(spi1.SpiImplExchangeVmo(0, 1, 0, 2, 128, 128));
  EXPECT_EQ(spi1.SpiImplExchangeVmo(0, 2, 0, 1, 128, 128), ZX_ERR_ACCESS_DENIED);
  EXPECT_EQ(spi1.SpiImplExchangeVmo(0, 1, 0, 1, 128, 128), ZX_ERR_ACCESS_DENIED);
  EXPECT_EQ(spi1.SpiImplReceiveVmo(0, 1, 0, 128), ZX_ERR_ACCESS_DENIED);

  ASSERT_NO_FATAL_FAILURES(bind.gpio().VerifyAndClear());
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

  // Zero out rxcnt and txcnt just in case.
  bind.mmio()[AML_SPI_TESTREG / sizeof(uint32_t)] = 0;

  // First (and only) word of kExpectedRxData with bytes swapped.
  bind.mmio()[AML_SPI_RXDATA / sizeof(uint32_t)] = 0xea2b'8f8f;

  bind.gpio().ExpectWrite(ZX_OK, 0).ExpectWrite(ZX_OK, 1);

  uint8_t rxbuf[sizeof(kTxData)] = {};
  size_t rx_actual;
  EXPECT_OK(spi0.SpiImplExchange(0, kTxData, sizeof(kTxData), rxbuf, sizeof(rxbuf), &rx_actual));

  EXPECT_EQ(rx_actual, sizeof(rxbuf));
  EXPECT_BYTES_EQ(rxbuf, kExpectedRxData, rx_actual);
  // Last word of kTxData with bytes swapped.
  EXPECT_EQ(bind.mmio()[AML_SPI_TXDATA / sizeof(uint32_t)], 0xbaf1'4900);

  EXPECT_FALSE(bind.ControllerReset());

  ASSERT_NO_FATAL_FAILURES(bind.gpio().VerifyAndClear());
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

  bind.mmio()[AML_SPI_TESTREG / sizeof(uint32_t)] = 0;

  bind.mmio()[AML_SPI_RXDATA / sizeof(uint32_t)] = 0xea;

  bind.gpio().ExpectWrite(ZX_OK, 0).ExpectWrite(ZX_OK, 1);

  uint8_t rxbuf[sizeof(kTxData)] = {};
  size_t rx_actual;
  EXPECT_OK(spi0.SpiImplExchange(0, kTxData, sizeof(kTxData), rxbuf, sizeof(rxbuf), &rx_actual));

  EXPECT_EQ(rx_actual, sizeof(rxbuf));
  EXPECT_BYTES_EQ(rxbuf, kExpectedRxData, rx_actual);
  EXPECT_EQ(bind.mmio()[AML_SPI_TXDATA / sizeof(uint32_t)], 0xba);

  EXPECT_FALSE(bind.ControllerReset());

  ASSERT_NO_FATAL_FAILURES(bind.gpio().VerifyAndClear());
}

TEST(AmlSpiTest, ExchangeResetsController) {
  FakeDdkSpi bind;

  EXPECT_OK(AmlSpi::Create(nullptr, fake_ddk::kFakeParent));

  ASSERT_EQ(bind.children().size(), 1);
  AmlSpi& spi0 = *bind.children()[0].device;

  bind.mmio()[AML_SPI_TESTREG / sizeof(uint32_t)] = 0;
  bind.gpio().ExpectWrite(ZX_OK, 0).ExpectWrite(ZX_OK, 1);

  uint8_t buf[17] = {};
  size_t rx_actual;
  EXPECT_OK(spi0.SpiImplExchange(0, buf, 17, buf, 17, &rx_actual));
  EXPECT_EQ(rx_actual, 17);
  EXPECT_FALSE(bind.ControllerReset());

  bind.mmio()[AML_SPI_TESTREG / sizeof(uint32_t)] = 0;
  bind.gpio().ExpectWrite(ZX_OK, 0).ExpectWrite(ZX_OK, 1);

  // Controller should be reset because a 64-bit transfer was preceded by a transfer of an odd
  // number of bytes.
  EXPECT_OK(spi0.SpiImplExchange(0, buf, 16, buf, 16, &rx_actual));
  EXPECT_EQ(rx_actual, 16);
  EXPECT_TRUE(bind.ControllerReset());

  bind.mmio()[AML_SPI_TESTREG / sizeof(uint32_t)] = 0;
  bind.gpio().ExpectWrite(ZX_OK, 0).ExpectWrite(ZX_OK, 1);

  EXPECT_OK(spi0.SpiImplExchange(0, buf, 3, buf, 3, &rx_actual));
  EXPECT_EQ(rx_actual, 3);
  EXPECT_FALSE(bind.ControllerReset());

  bind.mmio()[AML_SPI_TESTREG / sizeof(uint32_t)] = 0;
  bind.gpio().ExpectWrite(ZX_OK, 0).ExpectWrite(ZX_OK, 1);

  EXPECT_OK(spi0.SpiImplExchange(0, buf, 6, buf, 6, &rx_actual));
  EXPECT_EQ(rx_actual, 6);
  EXPECT_FALSE(bind.ControllerReset());

  bind.mmio()[AML_SPI_TESTREG / sizeof(uint32_t)] = 0;
  bind.gpio().ExpectWrite(ZX_OK, 0).ExpectWrite(ZX_OK, 1);

  EXPECT_OK(spi0.SpiImplExchange(0, buf, 8, buf, 8, &rx_actual));
  EXPECT_EQ(rx_actual, 8);
  EXPECT_TRUE(bind.ControllerReset());

  ASSERT_NO_FATAL_FAILURES(bind.gpio().VerifyAndClear());
}

TEST(AmlSpiTest, ExchangeWithNoResetFragment) {
  FakeDdkSpi bind(false);

  EXPECT_OK(AmlSpi::Create(nullptr, fake_ddk::kFakeParent));

  ASSERT_EQ(bind.children().size(), 1);
  AmlSpi& spi0 = *bind.children()[0].device;

  bind.mmio()[AML_SPI_TESTREG / sizeof(uint32_t)] = 0;
  bind.gpio().ExpectWrite(ZX_OK, 0).ExpectWrite(ZX_OK, 1);

  uint8_t buf[17] = {};
  size_t rx_actual;
  EXPECT_OK(spi0.SpiImplExchange(0, buf, 17, buf, 17, &rx_actual));
  EXPECT_EQ(rx_actual, 17);
  EXPECT_FALSE(bind.ControllerReset());

  bind.mmio()[AML_SPI_TESTREG / sizeof(uint32_t)] = 0;
  bind.gpio().ExpectWrite(ZX_OK, 0).ExpectWrite(ZX_OK, 1);

  // Controller should not be reset because no reset fragment was provided.
  EXPECT_OK(spi0.SpiImplExchange(0, buf, 16, buf, 16, &rx_actual));
  EXPECT_EQ(rx_actual, 16);
  EXPECT_FALSE(bind.ControllerReset());

  bind.mmio()[AML_SPI_TESTREG / sizeof(uint32_t)] = 0;
  bind.gpio().ExpectWrite(ZX_OK, 0).ExpectWrite(ZX_OK, 1);

  EXPECT_OK(spi0.SpiImplExchange(0, buf, 3, buf, 3, &rx_actual));
  EXPECT_EQ(rx_actual, 3);
  EXPECT_FALSE(bind.ControllerReset());

  bind.mmio()[AML_SPI_TESTREG / sizeof(uint32_t)] = 0;
  bind.gpio().ExpectWrite(ZX_OK, 0).ExpectWrite(ZX_OK, 1);

  EXPECT_OK(spi0.SpiImplExchange(0, buf, 6, buf, 6, &rx_actual));
  EXPECT_EQ(rx_actual, 6);
  EXPECT_FALSE(bind.ControllerReset());

  bind.mmio()[AML_SPI_TESTREG / sizeof(uint32_t)] = 0;
  bind.gpio().ExpectWrite(ZX_OK, 0).ExpectWrite(ZX_OK, 1);

  EXPECT_OK(spi0.SpiImplExchange(0, buf, 8, buf, 8, &rx_actual));
  EXPECT_EQ(rx_actual, 8);
  EXPECT_FALSE(bind.ControllerReset());

  ASSERT_NO_FATAL_FAILURES(bind.gpio().VerifyAndClear());
}

}  // namespace spi
