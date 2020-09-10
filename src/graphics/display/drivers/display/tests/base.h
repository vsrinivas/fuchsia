// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_DISPLAY_TESTS_BASE_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_DISPLAY_TESTS_BASE_H_

#include <fuchsia/hardware/display/llcpp/fidl.h>
#include <lib/async-loop/default.h>
#include <lib/fake-bti/bti.h>
#include <lib/fake_ddk/fake_ddk.h>
#include <lib/zx/bti.h>
#include <zircon/device/sysmem.h>

#include <map>
#include <vector>

#include <ddk/platform-defs.h>
#include <ddk/protocol/composite.h>
#include <ddk/protocol/platform/device.h>
#include <ddk/protocol/sysmem.h>
#include <ddktl/device.h>
#include <ddktl/protocol/composite.h>
#include <ddktl/protocol/platform/bus.h>
#include <ddktl/protocol/platform/device.h>
#include <ddktl/protocol/sysmem.h>
#include <fbl/array.h>
#include <zxtest/zxtest.h>

#include "src/devices/sysmem/drivers/sysmem/device.h"
#include "src/devices/sysmem/drivers/sysmem/driver.h"
#include "src/graphics/display/drivers/fake/fake-display.h"

namespace fake_display {
// Forward declared because the Banjo and FIDL headers conflict for fuchsia.hardware.display
class FakeDisplay;
}  // namespace fake_display

namespace display {

class Controller;

class Binder : public fake_ddk::Bind {
 public:
  ~Binder() {}

  class DeviceState {
   public:
    device_add_args_t args = {};
    std::vector<zx_device_t*> children;
  };

  // |fake_ddk::Bind|
  zx_status_t DeviceAdd(zx_driver_t* drv, zx_device_t* parent, device_add_args_t* args,
                        zx_device_t** out) override {
    *out = reinterpret_cast<zx_device_t*>(reinterpret_cast<char*>(kFakeChild) + total_children_);
    children_++;
    total_children_++;
    devices_[parent].children.push_back(*out);
    if (args && args->ops && args->ops->message) {
      auto loop =
          std::make_unique<fake_ddk::FidlMessenger>(&kAsyncLoopConfigNoAttachToCurrentThread);
      loop->SetMessageOp(args->ctx, args->ops->message);
      fidl_loops_.insert({*out, std::move(loop)});
    }

    DeviceState state;
    constexpr device_add_args_t null_args = {};
    state.args = args ? *args : null_args;
    devices_.insert({*out, state});
    return ZX_OK;
  }

  void RemoveHelper(DeviceState* state) {
    if (state->args.ops->unbind) {
      state->args.ops->unbind(state->args.ctx);
    }
    // unbind all children
    for (zx_device_t* dev : state->children) {
      auto child = devices_.find(dev);
      if (child != devices_.end()) {
        RemoveHelper(&child->second);
        children_--;
        devices_.erase(child);
      }
    }
    if (state->args.ops->release) {
      state->args.ops->release(state->args.ctx);
    }
  }

  // |fake_ddk::Bind|
  void DeviceAsyncRemove(zx_device_t* device) override {
    auto state = devices_.find(device);
    if (state == devices_.end()) {
      printf("Unrecognized device %p\n", device);
      return;
    }
    RemoveHelper(&state->second);
    devices_.erase(state);
  }

  // |fake_ddk::Bind|
  zx_status_t DeviceGetProtocol(const zx_device_t* device, uint32_t proto_id,
                                void* protocol) override;

  void SetDisplay(fake_display::FakeDisplay* display) { display_ = display; }

  zx_device_t* display();

  bool Ok() {
    if (devices_.empty()) {
      EXPECT_EQ(children_, 0);
      return children_ == 0;
    } else {
      EXPECT_TRUE(devices_.size() == 1);
      EXPECT_TRUE(devices_.begin()->first == fake_ddk::kFakeParent);
      return devices_.size() == 1 && devices_.begin()->first == fake_ddk::kFakeParent;
    }
  }
  // |fake_ddk::Bind|
  zx_status_t DeviceGetMetadataSize(zx_device_t* dev, uint32_t type, size_t* out_size) override {
    if (type == SYSMEM_METADATA) {
      *out_size = sizeof(sysmem_metadata_);
      return ZX_OK;
    }
    return ZX_ERR_INVALID_ARGS;
  }

  // |fake_ddk::Bind|
  zx_status_t DeviceGetMetadata(zx_device_t* dev, uint32_t type, void* data, size_t length,
                                size_t* actual) override {
    if (type == SYSMEM_METADATA) {
      *actual = sizeof(sysmem_metadata_);
      if (length < *actual) {
        return ZX_ERR_NO_MEMORY;
      }
      *static_cast<sysmem_metadata_t*>(data) = sysmem_metadata_;
      return ZX_OK;
    }
    return ZX_ERR_INVALID_ARGS;
  }

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
  fake_display::FakeDisplay* display_;
  const sysmem_metadata_t sysmem_metadata_ = {
      .vid = PDEV_VID_QEMU,
      .pid = PDEV_PID_QEMU,
      .protected_memory_size = 0,
      .contiguous_memory_size = 0,
  };
};

class FakePBus : public ddk::PBusProtocol<FakePBus, ddk::base_protocol> {
 public:
  FakePBus() : proto_({&pbus_protocol_ops_, this}) {}
  const pbus_protocol_t* proto() const { return &proto_; }
  zx_status_t PBusDeviceAdd(const pbus_dev_t* dev) { return ZX_ERR_NOT_SUPPORTED; }
  zx_status_t PBusProtocolDeviceAdd(uint32_t proto_id, const pbus_dev_t* dev) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  zx_status_t PBusRegisterProtocol(uint32_t proto_id, const void* protocol, size_t protocol_size) {
    return ZX_OK;
  }
  zx_status_t PBusGetBoardInfo(pdev_board_info_t* out_info) { return ZX_ERR_NOT_SUPPORTED; }
  zx_status_t PBusSetBoardInfo(const pbus_board_info_t* info) { return ZX_ERR_NOT_SUPPORTED; }
  zx_status_t PBusSetBootloaderInfo(const pbus_bootloader_info_t* info) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  zx_status_t PBusCompositeDeviceAdd(const pbus_dev_t* dev, const device_fragment_t* fragments_list,
                                     size_t fragments_count, uint32_t coresident_device_index) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  zx_status_t PBusRegisterSysSuspendCallback(const pbus_sys_suspend_t* suspend_cbin) {
    return ZX_ERR_NOT_SUPPORTED;
  }

 private:
  pbus_protocol_t proto_;
};

class FakePDev : public ddk::PDevProtocol<FakePDev, ddk::base_protocol> {
 public:
  FakePDev() : proto_({&pdev_protocol_ops_, this}) {}

  const pdev_protocol_t* proto() const { return &proto_; }

  zx_status_t PDevGetMmio(uint32_t index, pdev_mmio_t* out_mmio) { return ZX_ERR_NOT_SUPPORTED; }

  zx_status_t PDevGetInterrupt(uint32_t index, uint32_t flags, zx::interrupt* out_irq) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  zx_status_t PDevGetBti(uint32_t index, zx::bti* out_bti) {
    return fake_bti_create(out_bti->reset_and_get_address());
  }

  zx_status_t PDevGetSmc(uint32_t index, zx::resource* out_resource) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  zx_status_t PDevGetDeviceInfo(pdev_device_info_t* out_info) { return ZX_ERR_NOT_SUPPORTED; }

  zx_status_t PDevGetBoardInfo(pdev_board_info_t* out_info) { return ZX_ERR_NOT_SUPPORTED; }

 private:
  pdev_protocol_t proto_;
};

class FakeComposite : public ddk::CompositeProtocol<FakeComposite> {
 public:
  explicit FakeComposite(zx_device_t* parent)
      : proto_({&composite_protocol_ops_, this}), parent_(parent) {}

  const composite_protocol_t* proto() const { return &proto_; }

  uint32_t CompositeGetFragmentCount() { return static_cast<uint32_t>(kNumFragments); }

  void CompositeGetFragments(zx_device_t** comp_list, size_t comp_count, size_t* comp_actual) {
    size_t comp_cur;

    for (comp_cur = 0; comp_cur < comp_count; comp_cur++) {
      comp_list[comp_cur] = parent_;
    }

    if (comp_actual != nullptr) {
      *comp_actual = comp_cur;
    }
  }

 private:
  static constexpr size_t kNumFragments = 2;

  composite_protocol_t proto_;
  zx_device_t* parent_;
};

class TestBase : public zxtest::Test {
 public:
  TestBase() : loop_(&kAsyncLoopConfigAttachToCurrentThread), composite_(fake_ddk::kFakeParent) {}

  void SetUp() override;
  void TearDown() override;

  Binder& ddk() { return ddk_; }
  Controller* controller() { return controller_; }
  fake_display::FakeDisplay* display() { return display_; }
  zx::unowned_channel sysmem_fidl();
  zx::unowned_channel display_fidl();

  async_dispatcher_t* dispatcher() { return loop_.dispatcher(); }
  bool RunLoopWithTimeoutOrUntil(fit::function<bool()>&& condition,
                                 zx::duration timeout = zx::sec(1),
                                 zx::duration step = zx::msec(10));

 private:
  async::Loop loop_;
  thrd_t loop_thrd_ = 0;
  Binder ddk_;
  FakeComposite composite_;
  FakePBus pbus_;
  FakePDev pdev_;
  std::unique_ptr<sysmem_driver::Device> sysmem_;
  std::unique_ptr<sysmem_driver::Driver> sysmem_ctx_;
  // Not owned, FakeDisplay will delete itself on shutdown.
  fake_display::FakeDisplay* display_;

  // Valid until test case destruction
  Controller* controller_;
};

}  // namespace display

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_DISPLAY_TESTS_BASE_H_
