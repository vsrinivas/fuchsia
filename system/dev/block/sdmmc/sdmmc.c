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

// DDK Includes
#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/debug.h>
#include <ddk/protocol/sdmmc.h>

// Zircon Includes
#include <sync/completion.h>
#include <pretty/hexdump.h>
#include <zircon/assert.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/threads.h>
#include <zircon/device/block.h>

#include "sdmmc.h"

#define SDMMC_TXN_RECEIVED      ZX_EVENT_SIGNALED
#define SDMMC_SHUTDOWN          ZX_USER_SIGNAL_0
#define SDMMC_SHUTDOWN_DONE     ZX_USER_SIGNAL_1

#define SDMMC_LOCK(dev)   mtx_lock(&(dev)->lock);
#define SDMMC_UNLOCK(dev) mtx_unlock(&(dev)->lock);

#define BLOCK_OP(op)    ((op) & BLOCK_OP_MASK)

#if WITH_STATS
#define STAT_INC(name) do { dev->stat_##name++; } while (0)
#define STAT_DEC(name) do { dev->stat_##name--; } while (0)
#define STAT_DEC_IF(name, c) do { if (c) dev->stat_##name--; } while (0)
#define STAT_ADD(name, num) do { dev->stat_##name += num; } while (0)
#define STAT_INC_MAX(name) do { \
    if (++dev->stat_##name > dev->stat_max_##name) { \
        dev->stat_max_##name = dev->stat_##name; \
    }} while (0)
#else
#define STAT_INC(name) do { } while (0)
#define STAT_DEC(name) do { } while (0)
#define STAT_DEC_IF(name, c) do { } while (0)
#define STAT_ADD(name, num) do { } while (0)
#define STAT_INC_MAX(name) do { } while (0)
#endif

static void block_complete(block_op_t* bop, zx_status_t status) {
    if (bop->completion_cb) {
        bop->completion_cb(bop, status);
    } else {
        zxlogf(TRACE, "sdmmc: block op %p completion_cb unset!\n", bop);
    }
}

static zx_off_t sdmmc_get_size(void* ctx) {
    sdmmc_device_t* dev = ctx;
    return dev->block_info.block_count * dev->block_info.block_size;
}

static zx_status_t sdmmc_ioctl(void* ctx, uint32_t op, const void* cmd,
                               size_t cmdlen, void* reply, size_t max, size_t* out_actual) {
    sdmmc_device_t* dev = ctx;
    switch (op) {
    case IOCTL_BLOCK_GET_INFO: {
        block_info_t* info = reply;
        if (max < sizeof(*info)) {
            return ZX_ERR_BUFFER_TOO_SMALL;
        }
        memcpy(info, &dev->block_info, sizeof(*info));
        *out_actual = sizeof(*info);
        return ZX_OK;
    }
    case IOCTL_BLOCK_GET_STATS: {
#if WITH_STATS
        if (cmdlen != sizeof(bool)) {
            return ZX_ERR_INVALID_ARGS;
        }
        block_stats_t* out = reply;
        if (max < sizeof(*out)) {
            return ZX_ERR_BUFFER_TOO_SMALL;
        }
        SDMMC_LOCK(dev);
        out->max_concur = dev->stat_max_concur;
        out->max_pending = dev->stat_max_pending;
        out->total_ops = dev->stat_total_ops;
        out->total_blocks = dev->stat_total_blocks;
        bool clear = *(bool *)cmd;
        if (clear) {
            dev->stat_max_concur = 0;
            dev->stat_max_pending = 0;
            dev->stat_total_ops = 0;
            dev->stat_total_blocks = 0;
        }
        SDMMC_UNLOCK(dev);
        *out_actual = sizeof(*out);
        return ZX_OK;
#else
        return ZX_ERR_NOT_SUPPORTED;
#endif
    }
    case IOCTL_DEVICE_SYNC:
        return ZX_OK;
    default:
        return ZX_ERR_NOT_SUPPORTED;
    }
    return 0;
}

static void sdmmc_unbind(void* ctx) {
    sdmmc_device_t* dev = ctx;
    device_remove(dev->zxdev);
}

static void sdmmc_release(void* ctx) {
    sdmmc_device_t* dev = ctx;
    if (dev->worker_thread_running) {
        // signal the worker thread and wait for it to terminate
        zx_object_signal(dev->worker_event, 0, SDMMC_SHUTDOWN);
        zx_object_wait_one(dev->worker_event, SDMMC_SHUTDOWN_DONE, ZX_TIME_INFINITE, NULL);

        SDMMC_LOCK(dev);

        // error out all pending requests
        sdmmc_txn_t* txn = NULL;
        list_for_every_entry(&dev->txn_list, txn, sdmmc_txn_t, node) {
            SDMMC_UNLOCK(dev);

            block_complete(&txn->bop, ZX_ERR_BAD_STATE);

            SDMMC_LOCK(dev);
        }

        SDMMC_UNLOCK(dev);

        thrd_join(dev->worker_thread, NULL);
    }

    if (dev->worker_event != ZX_HANDLE_INVALID) {
        zx_handle_close(dev->worker_event);
    }

    free(dev);
}

// Device protocol.
static zx_protocol_device_t sdmmc_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .ioctl = sdmmc_ioctl,
    .get_size = sdmmc_get_size,
    .unbind = sdmmc_unbind,
    .release = sdmmc_release,
};

static void sdmmc_query(void* ctx, block_info_t* info_out, size_t* block_op_size_out) {
    sdmmc_device_t* dev = ctx;
    memcpy(info_out, &dev->block_info, sizeof(*info_out));
    *block_op_size_out = sizeof(sdmmc_txn_t);
}

static void sdmmc_queue(void* ctx, block_op_t* btxn) {
    sdmmc_device_t* dev = ctx;
    sdmmc_txn_t* txn = containerof(btxn, sdmmc_txn_t, bop);

    switch (BLOCK_OP(btxn->command)) {
    case BLOCK_OP_READ:
    case BLOCK_OP_WRITE: {
        uint64_t max = dev->block_info.block_count;
        if ((btxn->rw.offset_dev >= max) || ((max - btxn->rw.offset_dev) < btxn->rw.length)) {
            block_complete(btxn, ZX_ERR_OUT_OF_RANGE);
            return;
        }
        if (btxn->rw.length == 0) {
            block_complete(btxn, ZX_OK);
            return;
        }
        break;
    }
    case BLOCK_OP_FLUSH:
        // queue the flush op. because there is no out of order execution in this
        // driver, when this op gets processed all previous ops are complete.
        break;
    default:
        block_complete(btxn, ZX_ERR_NOT_SUPPORTED);
        return;
    }

    SDMMC_LOCK(dev);

    STAT_INC(total_ops);
    if ((BLOCK_OP(btxn->command) == BLOCK_OP_READ) ||
        (BLOCK_OP(btxn->command) == BLOCK_OP_WRITE)) {
        STAT_ADD(total_blocks, btxn->rw.length);
    }

    list_add_tail(&dev->txn_list, &txn->node);
    STAT_INC_MAX(pending);
    // Wake up the worker thread (while locked, so they don't accidentally
    // clear the event).
    zx_object_signal(dev->worker_event, 0, SDMMC_TXN_RECEIVED);

    SDMMC_UNLOCK(dev);
}

// Block protocol
static block_protocol_ops_t block_proto = {
    .query = sdmmc_query,
    .queue = sdmmc_queue,
};

// SDIO protocol
static sdio_protocol_ops_t sdio_proto = {
    .get_oob_irq = sdio_get_oob_irq_host,
    .enable_fn = sdio_enable_function,
    .disable_fn = sdio_disable_function,
    .enable_fn_intr = sdio_enable_interrupt,
    .disable_fn_intr = sdio_disable_interrupt,
    .update_block_size = sdio_modify_block_size,
    .do_rw_txn = sdio_rw_data,
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
        block_complete(&txn->bop, ZX_OK);
        return;
    default:
        // should not get here
        zxlogf(ERROR, "sdmmc: do_txn invalid block op %d\n", BLOCK_OP(txn->bop.command));
        ZX_DEBUG_ASSERT(true);
        block_complete(&txn->bop, ZX_ERR_INVALID_ARGS);
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
        req->virt = NULL;
        req->pmt = ZX_HANDLE_INVALID;
        req->dma_vmo =  txn->bop.rw.vmo;
        req->buf_offset = txn->bop.rw.offset_vmo;
    } else {
        req->use_dma = false;
        st = zx_vmar_map(zx_vmar_root_self(), 0, txn->bop.rw.vmo,
                         txn->bop.rw.offset_vmo, txn->bop.rw.length,
                         ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE, (uintptr_t*)&req->virt);
        if (st != ZX_OK) {
            zxlogf(TRACE, "sdmmc: do_txn vmo map error %d\n", st);
            block_complete(&txn->bop, st);
            return;
        }
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
        zx_vmar_unmap(zx_vmar_root_self(), (uintptr_t)req->virt, txn->bop.rw.length);
    }
    block_complete(&txn->bop, st);
    zxlogf(TRACE, "sdmmc: do_txn complete\n");
}

static int sdmmc_worker_thread(void* arg) {
    zx_status_t st = ZX_OK;
    sdmmc_device_t* dev = (sdmmc_device_t*)arg;

    st = sdmmc_host_info(&dev->host, &dev->host_info);
    if (st != ZX_OK) {
        zxlogf(ERROR, "sdmmc: failed to get host info\n");
        return st;
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
        device_remove(dev->zxdev);
        return st;
    }

    // Probe for SDIO, SD and then MMC
    if ((st = sdmmc_probe_sdio(dev)) != ZX_OK) {
        if ((st = sdmmc_probe_sd(dev)) != ZX_OK) {
            if ((st = sdmmc_probe_mmc(dev)) != ZX_OK) {
                zxlogf(ERROR, "sdmmc: failed to probe\n");
                device_remove(dev->zxdev);
                return st;
            }
        }
    }

    if (dev->type == SDMMC_TYPE_SDIO) {
        zx_device_t* hci_zxdev =  device_get_parent(dev->zxdev);

        //Remove block device and add SDIO device
        device_remove(dev->zxdev);
        zx_device_prop_t props[] = {
             { BIND_SDIO_VID, 0, dev->sdio_info.funcs[0].manufacturer_id},
             { BIND_SDIO_PID, 0, dev->sdio_info.funcs[0].product_id},
        };

        device_add_args_t sdio_args = {
            .version = DEVICE_ADD_ARGS_VERSION,
            .name = "sdio",
            .ctx = dev,
            .ops = &sdmmc_device_proto,
            .proto_id = ZX_PROTOCOL_SDIO,
            .proto_ops = &sdio_proto,
            .props = props,
            .prop_count = countof(props),
        };

        st = device_add(hci_zxdev, &sdio_args, &dev->zxdev);
        if (st != ZX_OK) {
            zxlogf(ERROR, "sdmmc: Failed to add sdio device, retcode = %d\n", st);
            return st;
        }
    } else {
        // Device must be in TRAN state at this point
        st = sdmmc_wait_for_tran(dev);
        if (st != ZX_OK) {
            zxlogf(ERROR, "sdmmc: waiting for TRAN state failed, retcode = %d\n", st);
            device_remove(dev->zxdev);
            return st;
        }

        dev->worker_thread_running = true;
        device_make_visible(dev->zxdev);

        for (;;) {
            // don't loop until txn_list is empty to check for SDMMC_SHUTDOWN
            // between each txn.
            SDMMC_LOCK(dev);
            sdmmc_txn_t* txn = list_remove_head_type(&dev->txn_list, sdmmc_txn_t, node);
            STAT_DEC_IF(pending, txn != NULL);
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
                break;
            }
            if (pending & SDMMC_SHUTDOWN) {
                zx_object_signal(dev->worker_event, pending, SDMMC_SHUTDOWN_DONE);
                break;
            }
        }
    }
    zxlogf(TRACE, "sdmmc: worker thread terminated\n");
    return 0;
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
        st = ZX_ERR_NOT_SUPPORTED;
        goto fail;
    }

    mtx_init(&dev->lock, mtx_plain);
    list_initialize(&dev->txn_list);

    st = zx_event_create(0, &dev->worker_event);
    if (st != ZX_OK) {
        zxlogf(ERROR, "sdmmc: failed to create event, retcode = %d\n", st);
        goto fail;
    }

    device_add_args_t block_args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "sdmmc",
        .ctx = dev,
        .ops = &sdmmc_device_proto,
        .proto_id = ZX_PROTOCOL_BLOCK_IMPL,
        .proto_ops = &block_proto,
        .flags = DEVICE_ADD_INVISIBLE,
    };

    st = device_add(parent, &block_args, &dev->zxdev);
    if (st != ZX_OK) {
        goto fail;
    }

    // bootstrap in a thread
    int rc = thrd_create_with_name(&dev->worker_thread, sdmmc_worker_thread, dev, "sdmmc-worker");
    if (rc != thrd_success) {
        st = thrd_status_to_zx_status(rc);
        goto fail_remove;
    }
    return ZX_OK;

fail_remove:
    device_remove(dev->zxdev);
fail:
    free(dev);
    return st;
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
