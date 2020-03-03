// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/sysmem/c/fidl.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/fake-bti/bti.h>
#include <lib/fake_ddk/fake_ddk.h>
#include <lib/zx/bti.h>
#include <stdlib.h>
#include <zircon/device/sysmem.h>

#include <memory>

#include <ddktl/device.h>
#include <ddktl/protocol/amlogiccanvas.h>
#include <ddktl/protocol/composite.h>
#include <ddktl/protocol/platform/device.h>
#include <ddktl/protocol/sysmem.h>
#include <mock-mmio-reg/mock-mmio-reg.h>
#include <zxtest/zxtest.h>

#include "../device_ctx.h"
#include "src/media/drivers/amlogic_encoder/registers.h"

namespace {

class FakePDev : public ddk::PDevProtocol<FakePDev, ddk::base_protocol> {
 public:
  FakePDev() : proto_({&pdev_protocol_ops_, this}) {
    zx::interrupt::create(zx::resource(ZX_HANDLE_INVALID), 0, ZX_INTERRUPT_VIRTUAL, &irq_);
  }

  const pdev_protocol_t* proto() const { return &proto_; }

  zx_status_t PDevGetMmio(uint32_t index, pdev_mmio_t* out_mmio) { return ZX_OK; }

  zx_status_t PDevGetInterrupt(uint32_t index, uint32_t flags, zx::interrupt* out_irq) {
    irq_signaller_ = zx::unowned_interrupt(irq_);
    *out_irq = std::move(irq_);
    return ZX_OK;
  }

  zx_status_t PDevGetBti(uint32_t index, zx::bti* out_bti) {
    zx_handle_t fake_bti;
    zx_status_t status = fake_bti_create(&fake_bti);
    if (status != ZX_OK) {
      return status;
    }

    *out_bti = zx::bti(fake_bti);
    return status;
  }

  zx_status_t PDevGetSmc(uint32_t index, zx::resource* out_resource) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  zx_status_t PDevGetDeviceInfo(pdev_device_info_t* out_info) {
    out_info->pid = PDEV_PID_AMLOGIC_T931;
    return ZX_OK;
  }

  zx_status_t PDevGetBoardInfo(pdev_board_info_t* out_info) { return ZX_ERR_NOT_SUPPORTED; }

 private:
  pdev_protocol_t proto_;
  zx::interrupt irq_;
  zx::unowned_interrupt irq_signaller_;
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

class FakeCanvas : public ddk::AmlogicCanvasProtocol<FakeCanvas> {
 public:
  FakeCanvas() : proto_({&amlogic_canvas_protocol_ops_, this}) {}

  zx_status_t AmlogicCanvasConfig(zx::vmo vmo, size_t offset, const canvas_info_t* info,
                                  uint8_t* out_canvas_idx) {
    return ZX_OK;
  }
  zx_status_t AmlogicCanvasFree(uint8_t canvas_idx) { return ZX_OK; }

  const amlogic_canvas_protocol_t* proto() const { return &proto_; }

 private:
  amlogic_canvas_protocol_t proto_;
};

class Ddk : public fake_ddk::Bind {
 public:
  Ddk() {}
  bool added() { return add_called_; }
  const device_add_args_t& args() { return add_args_; }

 private:
  zx_status_t DeviceAdd(zx_driver_t* drv, zx_device_t* parent, device_add_args_t* args,
                        zx_device_t** out) override {
    zx_status_t status = fake_ddk::Bind::DeviceAdd(drv, parent, args, out);
    if (status != ZX_OK) {
      return status;
    }
    add_args_ = *args;
    return ZX_OK;
  }
  device_add_args_t add_args_;
};

class AmlogicEncoderTest : public zxtest::Test {
 protected:
  AmlogicEncoderTest() : loop_(&kAsyncLoopConfigAttachToCurrentThread) {
    ddk::PDevProtocolClient pdev(pdev_.proto());
    ddk::AmlogicCanvasProtocolClient canvas(canvas_.proto());
    ddk::SysmemProtocolClient sysmem(sysmem_.proto());

    ddk_mock::MockMmioRegRegion mock_mmio(nullptr, 32, 0);
    CbusRegisterIo cbus(mock_mmio.GetMmioBuffer());
    DosRegisterIo dosbus(mock_mmio.GetMmioBuffer());
    AoRegisterIo aobus(mock_mmio.GetMmioBuffer());
    HiuRegisterIo hiubus(mock_mmio.GetMmioBuffer());

    zx::interrupt irq;
    pdev_.PDevGetInterrupt(0, 0, &irq);

    zx::bti bti;
    pdev_.PDevGetBti(0, &bti);

    auto device_ctx = std::make_unique<DeviceCtx>(
        fake_ddk::kFakeParent, pdev, canvas, sysmem, std::move(cbus), std::move(dosbus),
        std::move(aobus), std::move(hiubus), std::move(irq), std::move(bti));

    auto status = device_ctx->Bind();
    ASSERT_OK(status);

    // The device_ctx* is now owned by the fake_ddk.
    device_ctx.release();
    // pull it out of device_add args
    device_.reset(static_cast<DeviceCtx*>(ddk_.args().ctx));
  }

  void TearDown() override {
    auto device = device_.release();
    if (device) {
      ddk::UnbindTxn txn(device->zxdev());
      device->DdkUnbindNew(std::move(txn));
    }
    ASSERT_TRUE(ddk_.Ok());
  }

  DeviceCtx* dev() { return device_.get(); }

 private:
  async::Loop loop_;
  Ddk ddk_;
  FakePDev pdev_;
  std::unique_ptr<DeviceCtx> device_;
  FakeSysmem sysmem_;
  FakeCanvas canvas_;
};

TEST_F(AmlogicEncoderTest, Lifecycle) {}

}  // namespace
