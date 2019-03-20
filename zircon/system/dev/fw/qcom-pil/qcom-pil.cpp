// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <fbl/alloc_checker.h>
#include <fbl/unique_ptr.h>
#include <zircon/syscalls/smc.h>

#include "qcom-pil.h"

namespace qcom_pil {

zx_status_t PilDevice::Bind() {
    auto status = pdev_.GetSmc(0, &smc_);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s GetSmc failed %d\n", __func__, status);
        return status;
    }
// Used to test communication with QSEE and its replies for different image ids.
//#define TEST_SMC
#ifdef TEST_SMC
    for (int i = 0; i < 16; ++i) {
        zx_smc_parameters_t params = CreatePilSmcParams(Cmd::QuerySupport, CreateScmArgs(1, 0),
                                                        static_cast<PasId>(i));
        zx_smc_result_t result = {};
        zx_smc_call(smc_.get(), &params, &result);
        if (result.arg0 == 0 && result.arg1 == 1) {
            zxlogf(INFO, "%s pas_id %d supported\n", __func__, i);
        }
    }
    for (int i = 0; i < 16; ++i) {
        zx_smc_parameters_t params = CreatePilSmcParams(Cmd::AuthAndReset, CreateScmArgs(1, 0),
                                                        static_cast<PasId>(i),
                                                        static_cast<zx_paddr_t>(0));
        zx_smc_result_t result = {};
        zx_smc_call(smc_.get(), &params, &result);
        zxlogf(INFO, "%s pas_id %d auth and reset reply %ld %ld\n", __func__, i, result.arg0,
               result.arg1);
    }
#endif
    status = DdkAdd("qcom-pil");
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s DdkAdd failed %d\n", __func__, status);
        ShutDown();
        return status;
    }
    return ZX_OK;
}
zx_status_t PilDevice::Init() {
    return ZX_OK;
}

void PilDevice::ShutDown() {
}

void PilDevice::DdkUnbind() {
    ShutDown();
    DdkRemove();
}

void PilDevice::DdkRelease() {
    delete this;
}

zx_status_t PilDevice::Create(zx_device_t* parent) {
    fbl::AllocChecker ac;
    auto dev = fbl::make_unique_checked<PilDevice>(&ac, parent);
    if (!ac.check()) {
        zxlogf(ERROR, "%s PilDevice creation ZX_ERR_NO_MEMORY\n", __func__);
        return ZX_ERR_NO_MEMORY;
    }
    auto status = dev->Bind();
    if (status != ZX_OK) {
        return status;
    }

    // devmgr is now in charge of the memory for dev
    auto ptr = dev.release();
    return ptr->Init();
}

zx_status_t qcom_pil_bind(void* ctx, zx_device_t* parent) {
    return qcom_pil::PilDevice::Create(parent);
}

} // namespace qcom_pil
