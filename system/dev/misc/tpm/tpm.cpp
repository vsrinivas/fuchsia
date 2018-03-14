// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/*
 * Very basic TPM driver
 *
 * Assumptions:
 * - The system firmware is responsible for initializing the TPM and has
 *   already done so.
 */

#include <assert.h>
#include <endian.h>
#include <ddk/debug.h>
#include <ddk/driver.h>
#include <ddk/io-buffer.h>
#include <explicit-memory/bytes.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>
#include <fbl/unique_ptr.h>
#include <fbl/unique_free_ptr.h>
#include <zircon/device/i2c.h>
#include <zircon/device/tpm.h>
#include <zircon/types.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <threads.h>

#include "i2c-cr50.h"
#include "tpm.h"
#include "tpm-commands.h"

// This is arbitrary, we just want to limit the size of the response buffer
// that we need to allocate.
#define MAX_RAND_BYTES 256

namespace tpm {

// implement tpm protocol:

static zx_status_t GetRandom(Device* dev, void* buf, uint16_t count, size_t* actual) {
    static_assert(MAX_RAND_BYTES <= UINT32_MAX, "");
    if (count > MAX_RAND_BYTES) {
        count = MAX_RAND_BYTES;
    }

    struct tpm_getrandom_cmd cmd;
    uint32_t resp_len = tpm_init_getrandom(&cmd, count);
    fbl::unique_free_ptr<tpm_getrandom_resp> resp(
            reinterpret_cast<tpm_getrandom_resp*>(malloc(resp_len)));
    size_t actual_read;
    uint16_t bytes_returned;
    if (!resp) {
        return ZX_ERR_NO_MEMORY;
    }

    zx_status_t status = dev->ExecuteCmd(0, (uint8_t*)&cmd, sizeof(cmd),
                                         (uint8_t*)resp.get(), resp_len, &actual_read);
    if (status != ZX_OK) {
        return status;
    }
    if (actual_read < sizeof(*resp) ||
        actual_read != betoh32(resp->hdr.total_len)) {

        return ZX_ERR_BAD_STATE;
    }
    bytes_returned = betoh16(resp->bytes_returned);
    if (actual_read != sizeof(*resp) + bytes_returned ||
        resp->hdr.tag != htobe16(TPM_ST_NO_SESSIONS) ||
        bytes_returned > count ||
        resp->hdr.return_code != htobe32(TPM_SUCCESS)) {

        return ZX_ERR_BAD_STATE;
    }
    memcpy(buf, resp->bytes, bytes_returned);
    mandatory_memset(resp->bytes, 0, bytes_returned);
    *actual = bytes_returned;
    return ZX_OK;
}

zx_status_t Device::ShutdownLocked(uint16_t type) {
    struct tpm_shutdown_cmd cmd;
    uint32_t resp_len = tpm_init_shutdown(&cmd, type);
    struct tpm_shutdown_resp resp;
    size_t actual;

    zx_status_t status = ExecuteCmdLocked(0, (uint8_t*)&cmd, sizeof(cmd),
                                          (uint8_t*)&resp, resp_len, &actual);
    if (status != ZX_OK) {
        return status;
    }
    if (actual < sizeof(resp) ||
        actual != betoh32(resp.hdr.total_len) ||
        resp.hdr.tag != htobe16(TPM_ST_NO_SESSIONS) ||
        resp.hdr.return_code != htobe32(TPM_SUCCESS)) {

        return ZX_ERR_BAD_STATE;
    }
    return ZX_OK;
}

zx_status_t Device::ExecuteCmd(Locality loc, const uint8_t* cmd, size_t len,
                               uint8_t* resp, size_t max_len, size_t* actual) {
    fbl::AutoLock guard(&lock_);
    return ExecuteCmdLocked(loc, cmd, len, resp, max_len, actual);
}

zx_status_t Device::ExecuteCmdLocked(Locality loc, const uint8_t* cmd, size_t len,
                                     uint8_t* resp, size_t max_len, size_t* actual) {
    zx_status_t status = SendCmdLocked(loc, cmd, len);
    if (status != ZX_OK) {
        return status;
    }
    return RecvRespLocked(loc, resp, max_len, actual);
}

void Device::DdkRelease() {
    delete this;
}

zx_status_t Device::DdkIoctl(uint32_t op,
                             const void* in_buf, size_t in_len,
                             void* out_buf, size_t out_len, size_t* out_actual) {
    switch (op) {
        case IOCTL_TPM_SAVE_STATE: {
            fbl::AutoLock guard(&lock_);
            return ShutdownLocked(TPM_SU_STATE);
        }
    }
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Device::DdkSuspend(uint32_t flags) {
    fbl::AutoLock guard(&lock_);

    if (flags == DEVICE_SUSPEND_FLAG_SUSPEND_RAM) {
        zx_status_t status = ShutdownLocked(TPM_SU_STATE);
        if (status != ZX_OK) {
            zxlogf(ERROR, "tpm: Failed to save state: %d\n", status);
            return status;
        }
    }

    zx_status_t status = ReleaseLocalityLocked(0);
    if (status != ZX_OK) {
        zxlogf(ERROR, "tpm: Failed to release locality: %d\n", status);
        return status;
    }
    return status;
}

zx_status_t Device::Bind() {
    zx_status_t status = DdkAdd("tpm", DEVICE_ADD_INVISIBLE);
    if (status != ZX_OK) {
        return status;
    }

    thrd_t thread;
    int ret = thrd_create_with_name(&thread, Init, this, "tpm:slow_bind");
    if (ret != thrd_success) {
        DdkRemove();
        return ZX_ERR_INTERNAL;
    }
    thrd_detach(thread);
    return ZX_OK;
}

zx_status_t Device::Init() {
    uint8_t buf[32] = { 0 };
    size_t bytes_read;

    auto cleanup = fbl::MakeAutoCall([&] {
        DdkRemove();
    });

    zx_status_t status = iface_->Validate();
    if (status != ZX_OK) {
        zxlogf(TRACE, "tpm: did not pass driver validation\n");
        return status;
    }

    {
        fbl::AutoLock guard(&lock_);

        // tpm_request_use will fail if we're not at least 30ms past _TPM_INIT.
        // The system firmware performs the init, so it's safe to assume that
        // is 30 ms past.  If we're on systems where we need to do init,
        // we need to wait up to 30ms for the TPM_ACCESS register to be valid.
        status = RequestLocalityLocked(0);
        if (status != ZX_OK) {
            zxlogf(ERROR, "tpm: Failed to request use: %d\n", status);
            return status;
        }

        status = WaitForLocalityLocked(0);
        if (status != ZX_OK) {
            zxlogf(ERROR, "tpm: Waiting for locality failed: %d\n", status);
            return status;
        }
    }

    DdkMakeVisible();

    // Make a best-effort attempt to give the kernel some more entropy
    // TODO(security): Perform a more recurring seeding
    status = tpm::GetRandom(this, buf, static_cast<uint16_t>(sizeof(buf)), &bytes_read);
    if (status == ZX_OK) {
        zx_cprng_add_entropy(buf, bytes_read);
        mandatory_memset(buf, 0, sizeof(buf));
    } else {
        zxlogf(ERROR, "tpm: Failed to add entropy to kernel CPRNG\n");
    }

    cleanup.cancel();
    return ZX_OK;
}

Device::Device(zx_device_t* parent, fbl::unique_ptr<HardwareInterface> iface)
    : DeviceType(parent), iface_(fbl::move(iface)) {
    ddk_proto_id_ = ZX_PROTOCOL_TPM;
}

Device::~Device() {
}

} // namespace tpm

zx_status_t tpm_bind(void* ctx, zx_device_t* parent) {
    zx::handle irq;
    size_t actual;
    zx_status_t status = device_ioctl(parent, IOCTL_I2C_SLAVE_IRQ, NULL, 0,
                                      irq.reset_and_get_address(), sizeof(zx_handle_t), &actual);
    if (status == ZX_OK && actual != sizeof(zx_handle_t)) {
        // irq contains garbage?
        zx_handle_t ignored __UNUSED = irq.release();
    }

    fbl::unique_ptr<tpm::I2cCr50Interface> i2c_iface;
    status = tpm::I2cCr50Interface::Create(parent, fbl::move(irq), &i2c_iface);
    if (status != ZX_OK) {
        return status;
    }

    fbl::AllocChecker ac;
    fbl::unique_ptr<tpm::Device> device(new (&ac) tpm::Device(parent, fbl::move(i2c_iface)));
    if (!ac.check()) {
        return status;
    }

    status = device->Bind();
    if (status == ZX_OK) {
        // DevMgr now owns this pointer, release it to avoid destroying the
        // object when device goes out of scope.
        __UNUSED auto ptr = device.release();
    }
    return status;
}
