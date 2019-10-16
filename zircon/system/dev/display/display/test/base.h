// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_DEV_DISPLAY_DISPLAY_TEST_BASE_H_
#define ZIRCON_SYSTEM_DEV_DISPLAY_DISPLAY_TEST_BASE_H_

#include <lib/fake-bti/bti.h>
#include <lib/fake_ddk/fake_ddk.h>
#include <lib/zx/bti.h>

#include <ddk/protocol/composite.h>
#include <ddk/protocol/platform/device.h>
#include <ddk/protocol/sysmem.h>
#include <ddktl/protocol/composite.h>
#include <ddktl/protocol/platform/device.h>
#include <ddktl/protocol/sysmem.h>
#include <fbl/array.h>
#include <zxtest/zxtest.h>

#include "../../fake/fake-display.h"
#include "../controller.h"

namespace display {

class Binder : public fake_ddk::Bind {
 public:
  ~Binder() {
    if (display_ != nullptr) {
      display_->DdkRelease();
    }
  }

  zx_status_t DeviceGetProtocol(const zx_device_t* device, uint32_t proto_id,
                                void* protocol) override {
    auto out = reinterpret_cast<fake_ddk::Protocol*>(protocol);
    if (proto_id == ZX_PROTOCOL_DISPLAY_CONTROLLER_IMPL) {
      const auto& p = display_->dcimpl_proto();
      out->ops = p->ops;
      out->ctx = p->ctx;
      return ZX_OK;
    }
    for (const auto& proto : protocols_) {
      if (proto_id == proto.id) {
        out->ops = proto.proto.ops;
        out->ctx = proto.proto.ctx;
        return ZX_OK;
      }
    }
    return ZX_ERR_NOT_SUPPORTED;
  }

  void SetDisplay(fake_display::FakeDisplay* display) {
    if (display_ && display == nullptr) {
      display_->DdkRelease();
    }
    display_ = display;
  }

  zx_device_t* display() { return display_->zxdev(); }

 private:
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
  TestBase() : composite_(parent()) {}

  void SetUp() override {
    fbl::Array<fake_ddk::ProtocolEntry> protocols(new fake_ddk::ProtocolEntry[3], 3);
    protocols[0] = {ZX_PROTOCOL_COMPOSITE,
                    *reinterpret_cast<const fake_ddk::Protocol*>(composite_.proto())};
    protocols[1] = {ZX_PROTOCOL_PDEV, *reinterpret_cast<const fake_ddk::Protocol*>(pdev_.proto())};
    protocols[2] = {ZX_PROTOCOL_SYSMEM,
                    *reinterpret_cast<const fake_ddk::Protocol*>(sysmem_.proto())};
    ddk_.SetProtocols(std::move(protocols));
    auto display = new fake_display::FakeDisplay(fake_ddk::kFakeParent);
    ASSERT_OK(display->Bind());
    ddk_.SetDisplay(display);

    fbl::unique_ptr<display::Controller> c(new Controller(dc_parent()));
    // Save a copy for test cases.
    controller_ = c.get();
    ASSERT_OK(c->Bind(&c));
  }

  void TearDown() override {
    ddk_.DeviceAsyncRemove(fake_ddk::kFakeDevice);
    ddk_.WaitUntilRemove();
    EXPECT_TRUE(ddk_.Ok());
  }

  Binder& ddk() { return ddk_; }
  zx_device_t* parent() { return fake_ddk::kFakeParent; }
  zx_device_t* dc_parent() { return fake_ddk::kFakeParent; }
  Controller* controller() { return controller_; }

 private:
  Binder ddk_;
  FakeComposite composite_;
  FakePDev pdev_;
  FakeSysmem sysmem_;

  // Valid until test case destruction
  Controller* controller_;
};

}  // namespace display

#endif  // ZIRCON_SYSTEM_DEV_DISPLAY_DISPLAY_TEST_BASE_H_
