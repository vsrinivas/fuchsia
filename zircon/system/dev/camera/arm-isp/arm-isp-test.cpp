// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arm-isp-test.h"
#include "arm-isp.h"

#include <ddk/debug.h>
#include <fbl/alloc_checker.h>
#include <fuchsia/camera/test/c/fidl.h>
#include <zircon/fidl.h>

namespace camera {

zx_status_t ArmIspDeviceTester::Create(ArmIspDevice* isp) {
    fbl::AllocChecker ac;
    auto isp_test_device = fbl::make_unique_checked<ArmIspDeviceTester>(&ac, isp, isp->parent());
    if (!ac.check()) {
        zxlogf(ERROR, "%s: Unable to start ArmIspDeviceTester \n", __func__);
        return ZX_ERR_NO_MEMORY;
    }

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

zx_status_t ArmIspDeviceTester::DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn) {
    return fuchsia_camera_test_IspTester_dispatch(this, txn, msg, &isp_tester_ops);
}

// DDKMessage Helper Functions.
zx_status_t ArmIspDeviceTester::RunTests(fidl_txn_t* txn) {
    fuchsia_camera_test_TestReport report = {1, 0, 0};
    if (isp_->RunTests() == ZX_OK) {
        report.success_count++;
    } else {
        report.failure_count++;
    }
    return fuchsia_camera_test_IspTesterRunTests_reply(txn, ZX_OK, &report);
}

} // namespace camera
