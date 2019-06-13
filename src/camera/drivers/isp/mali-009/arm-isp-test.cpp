// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arm-isp-test.h"
#include "arm-isp.h"

#include <ddk/debug.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>
#include <fuchsia/camera/test/c/fidl.h>
#include <zircon/fidl.h>

namespace camera {

zx_status_t ArmIspDeviceTester::Create(ArmIspDevice* isp, fit::callback<void()>* on_isp_unbind) {
    fbl::AllocChecker ac;
    auto isp_test_device = fbl::make_unique_checked<ArmIspDeviceTester>(&ac, isp, isp->zxdev());
    if (!ac.check()) {
        zxlogf(ERROR, "%s: Unable to start ArmIspDeviceTester \n", __func__);
        return ZX_ERR_NO_MEMORY;
    }

    *on_isp_unbind = fit::bind_member(isp_test_device.get(), &ArmIspDeviceTester::Disconnect);

    zx_status_t status = isp_test_device->DdkAdd("arm-isp-tester");
    if (status != ZX_OK) {
        zxlogf(ERROR, "Could not create arm-isp-tester device: %d\n", status);
        return status;
    } else {
        zxlogf(INFO, "arm-isp: Added arm-isp-tester device\n");
    }

    // isp_test_device intentionally leaked as it is now held by DevMgr.
    __UNUSED auto ptr = isp_test_device.release();

    return status;
}

// Methods required by the ddk.
void ArmIspDeviceTester::DdkRelease() {
    delete this;
}

void ArmIspDeviceTester::DdkUnbind() {
    DdkRemove();
}

void ArmIspDeviceTester::Disconnect() {
    fbl::AutoLock guard(&isp_lock_);
    isp_ = nullptr;
}

zx_status_t ArmIspDeviceTester::DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn) {
    return fuchsia_camera_test_IspTester_dispatch(this, txn, msg, &isp_tester_ops);
}

void ArmIspDeviceTester::TestWriteRegister(fuchsia_camera_test_TestReport& report) {
    // We'll enable then disable the global debug register:
    fbl::AutoLock guard(&isp_lock_);
    IspGlobalDbg::Get()
        .ReadFrom(&(isp_->isp_mmio_))
        .set_mode_en(1)
        .WriteTo(&(isp_->isp_mmio_));
    ArmIspRegisterDump after_enable = isp_->DumpRegisters();
    IspGlobalDbg::Get()
        .ReadFrom(&(isp_->isp_mmio_))
        .set_mode_en(0)
        .WriteTo(&(isp_->isp_mmio_));
    ArmIspRegisterDump after_disable = isp_->DumpRegisters();
    uint32_t offset = IspGlobalDbg::Get().addr() / 4; // divide by 4 to get the word address.

    report.test_count += 2;
    if (after_enable.global_config[offset] != 1) {
        zxlogf(ERROR, "%s Global debug was not enabled!\n", __func__);
        report.failure_count++;
    } else {
        report.success_count++;
    }
    if (after_disable.global_config[offset] != 0) {
        zxlogf(ERROR, "%s Global debug was not disabled!\n", __func__);
        report.failure_count++;
    } else {
        report.success_count++;
    }
}

// DDKMessage Helper Functions.
zx_status_t ArmIspDeviceTester::RunTests(fidl_txn_t* txn) {
    fuchsia_camera_test_TestReport report = {1, 0, 0};
    fbl::AutoLock guard(&isp_lock_);
    if (!isp_) {
        return ZX_ERR_BAD_STATE;
    }
    if (isp_->RunTests() == ZX_OK) {
        report.success_count++;
    } else {
        report.failure_count++;
    }
    TestWriteRegister(report);
    return fuchsia_camera_test_IspTesterRunTests_reply(txn, ZX_OK, &report);
}

} // namespace camera
