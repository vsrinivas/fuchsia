// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_DEV_DISPLAY_DISPLAY_TEST_BASE_H_
#define ZIRCON_SYSTEM_DEV_DISPLAY_DISPLAY_TEST_BASE_H_

#include <lib/fake-bti/bti.h>
#include <lib/fake_ddk/fake_ddk.h>
#include <lib/zx/bti.h>

#include <map>
#include <vector>

#include <ddk/protocol/composite.h>
#include <ddk/protocol/platform/device.h>
#include <ddk/protocol/sysmem.h>
#include <ddktl/device.h>
#include <ddktl/protocol/composite.h>
#include <ddktl/protocol/platform/device.h>
#include <ddktl/protocol/sysmem.h>
#include <fbl/array.h>
#include <zxtest/zxtest.h>

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

  zx_status_t DeviceAdd(zx_driver_t* drv, zx_device_t* parent, device_add_args_t* args,
                        zx_device_t** out) override {
    zx_status_t status;
    if (args && args->ops && args->ops->message) {
      if ((status = fidl_.SetMessageOp(args->ctx, args->ops->message)) < 0) {
        return status;
      }
    }
    if (parent == fake_ddk::kFakeParent) {
      *out = fake_ddk::kFakeDevice;
    } else {
      *out = reinterpret_cast<zx_device_t*>(reinterpret_cast<char*>(kFakeChild) + total_children_);
      children_++;
      total_children_++;
      devices_[parent].children.push_back(*out);
    }
    printf("added device %p\n", *out);

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
      printf("removing device %p\n", dev);
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

  void DeviceAsyncRemove(zx_device_t* device) override {
    printf("removing device %p\n", device);

    auto state = devices_.find(device);
    if (state == devices_.end()) {
      printf("Unrecognized device\n");
      return;
    }
    RemoveHelper(&state->second);
    devices_.erase(state);
  }

  zx_status_t DeviceGetProtocol(const zx_device_t* device, uint32_t proto_id,
                                void* protocol) override;

  void SetDisplay(fake_display::FakeDisplay* display) { display_ = display; }

  zx_device_t* display();

  bool Ok() {
    EXPECT_TRUE(devices_.empty());
    EXPECT_EQ(children_, 0);
    return true;
  }

 private:
  std::map<zx_device_t*, DeviceState> devices_;
  zx_device_t* kFakeChild = reinterpret_cast<zx_device_t*>(0xcccc);
  int total_children_ = 0;
  int children_ = 0;
  fake_display::FakeDisplay* display_;
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

class FakeSysmem : public ddk::SysmemProtocol<FakeSysmem> {
 public:
  FakeSysmem() : proto_({&sysmem_protocol_ops_, this}) {}

  const sysmem_protocol_t* proto() const { return &proto_; }

  zx_status_t SysmemConnect(zx::channel allocator2_request) {
    // Currently, do nothing
    return ZX_OK;
  }

  zx_status_t SysmemRegisterHeap(uint64_t heap, zx::channel heap_connection) {
    // Currently, do nothing
    return ZX_OK;
  }

  zx_status_t SysmemRegisterSecureMem(zx::channel tee_connection) {
    // Currently, do nothing
    return ZX_OK;
  }

  zx_status_t SysmemUnregisterSecureMem() {
    // Currently, do nothing
    return ZX_OK;
  }

 private:
  sysmem_protocol_t proto_;
};

class FakeComposite : public ddk::CompositeProtocol<FakeComposite> {
 public:
  explicit FakeComposite(zx_device_t* parent)
      : proto_({&composite_protocol_ops_, this}), parent_(parent) {}

  const composite_protocol_t* proto() const { return &proto_; }

  uint32_t CompositeGetComponentCount() { return static_cast<uint32_t>(kNumComponents); }

  void CompositeGetComponents(zx_device_t** comp_list, size_t comp_count, size_t* comp_actual) {
    size_t comp_cur;

    for (comp_cur = 0; comp_cur < comp_count; comp_cur++) {
      comp_list[comp_cur] = parent_;
    }

    if (comp_actual != nullptr) {
      *comp_actual = comp_cur;
    }
  }

 private:
  static constexpr size_t kNumComponents = 2;

  composite_protocol_t proto_;
  zx_device_t* parent_;
};

class TestBase : public zxtest::Test {
 public:
  TestBase() : composite_(fake_ddk::kFakeParent) {}

  void SetUp() override;
  void TearDown() override;

  Binder& ddk() { return ddk_; }
  Controller* controller() { return controller_; }
  fake_display::FakeDisplay* display() { return display_; }

 private:
  Binder ddk_;
  FakeComposite composite_;
  FakePDev pdev_;
  FakeSysmem sysmem_;
  // Not owned, FakeDisplay will delete itself on shutdown.
  fake_display::FakeDisplay* display_;

  // Valid until test case destruction
  Controller* controller_;
};

}  // namespace display

#endif  // ZIRCON_SYSTEM_DEV_DISPLAY_DISPLAY_TEST_BASE_H_
