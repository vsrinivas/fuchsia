// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "nand.h"
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include <bits/limits.h>
#include <ddk/debug.h>

#include <sync/completion.h>
#include <zircon/assert.h>
#include <zircon/status.h>

#include <string.h>

// The code in this file is only used for testing, the ioctl() is the entry
// point into this code, called by the nand driver's unit test.
static void nandtest_complete(nand_op_t* nand_op, zx_status_t status) {
    nand_op->command = status;
    completion_signal((completion_t*)nand_op->cookie);
}

static zx_status_t nand_test_get_info(nand_device_t* dev, void* reply, size_t max,
                                      size_t* out_actual) {
    nandtest_resp_t* resp_hdr;

    if (max < sizeof(nand_info_t) + sizeof(resp_hdr)) {
        zxlogf(ERROR, "%s: Bad response length\n", __func__);
        return ZX_ERR_BUFFER_TOO_SMALL;
    }
    resp_hdr = (nandtest_resp_t*)reply;
    resp_hdr->status = ZX_OK;
    nand_info_t nand_info;
    size_t nand_op_size_out;

    dev->nand_proto.ops->query(dev, &nand_info, &nand_op_size_out);
    memcpy((uint8_t*)reply + sizeof(nandtest_resp_t), &nand_info, sizeof(nand_info_t));
    *out_actual = sizeof(nand_info_t) + sizeof(resp_hdr);
    return ZX_OK;
}

static zx_status_t nand_test_read(nand_device_t* dev, const void* cmd, size_t cmd_length,
                                  void* reply, size_t max_reply_sz, size_t* out_actual) {
    nand_info_t nand_info;
    size_t nand_op_size_out;
    nand_io_t nand_io;
    nand_op_t* nand_op = &nand_io.nand_op;
    nandtest_rw_page_data_oob_t* cmd_read_page;
    nandtest_resp_t resp_hdr;
    zx_handle_t vmo_data;
    zx_handle_t vmo_oob;
    zx_status_t status;
    bool do_data = false;
    bool do_oob = false;

    // Sanity check sizes of cmd and resp buffers.
    dev->nand_proto.ops->query(dev, &nand_info, &nand_op_size_out);
    if (cmd_length < sizeof(nandtest_rw_page_data_oob_t)) {
        zxlogf(ERROR, "%s: Bad cmd length\n", __func__);
        return ZX_ERR_BUFFER_TOO_SMALL;
    }
    if (max_reply_sz < sizeof(resp_hdr)) {
        zxlogf(ERROR, "%s: Bad response length\n", __func__);
        return ZX_ERR_BUFFER_TOO_SMALL;
    }
    cmd_read_page = (nandtest_rw_page_data_oob_t*)cmd;
    vmo_data = cmd_read_page->data;
    vmo_oob = cmd_read_page->oob;
    if (cmd_read_page->data_len > 0) {
        if (cmd_read_page->data_len != 1) {
            zxlogf(ERROR, "%s: Bad cmd data_len %u\n", __func__, cmd_read_page->data_len);
            return ZX_ERR_INVALID_ARGS;
        }
        do_data = true;
    }
    if (cmd_read_page->oob_len > 0) {
        if (cmd_read_page->oob_len != nand_info.oob_size) {
            zxlogf(ERROR, "%s: Bad cmd oob_len %u\n", __func__, cmd_read_page->oob_len);
            return ZX_ERR_INVALID_ARGS;
        }
        do_oob = true;
    }

    completion_t completion = COMPLETION_INIT;

    nand_op->command = NAND_OP_READ;
    nand_op->rw.offset_nand = cmd_read_page->nand_page;
    nand_op->rw.length = 1;
    nand_op->rw.offset_data_vmo = 0;
    nand_op->rw.offset_oob_vmo = 0;

    nand_op->rw.data_vmo = do_data ? vmo_data : ZX_HANDLE_INVALID;
    nand_op->rw.oob_vmo = do_oob ? vmo_oob : ZX_HANDLE_INVALID;

    nand_op->completion_cb = nandtest_complete;
    nand_op->cookie = &completion;

    // Queue the data read op and wait for response.
    dev->nand_proto.ops->queue(dev, nand_op);
    completion_wait(&completion, ZX_TIME_INFINITE);

    resp_hdr.status = nand_op->command; // Status stored here by callback.
    status = resp_hdr.status;
    memcpy(reply, &resp_hdr, sizeof(resp_hdr));
    *out_actual = sizeof(resp_hdr);
    if (do_data) {
        zx_handle_close(vmo_data);
    }
    if (do_oob) {
        zx_handle_close(vmo_oob);
    }
    return status;
}

static zx_status_t nand_test_write(nand_device_t* dev, const void* cmd, size_t cmd_length,
                                   void* reply, size_t max_reply_sz, size_t* out_actual) {
    nand_info_t nand_info;
    size_t nand_op_size_out;
    nand_io_t nand_io;
    nand_op_t* nand_op = &nand_io.nand_op;
    nandtest_rw_page_data_oob_t* cmd_write_page;
    nandtest_resp_t resp_hdr;
    zx_handle_t vmo_data, vmo_oob;
    zx_status_t status;
    bool do_data = false;
    bool do_oob = false;

    // Sanity check sizes of cmd and resp buffers.
    dev->nand_proto.ops->query(dev, &nand_info, &nand_op_size_out);
    if (cmd_length < sizeof(nandtest_rw_page_data_oob_t)) {
        zxlogf(ERROR, "%s: Bad cmd length\n", __func__);
        return ZX_ERR_BUFFER_TOO_SMALL;
    }
    cmd_write_page = (nandtest_rw_page_data_oob_t*)cmd;
    if (cmd_write_page->data_len > 0) {
        if (cmd_write_page->data_len != 1) {
            zxlogf(ERROR, "%s: Bad cmd data_len %u\n", __func__, cmd_write_page->data_len);
            return ZX_ERR_INVALID_ARGS;
        }
        do_data = true;
    }
    vmo_data = cmd_write_page->data;
    vmo_oob = cmd_write_page->oob;
    if (cmd_write_page->oob_len > 0) {
        if (cmd_write_page->oob_len != nand_info.oob_size) {
            zxlogf(ERROR, "%s: Bad cmd oob_len %u\n", __func__, cmd_write_page->oob_len);
            return ZX_ERR_INVALID_ARGS;
        }
        do_oob = true;
    }
    if (cmd_length < sizeof(nandtest_rw_page_data_oob_t)) {
        zxlogf(ERROR, "%s: Bad cmd length\n", __func__);
        return ZX_ERR_BUFFER_TOO_SMALL;
    }
    if (max_reply_sz < sizeof(resp_hdr)) {
        zxlogf(ERROR, "%s: Bad response length\n", __func__);
        return ZX_ERR_BUFFER_TOO_SMALL;
    }
    *out_actual = sizeof(resp_hdr);

    completion_t completion = COMPLETION_INIT;

    // Create nand_op.
    nand_op->command = NAND_OP_WRITE;
    nand_op->rw.offset_nand = cmd_write_page->nand_page;
    nand_op->rw.length = 1;
    nand_op->rw.offset_data_vmo = 0;
    nand_op->rw.offset_oob_vmo = 0;

    nand_op->rw.data_vmo = do_data ? vmo_data : ZX_HANDLE_INVALID;
    nand_op->rw.oob_vmo = do_oob ? vmo_oob : ZX_HANDLE_INVALID;

    nand_op->completion_cb = nandtest_complete;
    nand_op->cookie = &completion;

    // Queue the data read op and wait for response.
    dev->nand_proto.ops->queue(dev, nand_op);
    completion_wait(&completion, ZX_TIME_INFINITE);

    resp_hdr.status = nand_op->command; // Status stored here by callback.
    status = resp_hdr.status;
    memcpy(reply, &resp_hdr, sizeof(resp_hdr));
    *out_actual = sizeof(resp_hdr);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: Got error back from PAGE write (%d)\n", __func__, status);
        status = ZX_OK; // Error code is in response header.
    }
    if (do_data) {
        zx_handle_close(vmo_data);
    }
    if (do_oob) {
        zx_handle_close(vmo_oob);
    }
    return status;
}

static zx_status_t nand_test_erase_block(nand_device_t* dev, const void* cmd,
                                         size_t cmd_length, void* reply, size_t max_reply_sz,
                                         size_t* out_actual) {
    nand_info_t nand_info;
    size_t nand_op_size_out;
    nand_io_t nand_io;
    nand_op_t* nand_op = &nand_io.nand_op;
    nandtest_cmd_erase_block_t* cmd_erase_block;
    nandtest_resp_t resp_hdr;

    // Sanity check sizes of cmd and resp buffers.
    dev->nand_proto.ops->query(dev, &nand_info, &nand_op_size_out);
    if (cmd_length < sizeof(nandtest_cmd_erase_block_t)) {
        zxlogf(ERROR, "%s: Bad cmd length\n", __func__);
        return ZX_ERR_BUFFER_TOO_SMALL;
    }
    cmd_erase_block = (nandtest_cmd_erase_block_t*)cmd;
    if (max_reply_sz < sizeof(resp_hdr)) {
        zxlogf(ERROR, "%s: Bad response buffer length\n", __func__);
        return ZX_ERR_BUFFER_TOO_SMALL;
    }

    completion_t completion = COMPLETION_INIT;

    // Create nand_op.
    nand_op->command = NAND_OP_ERASE;
    nand_op->erase.first_block = cmd_erase_block->nandblock;
    nand_op->erase.num_blocks = 1;
    nand_op->completion_cb = nandtest_complete;
    nand_op->cookie = &completion;

    // Queue the data read op and wait for response.
    dev->nand_proto.ops->queue(dev, nand_op);
    completion_wait(&completion, ZX_TIME_INFINITE);

    resp_hdr.status = nand_op->command; // Status stored here by callback.
    memcpy(reply, &resp_hdr, sizeof(resp_hdr));
    *out_actual = sizeof(resp_hdr);
    return ZX_OK;
}

// nand_ioctl() is *only* for testing purposes. This allows a userspace process
// to test reads/writes/erases down into the top level nand driver.
//
// The nand_page/length/data buffer are passed in via the ioctl. The ioctl code
// will create and prep a vmo, allocate a nand_op and queue the nand_op to the
// nand device. For read/write/erase, it will block until signalled by the
// completion callback and then return back status from the ioctl.
zx_status_t nand_ioctl(void* ctx, uint32_t op, const void* cmd, size_t cmd_length, void* reply,
                       size_t max_reply_sz, size_t* out_actual) {
    nand_device_t* dev = ctx;

    switch (op) {
    // Construct and send a query command to the nand driver,
    // and report back the nand_info_t and nand_op_size_out.
    case IOCTL_NAND_GET_NAND_INFO:
        return nand_test_get_info(dev, reply, max_reply_sz, out_actual);

    // Read data + oob for a single page.
    case IOCTL_NAND_READ_PAGE_DATA_OOB:
        return nand_test_read(dev, cmd, cmd_length, reply, max_reply_sz, out_actual);

    // Write data + oob for a single page.
    case IOCTL_NAND_WRITE_PAGE_DATA_OOB:
        return nand_test_write(dev, cmd, cmd_length, reply, max_reply_sz, out_actual);

    // Construct and queue a ERASE command (for the block range) to the
    // nand driver, and send back the status.
    case IOCTL_NAND_ERASE_BLOCK:
        return nand_test_erase_block(dev, cmd, cmd_length, reply, max_reply_sz, out_actual);

    default:
        return ZX_ERR_NOT_SUPPORTED;
    }
    return ZX_OK;
}
