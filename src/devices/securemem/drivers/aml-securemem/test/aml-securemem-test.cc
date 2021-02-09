// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/platform/device/c/banjo.h>
#include <fuchsia/hardware/platform/device/cpp/banjo.h>
#include <fuchsia/hardware/sysmem/c/banjo.h>
#include <fuchsia/hardware/sysmem/cpp/banjo.h>
#include <lib/async-loop/default.h>
#include <lib/fake_ddk/fake_ddk.h>
#include <lib/fit/result.h>
#include <lib/zx/interrupt.h>
#include <lib/zx/resource.h>
#include <zircon/limits.h>

#include <fbl/array.h>
#include <zxtest/zxtest.h>

#include "device.h"
#include "src/devices/bus/testing/fake-pdev/fake-pdev.h"

struct Context {
  std::shared_ptr<amlogic_secure_mem::AmlogicSecureMemDevice> dev;
};

class Binder : public fake_ddk::Bind {
  zx_status_t DeviceRemove(zx_device_t* dev) override {
    Context* context = reinterpret_cast<Context*>(dev);
    if (context->dev != nullptr) {
      context->dev->DdkRelease();
    }
    context->dev = nullptr;
    return ZX_OK;
  }

  zx_status_t DeviceAdd(zx_driver_t* drv, zx_device_t* parent, device_add_args_t* args,
                        zx_device_t** out) override {
    *out = parent;
    Context* context = reinterpret_cast<Context*>(parent);
    context->dev.reset(reinterpret_cast<amlogic_secure_mem::AmlogicSecureMemDevice*>(args->ctx));

    if (args && args->ops) {
      if (args->ops->message) {
        zx_status_t status;
        if ((status = fidl_.SetMessageOp(args->ctx, args->ops->message)) < 0) {
          return status;
        }
      }
    }
    return ZX_OK;
  }
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
    // Stash the tee_connection_ so the channel can stay open long enough to avoid a potentially
    // confusing error message during the test.
    tee_connection_ = std::move(tee_connection);
    return ZX_OK;
  }

  zx_status_t SysmemUnregisterSecureMem() {
    // Currently, do nothing
    return ZX_OK;
  }

 private:
  sysmem_protocol_t proto_;
  zx::channel tee_connection_;
};

// We cover the code involved in supporting non-VDEC secure memory and VDEC secure memory in
// sysmem-test, so this fake doesn't really need to do much yet.
class FakeTee : public ddk::TeeProtocol<FakeTee> {
 public:
  FakeTee() : proto_({&tee_protocol_ops_, this}) {}

  const tee_protocol_t* proto() const { return &proto_; }

  zx_status_t TeeConnectToApplication(const uuid_t* application_uuid, zx::channel tee_app_request,
                                      zx::channel service_provider) {
    // Currently, do nothing
    //
    // We don't rely on the tee_app_request channel sticking around for these tests.  See
    // sysmem-test for a test that exercises the tee_app_request channel.
    return ZX_OK;
  }

 private:
  tee_protocol_t proto_;
};

class AmlogicSecureMemTest : public zxtest::Test {
 protected:
  AmlogicSecureMemTest() : loop_(&kAsyncLoopConfigAttachToCurrentThread) {
    pdev_.UseFakeBti();

    static constexpr size_t kNumBindFragments = 3;

    fbl::Array<fake_ddk::FragmentEntry> fragments(new fake_ddk::FragmentEntry[kNumBindFragments],
                                                  kNumBindFragments);
    fragments[0] = pdev_.fragment();
    fragments[1].name = "sysmem";
    fragments[1].protocols.emplace_back(fake_ddk::ProtocolEntry{
        ZX_PROTOCOL_SYSMEM, *reinterpret_cast<const fake_ddk::Protocol*>(sysmem_.proto())});
    fragments[2].name = "tee";
    fragments[2].protocols.emplace_back(fake_ddk::ProtocolEntry{
        ZX_PROTOCOL_TEE, *reinterpret_cast<const fake_ddk::Protocol*>(tee_.proto())});

    ddk_.SetFragments(std::move(fragments));

    ASSERT_OK(amlogic_secure_mem::AmlogicSecureMemDevice::Create(nullptr, parent()));
  }

  void TearDown() override {
    // For now, we use DdkSuspend(mexec) partly to cover DdkSuspend(mexec) handling, and
    // partly because it's the only way of cleaning up safely that we've implemented so far, as
    // aml-securemem doesn't yet implement DdkUnbind() - and arguably it doesn't really need to
    // given what aml-securemem is.

    ddk::SuspendTxn txn(dev()->zxdev(), DEV_POWER_STATE_D3COLD, false, DEVICE_SUSPEND_REASON_MEXEC);
    dev()->DdkSuspend(std::move(txn));
  }

  zx_device_t* parent() { return reinterpret_cast<zx_device_t*>(&ctx_); }

  amlogic_secure_mem::AmlogicSecureMemDevice* dev() { return ctx_.dev.get(); }

 private:
  // Default dispatcher for the test thread.  Not used to actually dispatch in these tests so far.
  async::Loop loop_;
  Binder ddk_;
  fake_pdev::FakePDev pdev_;
  FakeSysmem sysmem_;
  FakeTee tee_;
  Context ctx_ = {};
};

TEST_F(AmlogicSecureMemTest, GetSecureMemoryPhysicalAddressBadVmo) {
  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(ZX_PAGE_SIZE, 0, &vmo));

  ASSERT_TRUE(dev()->GetSecureMemoryPhysicalAddress(std::move(vmo)).is_error());
}
