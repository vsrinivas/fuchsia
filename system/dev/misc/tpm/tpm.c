// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/*
 * Very basic TPM driver
 *
 * Assumptions:
 * - This driver is the sole owner of the TPM hardware.  While the TPM hardware
 *   supports co-ownership, this code does not handle being kicked off the TPM.
 * - The system firmware is responsible for initializing the TPM and has
 *   already done so.
 */

#include <assert.h>
#include <endian.h>
#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/io-buffer.h>
#include <magenta/device/tpm.h>
#include <magenta/syscalls.h>
#include <magenta/types.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <threads.h>

#include "tpm.h"
#include "tpm-commands.h"

#define TPM_PHYS_ADDRESS 0xfed40000
#define TPM_PHYS_LENGTH 0x5000

// This is arbitrary, we just want to limit the size of the response buffer
// that we need to allocate.
#define MAX_RAND_BYTES 256

static mtx_t tpm_lock = MTX_INIT;
void *tpm_base;
mx_handle_t irq_handle;
static io_buffer_t io_buffer;

// implement tpm protocol:

static ssize_t tpm_get_random(mx_device_t* dev, void* buf, size_t count) {
    if (count > MAX_RAND_BYTES) {
        count = MAX_RAND_BYTES;
    }
    struct tpm_getrandom_cmd cmd;
    uint32_t resp_len = tpm_init_getrandom(&cmd, count);
    struct tpm_getrandom_resp *resp = malloc(resp_len);
    if (!resp) {
        return MX_ERR_NO_MEMORY;
    }

    mtx_lock(&tpm_lock);

    mx_status_t status = tpm_send_cmd(LOCALITY0, (uint8_t*)&cmd, sizeof(cmd));
    if (status != MX_OK) {
        goto cleanup;
    }
    status = tpm_recv_resp(LOCALITY0, (uint8_t*)resp, resp_len);
    if (status < 0) {
        goto cleanup;
    }
    if ((uint32_t)status < sizeof(*resp) ||
        (uint32_t)status != betoh32(resp->hdr.total_len)) {

        status = MX_ERR_BAD_STATE;
        goto cleanup;
    }
    uint32_t bytes_returned = betoh32(resp->bytes_returned);
    if ((uint32_t)status != sizeof(*resp) + bytes_returned ||
        resp->hdr.tag != htobe16(TPM_TAG_RSP_COMMAND) ||
        bytes_returned > count ||
        resp->hdr.return_code != htobe32(TPM_SUCCESS)) {

        status = MX_ERR_BAD_STATE;
        goto cleanup;
    }
    memcpy(buf, resp->bytes, bytes_returned);
    memset(resp->bytes, 0, bytes_returned);
    status = bytes_returned;
cleanup:
    free(resp);
    mtx_unlock(&tpm_lock);
    return status;
}

static mx_status_t tpm_save_state(void) {
    struct tpm_savestate_cmd cmd;
    uint32_t resp_len = tpm_init_savestate(&cmd);
    struct tpm_savestate_resp resp;

    mtx_lock(&tpm_lock);

    mx_status_t status = tpm_send_cmd(LOCALITY0, (uint8_t*)&cmd, sizeof(cmd));
    if (status != MX_OK) {
        goto cleanup;
    }
    status = tpm_recv_resp(LOCALITY0, (uint8_t*)&resp, resp_len);
    if (status < 0) {
        goto cleanup;
    }
    if ((uint32_t)status < sizeof(resp) ||
        (uint32_t)status != betoh32(resp.hdr.total_len) ||
        resp.hdr.tag != htobe16(TPM_TAG_RSP_COMMAND) ||
        resp.hdr.return_code != htobe32(TPM_SUCCESS)) {

        status = MX_ERR_BAD_STATE;
        goto cleanup;
    }
    status = MX_OK;
cleanup:
    mtx_unlock(&tpm_lock);
    return status;
}

static mx_status_t tpm_device_ioctl(void* ctx, uint32_t op,
                                    const void* in_buf, size_t in_len,
                                    void* out_buf, size_t out_len, size_t* out_actual) {
    switch (op) {
        case IOCTL_TPM_SAVE_STATE: return tpm_save_state();
    }
    return MX_ERR_NOT_SUPPORTED;
}

// implement device protocol:
static mx_protocol_device_t tpm_device_proto __UNUSED = {
    .version = DEVICE_OPS_VERSION,
    .ioctl = tpm_device_ioctl,
};


//TODO: bind against hw, not misc
mx_status_t tpm_bind(void* ctx, mx_device_t* parent, void** cookie) {
#if defined(__x86_64__) || defined(__i386__)
    mx_status_t status = io_buffer_init_physical(&io_buffer, TPM_PHYS_ADDRESS, TPM_PHYS_LENGTH,
                                                 get_root_resource(), MX_CACHE_POLICY_UNCACHED);
    if (status != MX_OK) {
        return status;
    }
    tpm_base = io_buffer_virt(&io_buffer);

    status = tpm_is_supported(LOCALITY0);
    if (status != MX_OK) {
        return status;
    }

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "tpm",
        .ops = &tpm_device_proto,
        .proto_id = MX_PROTOCOL_TPM,
    };

    mx_device_t* dev;
    status =  device_add(parent, &args, &dev);
    if (status != MX_OK) {
        return status;
    }

    // tpm_request_use will fail if we're not at least 30ms past _TPM_INIT.
    // The system firmware performs the init, so it's safe to assume that
    // is 30 ms past.  If we're on systems where we need to do init,
    // we need to wait up to 30ms for the TPM_ACCESS register to be valid.
    status = tpm_request_use(LOCALITY0);
    if (status != MX_OK) {
        goto cleanup_device;
    }

    status = tpm_wait_for_locality(LOCALITY0);
    if (status != MX_OK) {
        goto cleanup_device;
    }

    // Configure interrupts
    status = tpm_set_irq(LOCALITY0, 10);
    if (status != MX_OK) {
        goto cleanup_device;
    }

    status = mx_interrupt_create(get_root_resource(), 10, MX_FLAG_REMAP_IRQ, &irq_handle);
    if (status != MX_OK) {
        goto cleanup_device;
    }

    status = tpm_enable_irq_type(LOCALITY0, IRQ_DATA_AVAIL);
    if (status < 0) {
        goto cleanup_device;
    }
    status = tpm_enable_irq_type(LOCALITY0, IRQ_LOCALITY_CHANGE);
    if (status < 0) {
        goto cleanup_device;
    }

    // Make a best-effort attempt to give the kernel some more entropy
    // TODO(security): Perform a more recurring seeding
    uint8_t buf[32] = { 0 };
    ssize_t bytes_read = tpm_get_random(dev, buf, sizeof(buf));
    if (bytes_read > 0) {
        mx_cprng_add_entropy(buf, bytes_read);
        memset(buf, 0, sizeof(buf));
    }

    return MX_OK;

cleanup_device:
    if (irq_handle > 0) {
        mx_handle_close(irq_handle);
    }
    device_remove(dev);
    return status;
#else
    return MX_ERR_NOT_SUPPORTED;
#endif
}

static mx_driver_ops_t tpm_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = tpm_bind,
};

MAGENTA_DRIVER_BEGIN(tpm, tpm_driver_ops, "magenta", "0.1", 1)
    BI_MATCH_IF(EQ, BIND_PROTOCOL, MX_PROTOCOL_MISC_PARENT),
MAGENTA_DRIVER_END(tpm)
