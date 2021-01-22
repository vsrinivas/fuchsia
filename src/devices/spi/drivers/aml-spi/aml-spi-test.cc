// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "aml-spi.h"

#include <fuchsia/hardware/gpio/cpp/banjo-mock.h>
#include <fuchsia/hardware/platform/device/cpp/banjo.h>
#include <lib/fake_ddk/fake_ddk.h>
#include <lib/fzl/vmo-mapper.h>
#include <lib/zx/vmo.h>

#include <memory>
#include <vector>

#include <zxtest/zxtest.h>

#include "registers.h"

namespace spi {

class FakeDdkSpi : public fake_ddk::Bind,
                   public ddk::CompositeProtocol<FakeDdkSpi>,
                   public ddk::PDevProtocol<FakeDdkSpi> {
 public:
  struct ChildDevice {
    AmlSpi* device;
    void (*unbind_op)(void*);
  };

  static FakeDdkSpi* instance() { return static_cast<FakeDdkSpi*>(instance_); }

  FakeDdkSpi() {
    fbl::Array<fake_ddk::ProtocolEntry> protocols(new fake_ddk::ProtocolEntry[3], 3);
    ASSERT_TRUE(protocols);
    protocols[0] = {ZX_PROTOCOL_COMPOSITE, {&composite_protocol_ops_, this}};
    protocols[1] = {ZX_PROTOCOL_PDEV, {&pdev_protocol_ops_, this}};
    protocols[2] = {ZX_PROTOCOL_GPIO, {gpio_.GetProto()->ops, &gpio_}};
    SetProtocols(std::move(protocols));

    SetMetadata(kGpioMap, sizeof(kGpioMap));

    ASSERT_OK(
        mmio_mapper_.CreateAndMap(0x100, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, nullptr, &mmio_));
  }

  ~FakeDdkSpi() {
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

    if (children_.size() == countof(kGpioMap)) {
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

  // ZX_PROTOCOL_COMPOSITE

  uint32_t CompositeGetFragmentCount() {
    uint32_t gpio_count = 0;
    for (const amlspi_cs_map_t& gpio_map : kGpioMap) {
      gpio_count += gpio_map.cs_count;
    }
    return gpio_count + 1;  // Add one for the platform device.
  }

  void CompositeGetFragments(composite_device_fragment_t* out_fragment_list, size_t fragment_count,
                             size_t* out_fragment_actual) {
    *out_fragment_actual = 0;
  }

  bool CompositeGetFragment(const char* name, zx_device_t** out_fragment) {
    if (strcmp(name, "fuchsia.hardware.platform.device.PDev") != 0 &&
        strcmp(name, "gpio-cs-2") != 0 && strcmp(name, "gpio-cs-3") != 0 &&
        strcmp(name, "gpio-cs-5") != 0) {
      return false;
    }

    // Won't actually be the parent device, but this makes fake_ddk vend the right protocols.
    *out_fragment = fake_ddk::kFakeParent;
    return true;
  }

  // ZX_PROTOCOL_PDEV

  zx_status_t PDevGetMmio(uint32_t index, pdev_mmio_t* out_mmio) {
    if (index >= countof(kGpioMap)) {
      return ZX_ERR_NOT_FOUND;
    }

    out_mmio->offset = 0;
    out_mmio->size = mmio_mapper_.size();
    out_mmio->vmo = mmio_.get();
    return ZX_OK;
  }

  zx_status_t PDevGetInterrupt(uint32_t index, uint32_t flags, zx::interrupt* out_irq) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  zx_status_t PDevGetBti(uint32_t index, zx::bti* out_bti) { return ZX_ERR_NOT_SUPPORTED; }

  zx_status_t PDevGetSmc(uint32_t index, zx::resource* out_smc) { return ZX_ERR_NOT_SUPPORTED; }

  zx_status_t PDevGetDeviceInfo(pdev_device_info_t* out_info) {
    *out_info = {
        .mmio_count = countof(kGpioMap),
        .irq_count = countof(kGpioMap),
    };
    return ZX_OK;
  }

  zx_status_t PDevGetBoardInfo(pdev_board_info_t* out_info) { return ZX_ERR_NOT_SUPPORTED; }

 private:
  static constexpr amlspi_cs_map_t kGpioMap[] = {
      {.bus_id = 0, .cs_count = 2, .cs = {5, 3}},
      {.bus_id = 1, .cs_count = 1, .cs = {2}},
  };

  std::vector<ChildDevice> children_;
  zx::vmo mmio_;
  fzl::VmoMapper mmio_mapper_;
  ddk::MockGpio gpio_;
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

TEST(AmlSpiTest, DdkLifecycle) {
  FakeDdkSpi bind;

  EXPECT_OK(AmlSpi::Create(nullptr, fake_ddk::kFakeParent));

  ASSERT_EQ(bind.children().size(), 2);
  device_async_remove(reinterpret_cast<zx_device_t*>(bind.children()[0].device));

  ASSERT_EQ(bind.children().size(), 1);
  device_async_remove(reinterpret_cast<zx_device_t*>(bind.children()[0].device));

  EXPECT_TRUE(bind.Ok());
}

TEST(AmlSpiTest, ChipSelectCount) {
  FakeDdkSpi bind;

  EXPECT_OK(AmlSpi::Create(nullptr, fake_ddk::kFakeParent));

  ASSERT_EQ(bind.children().size(), 2);
  AmlSpi& spi0 = *bind.children()[0].device;
  AmlSpi& spi1 = *bind.children()[1].device;

  EXPECT_EQ(spi0.SpiImplGetChipSelectCount(), 2);
  EXPECT_EQ(spi1.SpiImplGetChipSelectCount(), 1);
}

TEST(AmlSpiTest, Exchange) {
  constexpr uint8_t kTxData[] = {0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12};
  constexpr uint8_t kExpectedRxData[] = {0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab};

  FakeDdkSpi bind;

  EXPECT_OK(AmlSpi::Create(nullptr, fake_ddk::kFakeParent));

  ASSERT_EQ(bind.children().size(), 2);
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

  ASSERT_NO_FATAL_FAILURES(bind.gpio().VerifyAndClear());
}

}  // namespace spi
