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
#include <queue>
#include <thread>

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/metadata.h>
#include <ddk/protocol/platform/device.h>
#include <fake-mmio-reg/fake-mmio-reg.h>
#include <fbl/auto_lock.h>
#include <fbl/condition_variable.h>
#include <fbl/mutex.h>
#include <zxtest/zxtest.h>

#include "usb-phy-regs.h"

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

constexpr auto kRegisterBanks = 5;
constexpr auto kRegisterCount = 2048;

class FakeDevice : public ddk::PDevProtocol<FakeDevice, ddk::base_protocol> {
 public:
  FakeDevice() : pdev_({&pdev_protocol_ops_, this}) {
    // Initialize register read/write hooks.
    for (size_t i = 0; i < kRegisterBanks; i++) {
      for (size_t c = 0; c < kRegisterCount; c++) {
        regs_[i][c].SetReadCallback([this, i, c]() { return reg_values_[i][c]; });
        regs_[i][c].SetWriteCallback([this, i, c](uint64_t value) {
          reg_values_[i][c] = value;
          if (callback_) {
            (*callback_)(i, c, value);
          }
        });
      }
      regions_[i].emplace(regs_[i], sizeof(uint32_t), kRegisterCount);
    }
    ASSERT_OK(zx::interrupt::create(zx::resource(), 0, ZX_INTERRUPT_VIRTUAL, &irq_));
  }

  void SetWriteCallback(fit::function<void(size_t bank, size_t reg, uint64_t value)> callback) {
    callback_ = std::move(callback);
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
  std::optional<fit::function<void(size_t bank, size_t reg, uint64_t value)>> callback_;
  zx::unowned_interrupt irq_signaller_;
  zx::interrupt irq_;
  uint64_t reg_values_[kRegisterBanks][kRegisterCount] = {};
  ddk_fake::FakeMmioReg regs_[kRegisterBanks][kRegisterCount];
  std::optional<ddk_fake::FakeMmioRegRegion> regions_[kRegisterBanks];
  pdev_protocol_t pdev_;
};

class Ddk : public fake_ddk::Bind {
 public:
  // Device lifecycle events that will be recorded and returned by |WaitForEvent|.
  enum struct EventType { DEVICE_ADDED, DEVICE_RELEASED };
  struct Event {
    EventType type;
    void* device_ctx;  // The test should not dereference this if the device has been released.
  };

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

    fbl::AutoLock lock(&events_lock_);
    events_.push(Event{EventType::DEVICE_ADDED, args->ctx});
    events_signal_.Signal();

    if (dev->dev_ops.init) {
      dev->dev_ops.init(dev->pdev_ops.ctx);
    }
    return ZX_OK;
  }

  // Schedules a device to be unbound and released.
  // If the test expects this to be called, it should wait for the corresponding DEVICE_RELEASED
  // event.
  void DeviceAsyncRemove(zx_device_t* device) override {
    // Run this in a new thread to simulate the asynchronous nature.
    std::thread t([&] {
      // Call the unbind hook. When unbind replies, |DeviceRemove| will handle
      // unbinding and releasing the children, then releasing the device itself.
      if (device->dev_ops.unbind) {
        device->dev_ops.unbind(device->pdev_ops.ctx);
      } else {
        // The unbind hook has not been implemented, so we can reply to the unbind immediately.
        DeviceRemove(device);
      }
    });
    t.detach();
  }

  // Called once unbind replies.
  zx_status_t DeviceRemove(zx_device_t* device) override {
    // Unbind and release all children.
    DestroyDevices(device);

    auto parent = device->parent.lock();
    if (parent && parent->dev_ops.child_pre_release) {
      parent->dev_ops.child_pre_release(parent->pdev_ops.ctx, device->pdev_ops.ctx);
    }
    device->dev_ops.release(device->pdev_ops.ctx);

    fbl::AutoLock lock(&events_lock_);
    events_.push(Event{EventType::DEVICE_RELEASED, device->pdev_ops.ctx});

    // Remove it from the parent's devices list so that we don't try
    // to unbind it again when cleaning up at the end of the test with |DestroyDevices|.
    // This may drop the last reference to the zx_device object.
    if (parent) {
      parent->devices.erase(std::find_if(parent->devices.begin(), parent->devices.end(),
                                         [&](const auto& dev) { return dev.get() == device; }));
    }
    events_signal_.Signal();
    return ZX_OK;
  }

  void DestroyDevices(zx_device_t* node) {
    // Make a copy of the list, as the device will remove itself from the parent's list after
    // being released.
    std::list<std::shared_ptr<zx_device>> devices(node->devices);
    for (auto& dev : devices) {
      // Call the unbind hook. When unbind replies, |DeviceRemove| will handle
      // unbinding and releasing the children, then releasing the device itself.
      if (dev->dev_ops.unbind) {
        dev->dev_ops.unbind(dev->pdev_ops.ctx);
      } else {
        // The unbind hook has not been implemented, so we can reply to the unbind immediately.
        DeviceRemove(dev.get());
      }
    }
  }

  // Blocks until the next device lifecycle event is recorded and returns the event.
  Event WaitForEvent() {
    fbl::AutoLock lock(&events_lock_);
    while (events_.empty()) {
      events_signal_.Wait(&events_lock_);
    }
    auto event = events_.front();
    events_.pop();
    return event;
  }

 private:
  fbl::Mutex events_lock_;
  fbl::ConditionVariable events_signal_ __TA_GUARDED(events_lock_);
  std::queue<Event> events_;
};

TEST(AmlUsbPhy, DoesNotCrash) {
  Ddk ddk;
  auto pdev = std::make_unique<FakeDevice>();
  auto root_device = std::make_shared<zx_device_t>();
  root_device->pdev_ops = *pdev->pdev();
  zx::interrupt irq;
  ASSERT_OK(zx::interrupt::create(zx::resource(), 0, ZX_INTERRUPT_VIRTUAL, &irq));
  ASSERT_OK(AmlUsbPhy::Create(nullptr, root_device.get()));
  ddk.DestroyDevices(root_device.get());
}

TEST(AmlUsbPhy, FIDLWrites) {
  Ddk ddk;
  auto pdev = std::make_unique<FakeDevice>();
  auto root_device = std::make_shared<zx_device_t>();
  root_device->pdev_ops = *pdev->pdev();
  zx::interrupt irq;
  bool written = false;
  pdev->SetWriteCallback([&](uint64_t bank, uint64_t index, size_t value) {
    if ((bank == 4) && (index = 5) && (value == 42)) {
      written = true;
    }
  });
  ASSERT_OK(zx::interrupt::create(zx::resource(), 0, ZX_INTERRUPT_VIRTUAL, &irq));
  ASSERT_OK(AmlUsbPhy::Create(nullptr, root_device.get()));
  fake_ddk::FidlMessenger fidl;
  fidl.SetMessageOp(root_device->devices.front().get(),
                    [](void* ctx, fidl_msg_t* msg, fidl_txn_t* txn) {
                      auto dev = static_cast<zx_device_t*>(ctx);
                      return dev->dev_ops.message(dev->pdev_ops.ctx, msg, txn);
                    });

  llcpp::fuchsia::hardware::registers::Device::SyncClient device(std::move(fidl.local()));
  constexpr auto kUsbBaseAddress = 0xff400000;
  ASSERT_OK(device.WriteRegister(kUsbBaseAddress + (5 * 4), 42));
  ASSERT_TRUE(written);
  ddk.DestroyDevices(root_device.get());
}

TEST(AmlUsbPhy, SetMode) {
  Ddk ddk;
  auto pdev = std::make_unique<FakeDevice>();
  auto root_device = std::make_shared<zx_device_t>();
  root_device->pdev_ops = *pdev->pdev();
  ASSERT_OK(AmlUsbPhy::Create(nullptr, root_device.get()));

  // The aml-usb-phy device should be added.
  auto event = ddk.WaitForEvent();
  ASSERT_EQ(event.type, Ddk::EventType::DEVICE_ADDED);
  auto root_ctx = static_cast<AmlUsbPhy*>(event.device_ctx);

  // Wait for host mode to be set by the irq thread. This should add the xhci child device.
  event = ddk.WaitForEvent();
  ASSERT_EQ(event.type, Ddk::EventType::DEVICE_ADDED);
  auto xhci_ctx = event.device_ctx;
  ASSERT_NE(xhci_ctx, root_ctx);
  ASSERT_EQ(root_ctx->mode(), AmlUsbPhy::UsbMode::HOST);

  ddk::PDev client(&root_device->pdev_ops);
  std::optional<ddk::MmioBuffer> usbctrl_mmio;
  ASSERT_OK(client.MapMmio(1, &usbctrl_mmio));

  // Switch to peripheral mode. This will be read by the irq thread.
  USB_R5_V2::Get().FromValue(0).set_iddig_curr(1).WriteTo(&usbctrl_mmio.value());
  // Wake up the irq thread.
  pdev->Interrupt();

  event = ddk.WaitForEvent();
  ASSERT_EQ(event.type, Ddk::EventType::DEVICE_ADDED);
  auto dwc2_ctx = event.device_ctx;
  ASSERT_NE(dwc2_ctx, root_ctx);

  event = ddk.WaitForEvent();
  ASSERT_EQ(event.type, Ddk::EventType::DEVICE_RELEASED);
  ASSERT_EQ(event.device_ctx, xhci_ctx);

  ASSERT_EQ(root_ctx->mode(), AmlUsbPhy::UsbMode::PERIPHERAL);

  // Switch back to host mode. This will be read by the irq thread.
  USB_R5_V2::Get().FromValue(0).set_iddig_curr(0).WriteTo(&usbctrl_mmio.value());
  // Wake up the irq thread.
  pdev->Interrupt();

  event = ddk.WaitForEvent();
  ASSERT_EQ(event.type, Ddk::EventType::DEVICE_ADDED);
  xhci_ctx = event.device_ctx;
  ASSERT_NE(xhci_ctx, root_ctx);

  event = ddk.WaitForEvent();
  ASSERT_EQ(event.type, Ddk::EventType::DEVICE_RELEASED);
  ASSERT_EQ(event.device_ctx, dwc2_ctx);

  ASSERT_EQ(root_ctx->mode(), AmlUsbPhy::UsbMode::HOST);

  ddk.DestroyDevices(root_device.get());
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
