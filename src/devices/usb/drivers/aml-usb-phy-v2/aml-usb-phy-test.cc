// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "aml-usb-phy.h"

#include <fuchsia/hardware/platform/device/c/banjo.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <lib/ddk/metadata.h>
#include <lib/fake_ddk/fake_ddk.h>
#include <lib/zx/clock.h>
#include <lib/zx/interrupt.h>
#include <zircon/errors.h>
#include <zircon/syscalls.h>

#include <list>
#include <memory>
#include <queue>
#include <thread>

#include <fake-mmio-reg/fake-mmio-reg.h>
#include <fbl/auto_lock.h>
#include <fbl/condition_variable.h>
#include <fbl/mutex.h>
#include <zxtest/zxtest.h>

#include "src/devices/registers/testing/mock-registers/mock-registers.h"
#include "src/devices/usb/drivers/aml-usb-phy-v2/aml_usb_phy_bind.h"
#include "usb-phy-regs.h"

struct zx_device : std::enable_shared_from_this<zx_device> {
  std::list<std::shared_ptr<zx_device>> devices;
  std::weak_ptr<zx_device> parent;
  std::vector<zx_device_prop_t> props;
  fake_ddk::Protocol ops;
  zx_protocol_device_t dev_ops;
  virtual ~zx_device() = default;
};

namespace aml_usb_phy {

enum class RegisterIndex : size_t {
  Control = 0,
  Phy0 = 1,
  Phy1 = 2,
};

constexpr auto kRegisterBanks = 3;
constexpr auto kRegisterCount = 2048;

class FakePDev : public ddk::PDevProtocol<FakePDev, ddk::base_protocol> {
 public:
  FakePDev() : pdev_({&pdev_protocol_ops_, this}) {
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

  const pdev_protocol_t* proto() const { return &pdev_; }

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

  ~FakePDev() {}

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

  zx_status_t DeviceAdd(zx_driver_t* drv, zx_device_t* parent, device_add_args_t* args,
                        zx_device_t** out) override {
    auto dev = std::make_shared<zx_device>();
    dev->ops.ctx = args->ctx;
    dev->ops.ops = args->proto_ops;
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
      dev->dev_ops.init(dev->ops.ctx);
    }
    return ZX_OK;
  }

  // Schedules a device to be unbound and released.
  // If the test expects this to be called, it should wait for the corresponding DEVICE_RELEASED
  // event.
  void DeviceAsyncRemove(zx_device_t* device) override {
    // Run this in a new thread to simulate the asynchronous nature.
    std::thread t([&, device] {
      // Call the unbind hook. When unbind replies, |DeviceRemove| will handle
      // unbinding and releasing the children, then releasing the device itself.
      if (device->dev_ops.unbind) {
        device->dev_ops.unbind(device->ops.ctx);
      } else {
        // The unbind hook has not been implemented, so we can reply to the unbind immediately.
        DeviceRemove(device);
      }
    });
    async_remove_threads_.push_back(std::move(t));
  }

  // Called once unbind replies.
  zx_status_t DeviceRemove(zx_device_t* device) override {
    // Unbind and release all children.
    DestroyDevices(device);

    auto parent = device->parent.lock();
    if (parent && parent->dev_ops.child_pre_release) {
      parent->dev_ops.child_pre_release(parent->ops.ctx, device->ops.ctx);
    }

    device->dev_ops.release(device->ops.ctx);

    fbl::AutoLock lock(&events_lock_);
    events_.push(Event{EventType::DEVICE_RELEASED, device->ops.ctx});

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
        dev->dev_ops.unbind(dev->ops.ctx);
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

  void JoinAsyncRemoveThreads() {
    for (auto& t : async_remove_threads_) {
      if (t.joinable()) {
        t.join();
      }
    }
    async_remove_threads_.clear();
  }

 private:
  fbl::Mutex events_lock_;
  fbl::ConditionVariable events_signal_ __TA_GUARDED(events_lock_);
  std::queue<Event> events_;

  std::vector<std::thread> async_remove_threads_;
};

// Fixture that supports tests of AmlUsbPhy::Create.
class AmlUsbPhyTest : public zxtest::Test {
 public:
  AmlUsbPhyTest() : root_device_(std::make_shared<zx_device_t>()) {
    static constexpr uint32_t kMagicNumbers[8] = {};
    ddk_.SetMetadata(DEVICE_METADATA_PRIVATE, &kMagicNumbers, sizeof(kMagicNumbers));

    static constexpr size_t kNumBindFragments = 2;
    loop_.StartThread();
    registers_device_ = std::make_unique<mock_registers::MockRegistersDevice>(loop_.dispatcher());

    fbl::Array<fake_ddk::FragmentEntry> fragments(new fake_ddk::FragmentEntry[kNumBindFragments],
                                                  kNumBindFragments);
    fragments[0].name = "pdev";
    ;
    fragments[0].protocols.emplace_back(fake_ddk::ProtocolEntry{
        ZX_PROTOCOL_PDEV, *reinterpret_cast<const fake_ddk::Protocol*>(pdev_.proto())});
    fragments[1].name = "register-reset";
    fragments[1].protocols.emplace_back(fake_ddk::ProtocolEntry{
        ZX_PROTOCOL_REGISTERS,
        *reinterpret_cast<const fake_ddk::Protocol*>(registers_device_->proto())});
    ddk_.SetFragments(std::move(fragments));

    registers()->ExpectWrite<uint32_t>(RESET1_LEVEL_OFFSET, aml_registers::USB_RESET1_LEVEL_MASK,
                                       aml_registers::USB_RESET1_LEVEL_MASK);
    registers()->ExpectWrite<uint32_t>(RESET1_REGISTER_OFFSET,
                                       aml_registers::USB_RESET1_REGISTER_UNKNOWN_1_MASK,
                                       aml_registers::USB_RESET1_REGISTER_UNKNOWN_1_MASK);
    registers()->ExpectWrite<uint32_t>(RESET1_REGISTER_OFFSET,
                                       aml_registers::USB_RESET1_REGISTER_UNKNOWN_2_MASK,
                                       aml_registers::USB_RESET1_REGISTER_UNKNOWN_2_MASK);
    registers()->ExpectWrite<uint32_t>(RESET1_REGISTER_OFFSET,
                                       aml_registers::USB_RESET1_REGISTER_UNKNOWN_2_MASK,
                                       aml_registers::USB_RESET1_REGISTER_UNKNOWN_2_MASK);
    ASSERT_OK(AmlUsbPhy::Create(nullptr, parent()));
  }

  void TearDown() override {
    EXPECT_OK(registers()->VerifyAll());

    ddk_.DestroyDevices(parent());
    ddk_.JoinAsyncRemoveThreads();

    loop_.Shutdown();
  }

  zx_device_t* parent() { return reinterpret_cast<zx_device_t*>(root_device_.get()); }

  mock_registers::MockRegisters* registers() { return registers_device_->fidl_service(); }

 protected:
  async::Loop loop_{&kAsyncLoopConfigNeverAttachToThread};
  std::shared_ptr<zx_device> root_device_;
  Ddk ddk_;
  FakePDev pdev_;
  std::unique_ptr<mock_registers::MockRegistersDevice> registers_device_;
};

TEST_F(AmlUsbPhyTest, SetMode) {
  // The aml-usb-phy device should be added.
  auto event = ddk_.WaitForEvent();
  ASSERT_EQ(event.type, Ddk::EventType::DEVICE_ADDED);
  auto root_ctx = static_cast<AmlUsbPhy*>(event.device_ctx);

  // Wait for host mode to be set by the irq thread. This should add the xhci child device.
  event = ddk_.WaitForEvent();
  ASSERT_EQ(event.type, Ddk::EventType::DEVICE_ADDED);
  auto xhci_ctx = event.device_ctx;
  ASSERT_NE(xhci_ctx, root_ctx);
  ASSERT_EQ(root_ctx->mode(), AmlUsbPhy::UsbMode::HOST);

  ddk::PDev client(pdev_.proto());
  std::optional<ddk::MmioBuffer> usbctrl_mmio;
  ASSERT_OK(client.MapMmio(0, &usbctrl_mmio));

  // Switch to peripheral mode. This will be read by the irq thread.
  USB_R5_V2::Get().FromValue(0).set_iddig_curr(1).WriteTo(&usbctrl_mmio.value());
  // Wake up the irq thread.
  pdev_.Interrupt();

  event = ddk_.WaitForEvent();
  ASSERT_EQ(event.type, Ddk::EventType::DEVICE_ADDED);
  auto dwc2_ctx = event.device_ctx;
  ASSERT_NE(dwc2_ctx, root_ctx);

  event = ddk_.WaitForEvent();
  ASSERT_EQ(event.type, Ddk::EventType::DEVICE_RELEASED);
  ASSERT_EQ(event.device_ctx, xhci_ctx);

  ASSERT_EQ(root_ctx->mode(), AmlUsbPhy::UsbMode::PERIPHERAL);

  // Switch back to host mode. This will be read by the irq thread.
  USB_R5_V2::Get().FromValue(0).set_iddig_curr(0).WriteTo(&usbctrl_mmio.value());
  // Wake up the irq thread.
  pdev_.Interrupt();

  event = ddk_.WaitForEvent();
  ASSERT_EQ(event.type, Ddk::EventType::DEVICE_ADDED);
  xhci_ctx = event.device_ctx;
  ASSERT_NE(xhci_ctx, root_ctx);

  event = ddk_.WaitForEvent();
  ASSERT_EQ(event.type, Ddk::EventType::DEVICE_RELEASED);
  ASSERT_EQ(event.device_ctx, dwc2_ctx);

  ASSERT_EQ(root_ctx->mode(), AmlUsbPhy::UsbMode::HOST);
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
