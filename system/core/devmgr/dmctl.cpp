// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <string.h>

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddktl/device.h>
#include <fuchsia/device/manager/c/fidl.h>

#include "device-internal.h"

#include <zircon/device/dmctl.h>

using namespace devmgr;

namespace {

class Dmctl;
using DmctlBase = ddk::Device<Dmctl, ddk::Ioctlable, ddk::Writable>;

class Dmctl : public DmctlBase {
public:
    Dmctl(zx_device_t* parent);

    static zx_status_t Bind(void* ctx, zx_device_t* parent);

    void DdkRelease();
    zx_status_t DdkWrite(const void* buf, size_t count, zx_off_t off, size_t* actual);
    zx_status_t DdkIoctl(uint32_t op, const void* in_buf, size_t in_len, void* out_buf,
                         size_t out_len, size_t* out_actual);
};

Dmctl::Dmctl(zx_device_t* parent) : DmctlBase(parent) {
}

zx_status_t Dmctl::Bind(void* ctx, zx_device_t* parent) {
    auto dev = fbl::make_unique<Dmctl>(parent);
    auto status = dev->DdkAdd("dmctl");
    if (status == ZX_OK) {
        // devmgr owns the memory now
        __UNUSED auto ptr = dev.release();
    }
    return status;
}

void Dmctl::DdkRelease() {
    // This driver does not expect to be shut down.
    abort();
}

zx_status_t Dmctl::DdkWrite(const void* buf, size_t count, zx_off_t off, size_t* actual) {
    const zx::channel& rpc = *zxdev()->rpc;
    zx_status_t status, call_status;
    status = fuchsia_device_manager_CoordinatorDmCommand(
            rpc.get(), ZX_HANDLE_INVALID, static_cast<const char*>(buf), count, &call_status);
    if (status != ZX_OK) {
        return status;
    } else if (call_status != ZX_OK) {
        return call_status;
    }
    *actual = count;
    return ZX_OK;
}

zx_status_t Dmctl::DdkIoctl(uint32_t op, const void* in_buf, size_t in_len,
                            void* out_buf, size_t out_len, size_t* out_actual) {
    const zx::channel& rpc = *zxdev()->rpc;
    zx_status_t status, call_status;
    switch (op) {
    case IOCTL_DMCTL_COMMAND: {
        if (in_len != sizeof(dmctl_cmd_t)) {
            return ZX_ERR_INVALID_ARGS;
        }

        dmctl_cmd_t cmd;
        memcpy(&cmd, in_buf, sizeof(cmd));
        cmd.name[sizeof(cmd.name) - 1] = 0;

        status = fuchsia_device_manager_CoordinatorDmCommand(
                rpc.get(), cmd.h, static_cast<const char*>(cmd.name), strlen(cmd.name),
                &call_status);
        if (status != ZX_OK) {
            return status;
        } else if (call_status != ZX_OK) {
            // NOT_SUPPORTED tells the dispatcher to close the handle for
            // ioctls that accept a handle argument, so we have to avoid
            // returning that in this case where the handle has been passed
            // to another process (and effectively closed)
            if (call_status == ZX_ERR_NOT_SUPPORTED) {
                call_status = ZX_ERR_INTERNAL;
            }
            return call_status;
        }

        *out_actual = 0;
        return ZX_OK;
    }
    case IOCTL_DMCTL_OPEN_VIRTCON: {
        if (in_len != sizeof(zx_handle_t)) {
            return ZX_ERR_INVALID_ARGS;
        }
        return fuchsia_device_manager_CoordinatorDmOpenVirtcon(rpc.get(), *(zx_handle_t*)in_buf);
    }
    case IOCTL_DMCTL_WATCH_DEVMGR: {
        if (in_len != sizeof(zx_handle_t)) {
            return ZX_ERR_INVALID_ARGS;
        }
        return fuchsia_device_manager_CoordinatorDmWatch(rpc.get(), *(zx_handle_t*)in_buf);
    }
    case IOCTL_DMCTL_MEXEC: {
        if (in_len != sizeof(dmctl_mexec_args_t)) {
            return ZX_ERR_INVALID_ARGS;
        }
        auto args = reinterpret_cast<const dmctl_mexec_args_t*>(in_buf);
        return fuchsia_device_manager_CoordinatorDmMexec(rpc.get(), args->kernel, args->bootdata);
    }
    default:
        return ZX_ERR_INVALID_ARGS;
    }
}

zx_driver_ops_t dmctl_driver_ops = []() {
    zx_driver_ops_t ops = {};
    ops.version = DRIVER_OPS_VERSION;
    ops.bind = Dmctl::Bind;
    return ops;
}();

} // namespace

ZIRCON_DRIVER_BEGIN(dmctl, dmctl_driver_ops, "zircon", "0.1", 1)
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_MISC_PARENT),
ZIRCON_DRIVER_END(dmctl)
