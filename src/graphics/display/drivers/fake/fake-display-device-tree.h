// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_FAKE_FAKE_DISPLAY_DEVICE_TREE_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_FAKE_FAKE_DISPLAY_DEVICE_TREE_H_

#include <fuchsia/hardware/platform/device/c/banjo.h>
#include <fuchsia/hardware/platform/device/cpp/banjo.h>
#include <fuchsia/sysmem/c/banjo.h>
#include <lib/async-loop/default.h>
#include <lib/async/dispatcher.h>
#include <lib/ddk/platform-defs.h>
#include <lib/fake_ddk/fake_ddk.h>

#include <map>

#include <ddktl/device.h>

#include "src/devices/bus/testing/fake-pdev/fake-pdev.h"
#include "src/graphics/display/drivers/display/controller.h"
#include "src/graphics/display/drivers/fake/sysmem-device-wrapper.h"

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
