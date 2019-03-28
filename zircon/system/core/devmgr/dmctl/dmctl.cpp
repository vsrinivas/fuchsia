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
#include <lib/zx/socket.h>
#include <lib/zx/vmo.h>

#include "../devhost/zx-device.h"

namespace {

class Dmctl;
using DmctlBase = ddk::Device<Dmctl, ddk::Messageable, ddk::Writable>;

class Dmctl : public DmctlBase {
public:
    Dmctl(zx_device_t* parent);

    static zx_status_t Bind(void* ctx, zx_device_t* parent);

    void DdkRelease();
    zx_status_t DdkWrite(const void* buf, size_t count, zx_off_t off, size_t* actual);
    zx_status_t DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn);
};

Dmctl::Dmctl(zx_device_t* parent) : DmctlBase(parent) {}

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

static zx_status_t fidl_ExecuteCommand(void* ctx, zx_handle_t raw_log_socket,
                                       const char* command_data, size_t command_size,
                                       fidl_txn_t* txn) {
    zx::socket log_socket(raw_log_socket);
    auto zxdev = static_cast<zx_device_t*>(ctx);
    const zx::channel& rpc = *zxdev->rpc;

    zx_status_t status, call_status;
    status = fuchsia_device_manager_CoordinatorDmCommand(rpc.get(), log_socket.release(),
                                                         command_data, command_size, &call_status);
    if (status == ZX_OK) {
        status = call_status;
    }
    return fuchsia_device_manager_ExternalControllerExecuteCommand_reply(txn, status);
}

static zx_status_t fidl_OpenVirtcon(void* ctx, zx_handle_t raw_vc_receiver) {
    zx::channel vc_receiver(raw_vc_receiver);
    auto zxdev = static_cast<zx_device_t*>(ctx);
    const zx::channel& rpc = *zxdev->rpc;

    return fuchsia_device_manager_CoordinatorDmOpenVirtcon(rpc.get(), vc_receiver.release());
}

static zx_status_t fidl_PerformMexec(void* ctx, zx_handle_t raw_kernel, zx_handle_t raw_bootdata) {
    zx::vmo kernel(raw_kernel);
    zx::vmo bootdata(raw_bootdata);
    auto zxdev = static_cast<zx_device_t*>(ctx);
    const zx::channel& rpc = *zxdev->rpc;

    return fuchsia_device_manager_CoordinatorDmMexec(rpc.get(), kernel.release(),
                                                     bootdata.release());
}

static fuchsia_device_manager_ExternalController_ops_t fidl_ops = {
    .ExecuteCommand = fidl_ExecuteCommand,
    .OpenVirtcon = fidl_OpenVirtcon,
    .PerformMexec = fidl_PerformMexec,
};

zx_status_t Dmctl::DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn) {
    return fuchsia_device_manager_ExternalController_dispatch(zxdev(), txn, msg, &fidl_ops);
}

zx_driver_ops_t dmctl_driver_ops = []() {
    zx_driver_ops_t ops = {};
    ops.version = DRIVER_OPS_VERSION;
    ops.bind = Dmctl::Bind;
    return ops;
}();

} // namespace

ZIRCON_DRIVER_BEGIN(dmctl, dmctl_driver_ops, "zircon", "0.1", 1)
BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_MISC_PARENT), ZIRCON_DRIVER_END(dmctl)
