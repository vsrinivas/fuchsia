// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_FAKE_MOCK_DISPLAY_DEVICE_TREE_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_FAKE_MOCK_DISPLAY_DEVICE_TREE_H_

#include <fuchsia/sysmem/c/banjo.h>
#include <lib/async-loop/loop.h>
#include <lib/ddk/platform-defs.h>

#include <ddktl/device.h>

#include "src/devices/bus/testing/fake-pdev/fake-pdev.h"
#include "src/devices/sysmem/drivers/sysmem/device.h"
#include "src/graphics/display/drivers/display/controller.h"
#include "src/graphics/display/drivers/fake/fake-display.h"
#include "src/graphics/display/drivers/fake/sysmem-device-wrapper.h"

namespace display {

// MockDisplayDeviceTree encapusulates the requirements for creating a fake DDK device tree with a
// FakeDisplay device attached to it.
//
// It is equivalent to FakeDisplayDeviceTree but uses mock-ddk instead of the deprecated fake_ddk
// library.
class MockDisplayDeviceTree {
 public:
  // |sysmem| allows the caller to customize the sysmem implementation used by the
  // FakeDisplayDeviceTree.  See SysmemDeviceWrapper for more details, as well as existing
  // specializations of GenericSysmemDeviceWrapper<>.
  MockDisplayDeviceTree(std::shared_ptr<zx_device> mock_root,
                        std::unique_ptr<SysmemDeviceWrapper> sysmem, bool start_vsync);
  ~MockDisplayDeviceTree();

  Controller* controller() { return controller_; }
  fake_display::FakeDisplay* display() { return display_; }

  const zx_device_t* sysmem_device() { return sysmem_->device(); }

  zx::unowned_channel display_client();
  fidl::UnownedClientEnd<fuchsia_sysmem::DriverConnector> sysmem_client();

  void AsyncShutdown();

 private:
  fidl::ClientEnd<fuchsia_io::Directory> SetUpPDevFidlServer();

  std::shared_ptr<zx_device> mock_root_;

  // Display device tree needs two platform-device nodes, one as the parent of sysmem and one as a
  // fragment of the composite parent of display. The latter uses Banjo while the former uses FIDL.
  fake_pdev::FakePDevFidl pdev_fidl_;
  fake_pdev::FakePDev pdev_banjo_;

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

  async::Loop display_loop_{&kAsyncLoopConfigNeverAttachToThread};
  async::Loop sysmem_loop_{&kAsyncLoopConfigNeverAttachToThread};
  async::Loop pdev_loop_{&kAsyncLoopConfigNeverAttachToThread};
  component::OutgoingDirectory outgoing_;

  fidl::WireSyncClient<fuchsia_hardware_display::Provider> display_provider_client_;
  fidl::WireSyncClient<fuchsia_sysmem::DriverConnector> sysmem_client_;
};

}  // namespace display

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_FAKE_MOCK_DISPLAY_DEVICE_TREE_H_
