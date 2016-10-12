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
#include <ddk/protocol/tpm.h>
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

mtx_t tpm_lock = MTX_INIT;
void *tpm_base;
mx_handle_t irq_handle;

// implement tpm protocol:

static ssize_t tpm_get_random(mx_device_t* dev, void* buf, size_t count) {
    if (count > MAX_RAND_BYTES) {
        count = MAX_RAND_BYTES;
    }
    struct tpm_getrandom_cmd cmd;
    uint32_t resp_len = tpm_init_getrandom(&cmd, count);
    struct tpm_getrandom_resp *resp = malloc(resp_len);
    if (!resp) {
        return ERR_NO_MEMORY;
    }

    mtx_lock(&tpm_lock);

    mx_status_t status = tpm_send_cmd(LOCALITY0, (uint8_t*)&cmd, sizeof(cmd));
    if (status != NO_ERROR) {
        goto cleanup;
    }
    status = tpm_recv_resp(LOCALITY0, (uint8_t*)resp, resp_len);
    if (status < 0) {
        goto cleanup;
    }
    if ((uint32_t)status < sizeof(*resp) ||
        (uint32_t)status != betoh32(resp->hdr.total_len)) {

        status = ERR_BAD_STATE;
        goto cleanup;
    }
    uint32_t bytes_returned = betoh32(resp->bytes_returned);
    if ((uint32_t)status != sizeof(*resp) + bytes_returned ||
        resp->hdr.tag != htobe16(TPM_TAG_RSP_COMMAND) ||
        bytes_returned > count ||
        resp->hdr.return_code != htobe32(TPM_SUCCESS)) {

        status = ERR_BAD_STATE;
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

static mx_status_t tpm_save_state(mx_device_t *dev) {
    struct tpm_savestate_cmd cmd;
    uint32_t resp_len = tpm_init_savestate(&cmd);
    struct tpm_savestate_resp resp;

    mtx_lock(&tpm_lock);

    mx_status_t status = tpm_send_cmd(LOCALITY0, (uint8_t*)&cmd, sizeof(cmd));
    if (status != NO_ERROR) {
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

        status = ERR_BAD_STATE;
        goto cleanup;
    }
    status = NO_ERROR;
cleanup:
    mtx_unlock(&tpm_lock);
    return status;
}

static mx_protocol_tpm_t tpm_proto = {
    .get_random = tpm_get_random,
    .save_state = tpm_save_state,
};

static ssize_t tpm_device_ioctl(mx_device_t* dev, uint32_t op,
                             const void* in_buf, size_t in_len,
                             void* out_buf, size_t out_len) {
    switch (op) {
        case IOCTL_TPM_SAVE_STATE: return tpm_save_state(dev);
    }
    return ERR_NOT_SUPPORTED;
}

// implement device protocol:
static mx_protocol_device_t tpm_device_proto = {
    .ioctl = tpm_device_ioctl,
};

// implement driver object:

mx_status_t tpm_init(mx_driver_t* driver) {
#if defined(__x86_64__) || defined(__i386__)
    mx_status_t status = mx_mmap_device_memory(
            get_root_resource(),
            TPM_PHYS_ADDRESS, TPM_PHYS_LENGTH,
            MX_CACHE_POLICY_UNCACHED, &tpm_base);
    if (status != NO_ERROR) {
        return status;
    }

    mx_device_t* dev;
    status = device_create(&dev, driver, "tpm", &tpm_device_proto);
    if (status != NO_ERROR) {
        return status;
    }
    dev->protocol_id = MX_PROTOCOL_TPM;
    dev->protocol_ops = &tpm_proto;

    status = device_add(dev, driver_get_misc_device());
    if (status != NO_ERROR) {
        free(dev);
        return status;
    }

    // tpm_request_use will fail if we're not at least 30ms past _TPM_INIT.
    // The system firmware performs the init, so it's safe to assume that
    // is 30 ms past.  If we're on systems where we need to do init,
    // we need to wait up to 30ms for the TPM_ACCESS register to be valid.
    status = tpm_request_use(LOCALITY0);
    if (status != NO_ERROR) {
        goto cleanup_device;
    }

    status = tpm_wait_for_locality(LOCALITY0);
    if (status != NO_ERROR) {
        goto cleanup_device;
    }

    // Configure interupts
    status = tpm_set_irq(LOCALITY0, 10);
    if (status != NO_ERROR) {
        goto cleanup_device;
    }

    irq_handle = mx_interrupt_create(get_root_resource(), 10, MX_FLAG_REMAP_IRQ);
    if (irq_handle < 0) {
        status = irq_handle;
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

    return NO_ERROR;

cleanup_device:
    if (irq_handle > 0) {
        mx_handle_close(irq_handle);
    }
    device_remove(dev);
    free(dev);
    return status;
#else
    tpm_proto = tpm_proto;
    tpm_device_proto = tpm_device_proto;
    return ERR_NOT_SUPPORTED;
#endif
}

mx_driver_t _driver_tpm = {
    .ops = {
        .init = tpm_init,
    },
};

MAGENTA_DRIVER_BEGIN(_driver_tpm, "tpm", "magenta", "0.1", 0)
MAGENTA_DRIVER_END(_driver_tpm)
