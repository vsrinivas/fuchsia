// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Standard Includes
#include <endian.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <threads.h>
#include <stdbool.h>

// DDK Includes
#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/debug.h>
#include <ddk/protocol/platform/device.h>
#include <ddk/protocol/sdmmc.h>
#include <ddk/trace/event.h>

// Zircon Includes
#include <lib/sync/completion.h>
#include <pretty/hexdump.h>
#include <zircon/assert.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/threads.h>
#include <zircon/device/block.h>

#include "sdmmc.h"

#define SDMMC_TXN_RECEIVED          ZX_EVENT_SIGNALED
#define SDMMC_SHUTDOWN              ZX_USER_SIGNAL_0
#define SDMMC_SHUTDOWN_DONE         ZX_USER_SIGNAL_1
#define SDMMC_ADD_MMC_CHILD_DONE    ZX_USER_SIGNAL_2

#define SDMMC_LOCK(dev)   mtx_lock(&(dev)->lock);
#define SDMMC_UNLOCK(dev) mtx_unlock(&(dev)->lock);

#define BLOCK_OP(op)    ((op) & BLOCK_OP_MASK)

// block io transactions. one per client request
typedef struct sdmmc_txn {
    block_op_t bop;
    list_node_t node;
    block_impl_queue_callback completion_cb;
    void* cookie;
} sdmmc_txn_t;

static void block_complete(sdmmc_txn_t* txn, zx_status_t status, trace_async_id_t async_id) {
    const block_op_t* bop = &txn->bop;
    if (txn->completion_cb) {
        // If tracing is not enabled this is a no-op.
        TRACE_ASYNC_END("sdmmc","sdmmc_do_txn", async_id,
            "command", TA_INT32(bop->rw.command),
            "extra", TA_INT32(bop->rw.extra),
            "length", TA_INT32(bop->rw.length),
            "offset_vmo", TA_INT64(bop->rw.offset_vmo),
            "offset_dev", TA_INT64(bop->rw.offset_dev),
            "txn_status", TA_INT32(status));
        txn->completion_cb(txn->cookie, status, &txn->bop);
    } else {
        zxlogf(TRACE, "sdmmc: block op %p completion_cb unset!\n", bop);
    }
}

static zx_off_t sdmmc_get_size(void* ctx) {
    sdmmc_device_t* dev = ctx;
    return dev->block_info.block_count * dev->block_info.block_size;
}

static void sdmmc_unbind(void* ctx) {
    sdmmc_device_t* dev = ctx;
    SDMMC_LOCK(dev);
    if (dev->dead) {
        //Already in middle of release.
        SDMMC_UNLOCK(dev);
        return;
    }
    dev->dead = true;
    SDMMC_UNLOCK(dev);
    device_remove(dev->zxdev);
}

static void sdmmc_release(void* ctx) {
    sdmmc_device_t* dev = ctx;
    SDMMC_LOCK(dev);
    dev->dead = true;
    bool worker_thread_started = dev->worker_thread_started;
    SDMMC_UNLOCK(dev);

    if (worker_thread_started) {
        //Wait until the probe is done.If we know the type
        //of the device, we can act accordingly.
        uint32_t pending;
        zx_object_wait_one(dev->worker_event, SDMMC_ADD_MMC_CHILD_DONE | SDMMC_SHUTDOWN_DONE,
                           ZX_TIME_INFINITE, &pending);
        if (pending & SDMMC_SHUTDOWN_DONE) {
            thrd_join(dev->worker_thread, NULL);
        } else if (pending & SDMMC_ADD_MMC_CHILD_DONE) {
            zx_object_signal(dev->worker_event, 0, SDMMC_SHUTDOWN);
            zx_object_wait_one(dev->worker_event, SDMMC_SHUTDOWN_DONE,
                               ZX_TIME_INFINITE, NULL);
            thrd_join(dev->worker_thread, NULL);
        }
    } else {
        goto exit;
    }

    // error out all pending requests
    trace_async_id_t async_id = dev->async_id;
    sdmmc_txn_t* txn = NULL;
    SDMMC_LOCK(dev);
    list_for_every_entry(&dev->txn_list, txn, sdmmc_txn_t, node) {
        SDMMC_UNLOCK(dev);
        block_complete(txn, ZX_ERR_BAD_STATE, async_id);
        SDMMC_LOCK(dev);
    }
    SDMMC_UNLOCK(dev);

    if (dev->child_zxdev != NULL) {
        device_remove(dev->child_zxdev);
    }

exit:
    if (dev->worker_event != ZX_HANDLE_INVALID) {
        zx_handle_close(dev->worker_event);
    }

    free(dev);
}

static zx_protocol_device_t sdmmc_block_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .get_size = sdmmc_get_size,
};

static zx_protocol_device_t sdmmc_sdio_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .get_size = sdmmc_get_size,
};

// Device protocol.
static zx_protocol_device_t sdmmc_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .unbind = sdmmc_unbind,
    .release = sdmmc_release,
};

static void sdmmc_query(void* ctx, block_info_t* info_out, size_t* block_op_size_out) {
    sdmmc_device_t* dev = ctx;
    memcpy(info_out, &dev->block_info, sizeof(*info_out));
    *block_op_size_out = sizeof(sdmmc_txn_t);
}

static void sdmmc_queue(void* ctx, block_op_t* btxn, block_impl_queue_callback completion_cb,
                        void* cookie) {
    sdmmc_device_t* dev = ctx;
    sdmmc_txn_t* txn = containerof(btxn, sdmmc_txn_t, bop);
    txn->completion_cb = completion_cb;
    txn->cookie = cookie;
    SDMMC_LOCK(dev);
    trace_async_id_t async_id = dev->async_id;
    SDMMC_UNLOCK(dev);

    switch (BLOCK_OP(btxn->command)) {
    case BLOCK_OP_READ:
    case BLOCK_OP_WRITE: {
        SDMMC_LOCK(dev);
        uint64_t max = dev->block_info.block_count;
        SDMMC_UNLOCK(dev);
        if ((btxn->rw.offset_dev >= max) || ((max - btxn->rw.offset_dev) < btxn->rw.length)) {
            block_complete(txn, ZX_ERR_OUT_OF_RANGE, async_id);
            return;
        }
        if (btxn->rw.length == 0) {
            block_complete(txn, ZX_OK, async_id);
            return;
        }
        break;
    }
    case BLOCK_OP_FLUSH:
        // queue the flush op. because there is no out of order execution in this
        // driver, when this op gets processed all previous ops are complete.
        break;
    default:
        block_complete(txn, ZX_ERR_NOT_SUPPORTED, async_id);
        return;
    }

    SDMMC_LOCK(dev);

    list_add_tail(&dev->txn_list, &txn->node);
    // Wake up the worker thread (while locked, so they don't accidentally
    // clear the event).
    zx_object_signal(dev->worker_event, 0, SDMMC_TXN_RECEIVED);

    SDMMC_UNLOCK(dev);
}

// Block protocol
static block_impl_protocol_ops_t block_proto = {
    .query = sdmmc_query,
    .queue = sdmmc_queue,
};

// SDIO protocol
static sdio_protocol_ops_t sdio_proto = {
    .enable_fn = sdio_enable_function,
    .disable_fn = sdio_disable_function,
    .enable_fn_intr = sdio_enable_interrupt,
    .disable_fn_intr = sdio_disable_interrupt,
    .update_block_size = sdio_modify_block_size,
    .get_block_size = sdio_get_cur_block_size,
    .do_rw_txn = sdio_rw_data,
    .do_rw_byte = sdio_rw_byte,
    .get_dev_hw_info = sdio_get_device_hw_info,
    .get_in_band_intr = sdio_get_interrupt,
};

static zx_status_t sdmmc_wait_for_tran(sdmmc_device_t* dev) {
    uint32_t current_state;
    const size_t max_attempts = 10;
    size_t attempt = 0;
    for (; attempt <= max_attempts; attempt++) {
        uint32_t response;
        zx_status_t st = sdmmc_send_status(dev, &response);
        if (st != ZX_OK) {
            zxlogf(SPEW, "sdmmc: SDMMC_SEND_STATUS error, retcode = %d\n", st);
            return st;
        }

        current_state = MMC_STATUS_CURRENT_STATE(response);
        if (current_state == MMC_STATUS_CURRENT_STATE_RECV) {
            st = sdmmc_stop_transmission(dev);
            continue;
        } else if (current_state == MMC_STATUS_CURRENT_STATE_TRAN) {
            break;
        }

        zx_nanosleep(zx_deadline_after(ZX_MSEC(10)));
    }

    if (attempt == max_attempts) {
        // Too many retries, fail.
        return ZX_ERR_TIMED_OUT;
    } else {
        return ZX_OK;
    }
}

static void sdmmc_do_txn(sdmmc_device_t* dev, sdmmc_txn_t* txn) {
    // The TRACE_*() event macros are empty if driver tracing isn't enabled.
    // But that doesn't work for our call to trace_state().
    if (TRACE_ENABLED()) {
        dev->async_id = TRACE_NONCE();
        TRACE_ASYNC_BEGIN("sdmmc","sdmmc_do_txn", dev->async_id,
            "command", TA_INT32(txn->bop.rw.command),
            "extra", TA_INT32(txn->bop.rw.extra),
            "length", TA_INT32(txn->bop.rw.length),
            "offset_vmo", TA_INT64(txn->bop.rw.offset_vmo),
            "offset_dev", TA_INT64(txn->bop.rw.offset_dev));
    }

    uint32_t cmd_idx = 0;
    uint32_t cmd_flags = 0;

    // Figure out which SD command we need to issue.
    switch (BLOCK_OP(txn->bop.command)) {
    case BLOCK_OP_READ:
        if (txn->bop.rw.length > 1) {
            cmd_idx = SDMMC_READ_MULTIPLE_BLOCK;
            cmd_flags = SDMMC_READ_MULTIPLE_BLOCK_FLAGS;
        } else {
            cmd_idx = SDMMC_READ_BLOCK;
            cmd_flags = SDMMC_READ_BLOCK_FLAGS;
        }
        break;
    case BLOCK_OP_WRITE:
        if (txn->bop.rw.length > 1) {
            cmd_idx = SDMMC_WRITE_MULTIPLE_BLOCK;
            cmd_flags = SDMMC_WRITE_MULTIPLE_BLOCK_FLAGS;
        } else {
            cmd_idx = SDMMC_WRITE_BLOCK;
            cmd_flags = SDMMC_WRITE_BLOCK_FLAGS;
        }
        break;
    case BLOCK_OP_FLUSH:
        block_complete(txn, ZX_OK, dev->async_id);
        return;
    default:
        // should not get here
        zxlogf(ERROR, "sdmmc: do_txn invalid block op %d\n", BLOCK_OP(txn->bop.command));
        ZX_DEBUG_ASSERT(true);
        block_complete(txn, ZX_ERR_INVALID_ARGS, dev->async_id);
        return;
    }

    zxlogf(TRACE, "sdmmc: do_txn blockop 0x%x offset_vmo 0x%" PRIx64 " length 0x%x blocksize 0x%x"
                  " max_transfer_size 0x%x\n",
           txn->bop.command, txn->bop.rw.offset_vmo, txn->bop.rw.length,
           dev->block_info.block_size, dev->block_info.max_transfer_size);

    sdmmc_req_t* req = &dev->req;
    memset(req, 0, sizeof(*req));
    req->cmd_idx = cmd_idx;
    req->cmd_flags = cmd_flags;
    req->arg = txn->bop.rw.offset_dev;
    req->blockcount = txn->bop.rw.length;
    req->blocksize = dev->block_info.block_size;

    // convert offset_vmo and length to bytes
    txn->bop.rw.offset_vmo *= dev->block_info.block_size;
    txn->bop.rw.length *= dev->block_info.block_size;

    zx_status_t st = ZX_OK;
    if (sdmmc_use_dma(dev)) {
        req->use_dma = true;
        req->virt_buffer = NULL;
        req->pmt = ZX_HANDLE_INVALID;
        req->dma_vmo =  txn->bop.rw.vmo;
        req->buf_offset = txn->bop.rw.offset_vmo;
    } else {
        req->use_dma = false;
        st = zx_vmar_map(zx_vmar_root_self(), ZX_VM_PERM_READ | ZX_VM_PERM_WRITE,
                         0, txn->bop.rw.vmo, txn->bop.rw.offset_vmo, txn->bop.rw.length,
                         (uintptr_t*)&req->virt_buffer);
        if (st != ZX_OK) {
            zxlogf(TRACE, "sdmmc: do_txn vmo map error %d\n", st);
            block_complete(txn, st, dev->async_id);
            return;
        }
        req->virt_size = txn->bop.rw.length;
    }

    st = sdmmc_request(&dev->host, req);
    if (st != ZX_OK) {
        zxlogf(TRACE, "sdmmc: do_txn error %d\n", st);
        goto exit;
    } else {
        if ((req->blockcount > 1) && !(dev->host_info.caps & SDMMC_HOST_CAP_AUTO_CMD12)) {
            st = sdmmc_stop_transmission(dev);
            if (st != ZX_OK) {
                zxlogf(TRACE, "sdmmc: do_txn stop transmission error %d\n", st);
                goto exit;
            }
        }
        goto exit;
    }
exit:
    if (!req->use_dma) {
        zx_vmar_unmap(zx_vmar_root_self(), (uintptr_t)req->virt_buffer, req->virt_size);
    }
    block_complete(txn, st, dev->async_id);
    zxlogf(TRACE, "sdmmc: do_txn complete\n");
}

static int sdmmc_worker_thread(void* arg) {
    zx_status_t st = ZX_OK;
    sdmmc_device_t* dev = (sdmmc_device_t*)arg;
    bool dead = false;

    SDMMC_LOCK(dev);
    st = sdmmc_host_info(&dev->host, &dev->host_info);
    if (st != ZX_OK) {
        zxlogf(ERROR, "sdmmc: failed to get host info\n");
        SDMMC_UNLOCK(dev);
        goto fail;
    }

    zxlogf(TRACE, "sdmmc: host caps dma %d 8-bit bus %d max_transfer_size %" PRIu64 "\n",
           sdmmc_use_dma(dev) ? 1 : 0,
           (dev->host_info.caps & SDMMC_HOST_CAP_BUS_WIDTH_8) ? 1 : 0,
           dev->host_info.max_transfer_size);

    dev->block_info.max_transfer_size = dev->host_info.max_transfer_size;

    // Reset the card.
    sdmmc_hw_reset(&dev->host);

    // No matter what state the card is in, issuing the GO_IDLE_STATE command will
    // put the card into the idle state.
    if ((st = sdmmc_go_idle(dev)) != ZX_OK) {
        zxlogf(ERROR, "sdmmc: SDMMC_GO_IDLE_STATE failed, retcode = %d\n", st);
        SDMMC_UNLOCK(dev);
        goto fail;
    }

    // Probe for SDIO, SD and then MMC
    if ((st = sdmmc_probe_sdio(dev)) != ZX_OK) {
        if ((st = sdmmc_probe_sd(dev)) != ZX_OK) {
            if ((st = sdmmc_probe_mmc(dev)) != ZX_OK) {
                zxlogf(ERROR, "sdmmc: failed to probe\n");
                SDMMC_UNLOCK(dev);
                goto fail;
            }
        }
    }

    if (dev->type == SDMMC_TYPE_SDIO) {
        zx_device_t* hci_zxdev =  device_get_parent(dev->zxdev);

        zx_device_prop_t props[] = {
             { BIND_SDIO_VID, 0, dev->sdio_dev.funcs[0].hw_info.manufacturer_id},
             { BIND_SDIO_PID, 0, dev->sdio_dev.funcs[0].hw_info.product_id},
        };

        device_add_args_t sdio_args = {
            .version = DEVICE_ADD_ARGS_VERSION,
            .name = "sdio",
            .ctx = dev,
            .ops = &sdmmc_sdio_device_proto,
            .proto_id = ZX_PROTOCOL_SDIO,
            .proto_ops = &sdio_proto,
            .props = props,
            .prop_count = countof(props),
        };

        // Use platform device protocol to create our SDIO device, if it is available.
        pdev_protocol_t pdev;
        st = device_get_protocol(hci_zxdev, ZX_PROTOCOL_PDEV, &pdev);
        if (st == ZX_OK) {
            st = pdev_device_add(&pdev, 0, &sdio_args, &dev->child_zxdev);
        } else {
            st = device_add(dev->zxdev, &sdio_args, &dev->child_zxdev);
        }
        if (st != ZX_OK) {
            zxlogf(ERROR, "sdmmc: Failed to add sdio device, retcode = %d\n", st);
            SDMMC_UNLOCK(dev);
            goto fail;
        }
        zx_object_signal(dev->worker_event, 0, SDMMC_SHUTDOWN_DONE);
        SDMMC_UNLOCK(dev);
    } else {
        // Device must be in TRAN state at this point
        st = sdmmc_wait_for_tran(dev);
        if (st != ZX_OK) {
            zxlogf(ERROR, "sdmmc: waiting for TRAN state failed, retcode = %d\n", st);
            SDMMC_UNLOCK(dev);
            goto fail;
        }

        device_add_args_t block_args = {
            .version = DEVICE_ADD_ARGS_VERSION,
            .name = "sdmmc-block",
            .ctx = dev,
            .ops = &sdmmc_block_device_proto,
            .proto_id = ZX_PROTOCOL_BLOCK_IMPL,
            .proto_ops = &block_proto,
        };

        st = device_add(dev->zxdev, &block_args, &dev->child_zxdev);
        if (st != ZX_OK) {
            zxlogf(ERROR, "sdmmc: Failed to add mmc device, retcode = %d\n", st);
            SDMMC_UNLOCK(dev);
            goto fail;
        }

        zx_object_signal(dev->worker_event, 0, SDMMC_ADD_MMC_CHILD_DONE);
        SDMMC_UNLOCK(dev);
        for (;;) {
            // don't loop until txn_list is empty to check for SDMMC_SHUTDOWN
            // between each txn.
            SDMMC_LOCK(dev);
            sdmmc_txn_t* txn = list_remove_head_type(&dev->txn_list, sdmmc_txn_t, node);
            if (txn) {
                // Unlock if we execute the transaction
                SDMMC_UNLOCK(dev);
                sdmmc_do_txn(dev, txn);
            } else {
                // Stay locked if we're clearing the "RECEIVED" flag.
                zx_object_signal(dev->worker_event, SDMMC_TXN_RECEIVED, 0);
                SDMMC_UNLOCK(dev);
            }

            uint32_t pending;
            zx_status_t st = zx_object_wait_one(dev->worker_event,
                                                SDMMC_TXN_RECEIVED | SDMMC_SHUTDOWN,
                                                ZX_TIME_INFINITE, &pending);
            if (st != ZX_OK) {
                zxlogf(ERROR, "sdmmc: worker thread wait failed, retcode = %d\n", st);
                goto fail;
            }
            if (pending & SDMMC_SHUTDOWN) {
                zx_object_signal(dev->worker_event, 0, SDMMC_SHUTDOWN_DONE);
                break;
            }
        }
    }

    zxlogf(TRACE, "sdmmc: worker thread terminated successfully\n");
    return 0;

fail:
    SDMMC_LOCK(dev);
    zx_object_signal(dev->worker_event, 0, SDMMC_SHUTDOWN_DONE);
    dead = dev->dead;
    zx_device_t* zxdev = dev->zxdev;
    SDMMC_UNLOCK(dev);
    if (!dead) {
        //Already in middle of shutdown
        device_remove(zxdev);
    }
    return st;
}

static zx_status_t sdmmc_bind(void* ctx, zx_device_t* parent) {
    // Allocate the device.
    sdmmc_device_t* dev = calloc(1, sizeof(*dev));
    if (!dev) {
        zxlogf(ERROR, "sdmmc: no memory to allocate sdmmc device!\n");
        return ZX_ERR_NO_MEMORY;
    }

    zx_status_t st = device_get_protocol(parent, ZX_PROTOCOL_SDMMC, &dev->host);
    if (st != ZX_OK) {
        zxlogf(ERROR, "sdmmc: failed to get sdmmc protocol\n");
        free(dev);
        return ZX_ERR_NOT_SUPPORTED;
    }

    mtx_init(&dev->lock, mtx_plain);

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "sdmmc",
        .ctx = dev,
        .ops = &sdmmc_device_proto,
        .flags = DEVICE_ADD_NON_BINDABLE,
    };

    SDMMC_LOCK(dev);
    st = device_add(parent, &args, &dev->zxdev);
    if (st != ZX_OK) {
        free(dev);
        SDMMC_UNLOCK(dev);
        return st;
    }

    st = zx_event_create(0, &dev->worker_event);
    if (st != ZX_OK) {
        zxlogf(ERROR, "sdmmc: failed to create event, retcode = %d\n", st);
        SDMMC_UNLOCK(dev);
        device_remove(dev->zxdev);
        return st;
    }
    list_initialize(&dev->txn_list);
    dev->worker_thread_started = true;
    // bootstrap in a thread
    int rc = thrd_create_with_name(&dev->worker_thread, sdmmc_worker_thread, dev, "sdmmc-worker");
    if (rc != thrd_success) {
        st = thrd_status_to_zx_status(rc);
        dev->worker_thread_started = false;
        zx_device_t* zxdev = dev->zxdev;
        bool dead = dev->dead;
        SDMMC_UNLOCK(dev);
        if (!dead) {
            device_remove(zxdev);
        }
        return st;
    }
    SDMMC_UNLOCK(dev);
    return ZX_OK;
}

static zx_driver_ops_t sdmmc_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = sdmmc_bind,
};

// The formatter does not play nice with these macros.
// clang-format off
ZIRCON_DRIVER_BEGIN(sdmmc, sdmmc_driver_ops, "zircon", "0.1", 1)
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_SDMMC),
ZIRCON_DRIVER_END(sdmmc)
// clang-format on
