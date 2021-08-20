// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_FAKE_FAKE_DISPLAY_DEVICE_TREE_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_FAKE_FAKE_DISPLAY_DEVICE_TREE_H_

#include <fuchsia/hardware/platform/bus/cpp/banjo.h>
#include <fuchsia/hardware/platform/device/c/banjo.h>
#include <fuchsia/hardware/platform/device/cpp/banjo.h>
#include <fuchsia/hardware/sysmem/c/banjo.h>
#include <fuchsia/hardware/sysmem/cpp/banjo.h>
#include <fuchsia/sysmem/c/banjo.h>
#include <lib/async-loop/default.h>
#include <lib/async/dispatcher.h>
#include <lib/ddk/platform-defs.h>
#include <lib/fake_ddk/fake_ddk.h>

#include <map>

#include <ddktl/device.h>

#include "src/devices/bus/testing/fake-pdev/fake-pdev.h"
#include "src/devices/sysmem/drivers/sysmem/driver.h"
#include "src/graphics/display/drivers/display/controller.h"

namespace fake_display {
// Forward declared because the Banjo and FIDL headers conflict for fuchsia.hardware.display
class FakeDisplay;
}  // namespace fake_display

namespace display {

class Controller;

// Helper class for internal use by FakeDisplayDeviceTree, below.
class Binder : public fake_ddk::Bind {
 public:
  ~Binder() override = default;

  class DeviceState {
   public:
    device_add_args_t args = {};
    std::vector<zx_device_t*> children;
  };

  // |fake_ddk::Bind|
  zx_status_t DeviceAdd(zx_driver_t* drv, zx_device_t* parent, device_add_args_t* args,
                        zx_device_t** out) override;

  void RemoveHelper(DeviceState* state);

  // |fake_ddk::Bind|
  void DeviceAsyncRemove(zx_device_t* device) override;

  bool Ok();

  zx::unowned_channel fidl_loop(const zx_device_t* dev) {
    auto iter = fidl_loops_.find(dev);
    if (iter == fidl_loops_.end()) {
      return zx::unowned_channel(0);
    }
    return zx::unowned_channel(iter->second->local().get());
  }

  void ShutdownFIDL() { fidl_loops_.clear(); }

 private:
  std::map<zx_device_t*, DeviceState> devices_;
  std::map<const zx_device_t*, std::unique_ptr<fake_ddk::FidlMessenger>> fidl_loops_;
  zx_device_t* kFakeChild = reinterpret_cast<zx_device_t*>(0xcccc);
  int total_children_ = 0;
  int children_ = 0;
};

// Helper class for internal use by FakeDisplayDeviceTree, below.
class FakePBus : public ddk::PBusProtocol<FakePBus, ddk::base_protocol> {
 public:
  FakePBus() : proto_({&pbus_protocol_ops_, this}) {}
  const pbus_protocol_t* proto() const { return &proto_; }
  zx_status_t PBusDeviceAdd(const pbus_dev_t* dev) { return ZX_ERR_NOT_SUPPORTED; }
  zx_status_t PBusProtocolDeviceAdd(uint32_t proto_id, const pbus_dev_t* dev) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  zx_status_t PBusRegisterProtocol(uint32_t proto_id, const uint8_t* protocol,
                                   size_t protocol_size) {
    return ZX_OK;
  }
  zx_status_t PBusGetBoardInfo(pdev_board_info_t* out_info) { return ZX_ERR_NOT_SUPPORTED; }
  zx_status_t PBusSetBoardInfo(const pbus_board_info_t* info) { return ZX_ERR_NOT_SUPPORTED; }
  zx_status_t PBusSetBootloaderInfo(const pbus_bootloader_info_t* info) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  zx_status_t PBusCompositeDeviceAdd(const pbus_dev_t* dev,
                                     /* const device_fragment_t* */ uint64_t fragments_list,
                                     size_t fragments_count, const char* primary_fragment) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  zx_status_t PBusAddComposite(const pbus_dev_t* dev,
                               /* const device_fragment_t* */ uint64_t fragments_list,
                               size_t fragments_count, const char* primary_fragment) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  zx_status_t PBusRegisterSysSuspendCallback(const pbus_sys_suspend_t* suspend_cbin) {
    return ZX_ERR_NOT_SUPPORTED;
  }

 private:
  pbus_protocol_t proto_;
};

// Clients of FakeDisplayDeviceTree pass a SysmemDeviceWrapper into the constructor to provide a
// sysmem implementation to the display driver, with the goal of supporting the following use cases:
//   - display driver unit tests want to use a self-contained/hermetic sysmem implementation, to
//     improve reliability of test results.
//   - system integration tests may want to use the "global" sysmem so that multiple components
//     can use it to coordinate memory allocation, for example tests which involve Scenic, Magma,
//     and the display driver.
class SysmemDeviceWrapper {
 public:
  virtual ~SysmemDeviceWrapper() = default;

  virtual const sysmem_protocol_t* proto() const = 0;
  virtual const zx_device_t* device() const = 0;
  virtual zx_status_t Bind() = 0;
};

// Convenient implementation of SysmemDeviceWrapper which can be used to wrap both
// sysmem_device::Driver and display::SysmemProxyDevice (the initial two usages of
// SysmemDeviceWrapper).
template <typename T>
class GenericSysmemDeviceWrapper : public SysmemDeviceWrapper {
 public:
  GenericSysmemDeviceWrapper()
      : sysmem_ctx_(std::make_unique<sysmem_driver::Driver>()),
        owned_sysmem_(std::make_unique<T>(fake_ddk::kFakeParent, sysmem_ctx_.get())) {
    sysmem_ = owned_sysmem_.get();
  }

  const sysmem_protocol_t* proto() const override { return sysmem_->proto(); }
  const zx_device_t* device() const override { return sysmem_->device(); }
  zx_status_t Bind() override {
    zx_status_t status = sysmem_->Bind();
    if (status == ZX_OK) {
      // DDK takes ownership of sysmem and DdkRelease will release it.
      owned_sysmem_.release();
    }
    return status;
  }

 private:
  std::unique_ptr<sysmem_driver::Driver> sysmem_ctx_;
  std::unique_ptr<T> owned_sysmem_;
  T* sysmem_{};
};

// FakeDisplayDeviceTree encapusulates the requirements for creating a fake DDK device tree with a
// FakeDisplay device attached to it.
class FakeDisplayDeviceTree {
 public:
  // |sysmem| allows the caller to customize the sysmem implementation used by the
  // FakeDisplayDeviceTree.  See SysmemDeviceWrapper for more details, as well as existing
  // specializations of GenericSysmemDeviceWrapper<>.
  FakeDisplayDeviceTree(std::unique_ptr<SysmemDeviceWrapper> sysmem, bool start_vsync);
  ~FakeDisplayDeviceTree();

  Binder& ddk() { return ddk_; }
  Controller* controller() { return controller_; }
  fake_display::FakeDisplay* display() { return display_; }

  const zx_device_t* sysmem_device() { return sysmem_->device(); }

  void AsyncShutdown();

 private:
  Binder ddk_;
  FakePBus pbus_;
  fake_pdev::FakePDev pdev_;

  std::unique_ptr<SysmemDeviceWrapper> sysmem_;

  // Not owned, FakeDisplay will delete itself on shutdown.
  fake_display::FakeDisplay* display_;

  Controller* controller_;

  bool shutdown_ = false;

  const sysmem_metadata_t sysmem_metadata_ = {
      .vid = PDEV_VID_QEMU,
      .pid = PDEV_PID_QEMU,
      .protected_memory_size = 0,
      .contiguous_memory_size = 0,
  };
};

}  // namespace display

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_FAKE_FAKE_DISPLAY_DEVICE_TREE_H_
