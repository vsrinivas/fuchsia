// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fake-display-device-tree.h"

#include <lib/async/cpp/task.h>

#include <zxtest/zxtest.h>

#include "src/graphics/display/drivers/fake/fake-display.h"

namespace display {

zx_status_t Binder::DeviceAdd(zx_driver_t* drv, zx_device_t* parent, device_add_args_t* args,
                              zx_device_t** out) {
  *out = reinterpret_cast<zx_device_t*>(reinterpret_cast<char*>(kFakeChild) + total_children_);
  children_++;
  total_children_++;
  devices_[parent].children.push_back(*out);
  if (args && args->ops && args->ops->message) {
    auto loop = std::make_unique<fake_ddk::FidlMessenger>(&kAsyncLoopConfigNoAttachToCurrentThread);
    loop->SetMessageOp(args->ctx, args->ops->message);
    fidl_loops_.insert({*out, std::move(loop)});
  }

  DeviceState state;
  constexpr device_add_args_t null_args = {};
  state.args = args ? *args : null_args;
  devices_.insert({*out, state});
  return ZX_OK;
}

void Binder::RemoveHelper(DeviceState* state) {
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

void Binder::DeviceAsyncRemove(zx_device_t* device) {
  auto state = devices_.find(device);
  if (state == devices_.end()) {
    printf("Unrecognized device %p\n", device);
    return;
  }
  RemoveHelper(&state->second);
  devices_.erase(state);
}

bool Binder::Ok() {
  if (devices_.empty()) {
    EXPECT_EQ(children_, 0);
    return children_ == 0;
  } else {
    EXPECT_TRUE(devices_.size() == 1);
    EXPECT_TRUE(devices_.begin()->first == fake_ddk::kFakeParent);
    return devices_.size() == 1 && devices_.begin()->first == fake_ddk::kFakeParent;
  }
}

FakeDisplayDeviceTree::FakeDisplayDeviceTree(std::unique_ptr<SysmemDeviceWrapper> sysmem,
                                             bool start_vsync)
    : sysmem_(std::move(sysmem)) {
  pdev_.UseFakeBti();
  ddk_.SetMetadata(SYSMEM_METADATA_TYPE, &sysmem_metadata_, sizeof(sysmem_metadata_));

  // Protocols for sysmem
  ddk_.SetProtocol(ZX_PROTOCOL_PBUS, pbus_.proto());
  ddk_.SetProtocol(ZX_PROTOCOL_PDEV, pdev_.proto());

  EXPECT_OK(sysmem_->Bind());

  // Fragments for fake-display
  fbl::Array<fake_ddk::FragmentEntry> fragments(new fake_ddk::FragmentEntry[2], 2);
  fragments[0] = pdev_.fragment();
  fragments[1].name = "sysmem";
  fragments[1].protocols.emplace_back(fake_ddk::ProtocolEntry{
      ZX_PROTOCOL_SYSMEM, *reinterpret_cast<const fake_ddk::Protocol*>(sysmem_->proto())});

  ddk_.SetFragments(std::move(fragments));

  display_ = new fake_display::FakeDisplay(fake_ddk::kFakeParent);
  ASSERT_OK(display_->Bind(start_vsync));

  // Protocols for display controller.
  ddk_.SetProtocol(ZX_PROTOCOL_DISPLAY_CONTROLLER_IMPL, display_->dcimpl_proto());
  ddk_.SetProtocol(ZX_PROTOCOL_DISPLAY_CAPTURE_IMPL, display_->capture_proto());
  ddk_.SetProtocol(ZX_PROTOCOL_DISPLAY_CLAMP_RGB_IMPL, display_->clamp_rgbimpl_proto());

  std::unique_ptr<display::Controller> c(new Controller(fake_ddk::kFakeParent));
  // Save a copy for test cases.
  controller_ = c.get();
  ASSERT_OK(c->Bind(&c));
}

FakeDisplayDeviceTree::~FakeDisplayDeviceTree() {
  // AsyncShutdown() must be called before ~FakeDisplayDeviceTree().
  ZX_ASSERT(shutdown_);
}

void FakeDisplayDeviceTree::AsyncShutdown() {
  if (shutdown_) {
    // AsyncShutdown() was already called.
    return;
  }
  shutdown_ = true;

  // FIDL loops must be destroyed first to avoid races between cleanup tasks and loop_.
  ddk_.ShutdownFIDL();

  display_->DdkChildPreRelease(controller_);
  controller_->DdkAsyncRemove();
  display_->DdkAsyncRemove();
  ddk_.DeviceAsyncRemove(const_cast<zx_device_t*>(sysmem_->device()));
}

}  // namespace display
