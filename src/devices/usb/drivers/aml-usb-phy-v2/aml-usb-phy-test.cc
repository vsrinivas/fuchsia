// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "aml-usb-phy.h"

#include <lib/fake_ddk/fake_ddk.h>
#include <lib/zx/clock.h>
#include <lib/zx/interrupt.h>
#include <zircon/errors.h>
#include <zircon/syscalls.h>

#include <list>
#include <memory>

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/metadata.h>
#include <ddk/protocol/platform/device.h>
#include <fake-mmio-reg/fake-mmio-reg.h>
#include <zxtest/zxtest.h>

struct zx_device : std::enable_shared_from_this<zx_device> {
  std::list<std::shared_ptr<zx_device>> devices;
  std::weak_ptr<zx_device> parent;
  std::vector<zx_device_prop_t> props;
  pdev_protocol_t pdev_ops;
  zx_protocol_device_t dev_ops;
  virtual ~zx_device() = default;
};

namespace aml_usb_phy {

enum class RegisterIndex : size_t {
  Reset = 0,
  Control = 1,
  Phy0 = 2,
  Phy1 = 3,
};

constexpr auto kRegisterBanks = 4;
constexpr auto kRegisterCount = 2048;

class FakeDevice : public ddk::PDevProtocol<FakeDevice, ddk::base_protocol> {
 public:
  FakeDevice() : pdev_({&pdev_protocol_ops_, this}) {
    // Initialize register read/write hooks.
    for (size_t i = 0; i < kRegisterBanks; i++) {
      for (size_t c = 0; c < kRegisterCount; c++) {
        regs_[i][c].SetReadCallback([this, i, c]() { return reg_values_[i][c]; });
        regs_[i][c].SetWriteCallback([this, i, c](uint64_t value) { reg_values_[i][c] = value; });
      }
      regions_[i].emplace(regs_[i], sizeof(uint32_t), kRegisterCount);
    }
  }

  const pdev_protocol_t* pdev() const { return &pdev_; }

  zx_status_t PDevGetMmio(uint32_t index, pdev_mmio_t* out_mmio) {
    out_mmio->offset = reinterpret_cast<size_t>(&regions_[index]);
    return ZX_OK;
  }

  ddk::MmioBuffer mmio(RegisterIndex index) {
    return ddk::MmioBuffer(regions_[static_cast<size_t>(index)]->GetMmioBuffer());
  }

  zx_status_t PDevGetInterrupt(uint32_t index, uint32_t flags, zx::interrupt* out_irq) {
    irq_signaller_ = zx::unowned_interrupt(irq_);
    *out_irq = std::move(irq_);
    return ZX_OK;
  }

  void Interrupt() { irq_signaller_->trigger(0, zx::clock::get_monotonic()); }

  zx_status_t PDevGetBti(uint32_t index, zx::bti* out_bti) { return ZX_ERR_NOT_SUPPORTED; }

  zx_status_t PDevGetSmc(uint32_t index, zx::resource* out_resource) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  zx_status_t PDevGetDeviceInfo(pdev_device_info_t* out_info) { return ZX_ERR_NOT_SUPPORTED; }

  zx_status_t PDevGetBoardInfo(pdev_board_info_t* out_info) { return ZX_ERR_NOT_SUPPORTED; }

  ~FakeDevice() {}

 private:
  zx::unowned_interrupt irq_signaller_;
  zx::interrupt irq_;
  uint64_t reg_values_[kRegisterBanks][kRegisterCount] = {};
  ddk_fake::FakeMmioReg regs_[kRegisterBanks][kRegisterCount];
  std::optional<ddk_fake::FakeMmioRegRegion> regions_[kRegisterBanks];
  pdev_protocol_t pdev_;
};

static void DestroyDevices(zx_device_t* node) {
  for (auto& dev : node->devices) {
    DestroyDevices(dev.get());
    if (dev->dev_ops.unbind) {
      dev->dev_ops.unbind(dev->pdev_ops.ctx);
    }
    dev->dev_ops.release(dev->pdev_ops.ctx);
  }
}

class Ddk : public fake_ddk::Bind {
 public:
  zx_status_t DeviceGetMetadata(zx_device_t* dev, uint32_t type, void* data, size_t length,
                                size_t* actual) override {
    uint32_t magic_numbers[8] = {};
    if ((type != DEVICE_METADATA_PRIVATE) || (length != sizeof(magic_numbers))) {
      return ZX_ERR_INVALID_ARGS;
    }
    memcpy(data, magic_numbers, sizeof(magic_numbers));
    *actual = sizeof(magic_numbers);
    return ZX_OK;
  }
  zx_status_t DeviceGetProtocol(const zx_device_t* device, uint32_t proto_id,
                                void* protocol) override {
    if (proto_id != ZX_PROTOCOL_PDEV) {
      return ZX_ERR_NOT_SUPPORTED;
    }
    *static_cast<pdev_protocol_t*>(protocol) = *const_cast<pdev_protocol_t*>(&device->pdev_ops);
    return ZX_OK;
  }
  zx_status_t DeviceAdd(zx_driver_t* drv, zx_device_t* parent, device_add_args_t* args,
                        zx_device_t** out) override {
    auto dev = std::make_shared<zx_device>();
    dev->pdev_ops.ctx = args->ctx;
    dev->pdev_ops.ops = static_cast<pdev_protocol_ops_t*>(args->proto_ops);
    if (args->props) {
      dev->props.resize(args->prop_count);
      memcpy(dev->props.data(), args->props, args->prop_count * sizeof(zx_device_prop_t));
    }
    dev->dev_ops = *args->ops;
    dev->parent = parent->weak_from_this();
    parent->devices.push_back(dev);
    *out = dev.get();
    return ZX_OK;
  }

  zx_status_t DeviceRemove(zx_device_t* device) override {
    DestroyDevices(device);
    return ZX_OK;
  }
};

TEST(AmlUsbPhy, DoesNotCrash) {
  Ddk ddk;
  auto pdev = std::make_unique<FakeDevice>();
  auto root_device = std::make_shared<zx_device_t>();
  root_device->pdev_ops = *pdev->pdev();
  zx::interrupt irq;
  ASSERT_OK(zx::interrupt::create(zx::resource(), 0, ZX_INTERRUPT_VIRTUAL, &irq));
  ASSERT_OK(AmlUsbPhy::Create(nullptr, root_device.get()));
  DestroyDevices(root_device.get());
}

}  // namespace aml_usb_phy

zx_status_t ddk::PDev::MapMmio(uint32_t index, std::optional<MmioBuffer>* mmio,
                               uint32_t cache_policy) {
  pdev_mmio_t pdev_mmio;
  zx_status_t status = GetMmio(index, &pdev_mmio);
  if (status != ZX_OK) {
    return status;
  }
  auto* src = reinterpret_cast<ddk_fake::FakeMmioRegRegion*>(pdev_mmio.offset);
  mmio->emplace(src->GetMmioBuffer());
  return ZX_OK;
}
