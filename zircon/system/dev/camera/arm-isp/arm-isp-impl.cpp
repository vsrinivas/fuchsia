// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arm-isp-impl.h"
#include "arm-isp.h"
#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/metadata.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_call.h>
#include <fbl/unique_ptr.h>
#include <hw/reg.h>
#include <memory>
#include <stdint.h>
#include <threads.h>
#include <zircon/types.h>

namespace camera {

zx_status_t ArmISPImplDevice::InitPdev(zx_device_t* parent) {
    // TODO(braval): Implement this.
    return ZX_OK;
}

void ArmISPImplDevice::DdkUnbind() {
    ShutDown();
    DdkRemove();
}

void ArmISPImplDevice::DdkRelease() {
    delete this;
}

void ArmISPImplDevice::ShutDown() {
}

zx_status_t ArmISPImplDevice::IspImplRegisterCallbacks(const isp_callbacks_protocol_t* cbs) {
    if (cbs == nullptr) {
        return ZX_ERR_INVALID_ARGS;
    }

    sensor_callbacks_ = *cbs;

    sync_completion_signal(&cb_registered_signal_);
    return ZX_OK;
}

zx_status_t ArmISPImplDevice::IspImplDeRegisterCallbacks() {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t ArmISPImplDevice::Bind(mipi_adapter_t* mipi_info) {

    zx_device_prop_t props[] = {
        {BIND_PLATFORM_DEV_VID, 0, mipi_info->vid},
        {BIND_PLATFORM_DEV_PID, 0, mipi_info->pid},
        {BIND_PLATFORM_DEV_DID, 0, mipi_info->did},
    };

    device_add_args_t args = {};
    args.version = DEVICE_ADD_ARGS_VERSION;
    args.name = "arm-isp";
    args.ctx = this;
    args.ops = &ddk_device_proto_;
    args.proto_id = ddk_proto_id_;
    args.proto_ops = ddk_proto_ops_;
    args.props = props;
    args.prop_count = countof(props);

    return pdev_.DeviceAdd(0, &args, &zxdev_);
}

int ArmISPImplDevice::WorkerThread() {
    // Note: Need to wait here for all sensors to register
    //       their callbacks before proceeding further.
    //       Currently only supporting single sensor, we can add
    //       support for multiple sensors easily when needed.
    sync_completion_wait(&cb_registered_signal_, ZX_TIME_INFINITE);

    return camera::ArmIspDevice::Create(parent_, sensor_callbacks_);
}

// static
zx_status_t ArmISPImplDevice::Create(zx_device_t* parent) {
    fbl::AllocChecker ac;
    auto isp_impl_device = std::unique_ptr<ArmISPImplDevice>(new (&ac) ArmISPImplDevice(parent));
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    zx_status_t status = isp_impl_device->InitPdev(parent);
    if (status != ZX_OK) {
        return status;
    }

    isp_impl_device->parent_ = parent;

    // Populate mipi specific information
    mipi_adapter_t mipi_info;
    size_t actual;
    status = device_get_metadata(parent, DEVICE_METADATA_PRIVATE,
                                 &mipi_info,
                                 sizeof(mipi_adapter_t),
                                 &actual);
    if (status != ZX_OK || actual != sizeof(mipi_adapter_t)) {
        zxlogf(ERROR, "arm-isp: Could not get Mipi Info metadata %d\n", status);
        return status;
    }

    sync_completion_reset(&isp_impl_device->cb_registered_signal_);
    auto cleanup = fbl::MakeAutoCall([&]() { isp_impl_device->ShutDown(); });

    status = isp_impl_device->Bind(&mipi_info);
    if (status != ZX_OK) {
        zxlogf(ERROR, "arm-isp-impl driver failed to get added\n");
        return status;
    } else {
        zxlogf(INFO, "arm-isp-impl driver added\n");
    }

    auto worker_thunk = [](void* arg) -> int {
        return reinterpret_cast<ArmISPImplDevice*>(arg)->WorkerThread();
    };

    int ret = thrd_create_with_name(&isp_impl_device->worker_thread_, worker_thunk,
                                    reinterpret_cast<void*>(isp_impl_device.get()),
                                    "ispimpl-worker-thread");
    ZX_DEBUG_ASSERT(ret == thrd_success);

    cleanup.cancel();

    // isp_impl_device intentionally leaked as it is now held by DevMgr.
    __UNUSED auto ptr = isp_impl_device.release();

    return status;
}

ArmISPImplDevice::~ArmISPImplDevice() {
}

zx_status_t isp_bind(void* ctx, zx_device_t* device) {
    return camera::ArmISPImplDevice::Create(device);
}

static zx_driver_ops_t driver_ops = []() {
    zx_driver_ops_t ops;
    ops.version = DRIVER_OPS_VERSION;
    ops.bind = isp_bind;
    return ops;
}();

} // namespace camera

// clang-format off
ZIRCON_DRIVER_BEGIN(arm_isp, camera::driver_ops, "arm-isp", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_ARM),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_ISP),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_ARM_MALI_IV009),
ZIRCON_DRIVER_END(arm_isp)
