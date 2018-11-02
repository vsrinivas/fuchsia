// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <string.h>

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddktl/device.h>

#include "device-internal.h"
#include "devhost-shared.h"

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

private:
    zx_status_t Command(Message::Op op, const char* cmd, size_t cmdlen, zx_handle_t* h,
                        uint32_t hcount);
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
    zx_status_t status = Command(Message::Op::kDmCommand, static_cast<const char*>(buf), count,
                                 nullptr, 0);
    if (status >= 0) {
        *actual = count;
        status = ZX_OK;
    }
    return status;
}

zx_status_t Dmctl::DdkIoctl(uint32_t op, const void* in_buf, size_t in_len,
                            void* out_buf, size_t out_len, size_t* out_actual) {
    switch (op) {
    case IOCTL_DMCTL_COMMAND: {
        if (in_len != sizeof(dmctl_cmd_t)) {
            return ZX_ERR_INVALID_ARGS;
        }
        dmctl_cmd_t cmd;
        memcpy(&cmd, in_buf, sizeof(cmd));
        cmd.name[sizeof(cmd.name) - 1] = 0;
        *out_actual = 0;
        zx_status_t status = Command(Message::Op::kDmCommand, cmd.name, strlen(cmd.name),
                                     &cmd.h, (cmd.h != ZX_HANDLE_INVALID) ? 1 : 0);
        // NOT_SUPPORTED tells the dispatcher to close the handle for
        // ioctls that accept a handle argument, so we have to avoid
        // returning that in this case where the handle has been passed
        // to another process (and effectively closed)
        if (status == ZX_ERR_NOT_SUPPORTED) {
            status = ZX_ERR_INTERNAL;
        }
        return status;
    }
    case IOCTL_DMCTL_OPEN_VIRTCON:
        if (in_len != sizeof(zx_handle_t)) {
            return ZX_ERR_INVALID_ARGS;
        }
        return Command(Message::Op::kDmOpenVirtcon, nullptr, 0, ((zx_handle_t*) in_buf), 1);
    case IOCTL_DMCTL_WATCH_DEVMGR:
        if (in_len != sizeof(zx_handle_t)) {
            return ZX_ERR_INVALID_ARGS;
        }
        return Command(Message::Op::kDmWatch, nullptr, 0, ((zx_handle_t*) in_buf), 1);
    case IOCTL_DMCTL_MEXEC:
        if (in_len != sizeof(dmctl_mexec_args_t)) {
            return ZX_ERR_INVALID_ARGS;
        }
        return Command(Message::Op::kDmMexec, nullptr, 0, ((zx_handle_t*) in_buf), 2);
    default:
        return ZX_ERR_INVALID_ARGS;
    }
}

zx_status_t Dmctl::Command(Message::Op op, const char* cmd, size_t cmdlen,
                           zx_handle_t* h, uint32_t hcount) {
    Message msg;
    uint32_t msglen;
    if (dc_msg_pack(&msg, &msglen, cmd, cmdlen, nullptr, nullptr) < 0) {
        return ZX_ERR_INVALID_ARGS;
    }
    msg.op = op;
    Status rsp;
    return dc_msg_rpc(zxdev()->rpc.get(), &msg, msglen, h, hcount, &rsp, sizeof(rsp), nullptr,
                      nullptr);
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
