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

namespace display {

class Controller;

class Binder : public fake_ddk::Bind {
 public:
  ~Binder() {}

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

  void SetDisplay(fake_display::FakeDisplay* display) { display_ = display; }

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

  void SetUp() override;
  void TearDown() override;

  Binder& ddk() { return ddk_; }
  zx_device_t* parent() { return fake_ddk::kFakeParent; }
  zx_device_t* dc_parent() { return fake_ddk::kFakeParent; }
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
