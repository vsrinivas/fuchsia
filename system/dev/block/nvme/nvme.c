// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/block.h>
#include <ddk/protocol/pci.h>
#include <ddk/io-buffer.h>

#include <hw/reg.h>
#include <hw/pci.h>

#include <sync/completion.h>

#include <zircon/device/block.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>
#include <zircon/listnode.h>

#include "nvme-hw.h"

// If enabled, gather stats on concurrent io ops,
// pending txns, etc.  Stats are retrieved by
// IOCTL_BLOCK_GET_STATS
#define WITH_STATS 1

#define TXN_FLAG_FAILED 1

typedef struct {
    block_op_t op;
    list_node_t node;
    uint16_t pending_utxns;
    uint8_t opcode;
    uint8_t flags;
} nvme_txn_t;

typedef struct {
    zx_paddr_t phys;    // io buffer phys base (1 page)
    void* virt;         // io buffer virt base
    zx_handle_t pmt;    // pinned memory
    nvme_txn_t* txn;    // related txn
    uint16_t id;
    uint16_t reserved0;
    uint32_t reserved1;
} nvme_utxn_t;

#define UTXN_COUNT 63

// There's no system constant for this.  Ensure it matches reality.
#define PAGE_SHIFT (12ULL)
static_assert(PAGE_SIZE == (1ULL << PAGE_SHIFT), "");

#define PAGE_MASK (PAGE_SIZE - 1ULL)

// Limit maximum transfer size to 1MB which fits comfortably
// within our single scatter gather page per utxn setup
#define MAX_XFER (1024*1024)

// Maximum submission and completion queue item counts, for
// queues that are a single page in size.
#define SQMAX (PAGE_SIZE / sizeof(nvme_cmd_t))
#define CQMAX (PAGE_SIZE / sizeof(nvme_cpl_t))

// global driver state bits
#define FLAG_IRQ_THREAD_STARTED  0x0001
#define FLAG_IO_THREAD_STARTED   0x0002
#define FLAG_SHUTDOWN            0x0004

#define FLAG_HAS_VWC             0x0100

typedef struct {
    void* io;
    zx_handle_t ioh;
    zx_handle_t irqh;
    zx_handle_t bti;
    uint32_t flags;
    mtx_t lock;

    // io queue doorbell registers
    void* io_sq_tail_db;
    void* io_cq_head_db;

    nvme_cpl_t* io_cq;
    nvme_cmd_t* io_sq;
    uint32_t io_nsid;
    uint16_t io_cq_head;
    uint16_t io_cq_toggle;
    uint16_t io_sq_tail;
    uint16_t io_sq_head;

    uint64_t utxn_avail;   // bitmask of available utxns

    // The pending list is txns that have been received
    // via nvme_queue() and are waiting for io to start.
    // The exception is the head of the pending list which may
    // be partially started, waiting for more utxns to become
    // available.
    // The active list consists of txns where all utxns have
    // been created and we're waiting for them to complete or
    // error out.
    list_node_t pending_txns;      // inbound txns to process
    list_node_t active_txns;       // txns in flight

    // The io signal completion is signaled from nvme_queue()
    // or from the irq thread, notifying the io thread that
    // it has work to do.
    completion_t io_signal;

    uint32_t max_xfer;
    block_info_t info;

    // admin queue doorbell registers
    void* io_admin_sq_tail_db;
    void* io_admin_cq_head_db;

    // admin queues and state
    nvme_cpl_t* admin_cq;
    nvme_cmd_t* admin_sq;
    uint16_t admin_cq_head;
    uint16_t admin_cq_toggle;
    uint16_t admin_sq_tail;
    uint16_t admin_sq_head;

    // context for admin transactions
    // presently we serialize these under the admin_lock
    mtx_t admin_lock;
    completion_t admin_signal;
    nvme_cpl_t admin_result;

    pci_protocol_t pci;
    zx_device_t* zxdev;

    size_t iosz;

    // source of physical pages for queues and admin commands
    io_buffer_t iob;

    thrd_t irqthread;
    thrd_t iothread;

#if WITH_STATS
    size_t stat_concur;
    size_t stat_pending;
    size_t stat_max_concur;
    size_t stat_max_pending;
    size_t stat_total_ops;
    size_t stat_total_blocks;
#endif

    // pool of utxns
    nvme_utxn_t utxn[UTXN_COUNT];
} nvme_device_t;

#if WITH_STATS
#define STAT_INC(name) do { nvme->stat_##name++; } while (0)
#define STAT_DEC(name) do { nvme->stat_##name--; } while (0)
#define STAT_DEC_IF(name, c) do { if (c) nvme->stat_##name--; } while (0)
#define STAT_ADD(name, num) do { nvme->stat_##name += num; } while (0)
#define STAT_INC_MAX(name) do { \
    if (++nvme->stat_##name > nvme->stat_max_##name) { \
        nvme->stat_max_##name = nvme->stat_##name; \
    }} while (0)
#else
#define STAT_INC(name) do { } while (0)
#define STAT_DEC(name) do { } while (0)
#define STAT_DEC_IF(name, c) do { } while (0)
#define STAT_ADD(name, num) do { } while (0)
#define STAT_INC_MAX(name) do { } while (0)
#endif


// We break IO transactions down into one or more "micro transactions" (utxn)
// based on the transfer limits of the controller, etc.  Each utxn has an
// id associated with it, which is used as the command id for the command
// queued to the NVME device.  This id is the same as its index into the
// pool of utxns and the bitmask of free txns, to simplify management.
//
// We maintain a pool of 63 of these, which is the number of commands
// that can be submitted to NVME via a single page submit queue.
//
// The utxns are not protected by locks.  Instead, after initialization,
// they may only be touched by the io thread, which is responsible for
// queueing commands and dequeuing completion messages.

static nvme_utxn_t* utxn_get(nvme_device_t* nvme) {
    uint64_t n = __builtin_ffsll(nvme->utxn_avail);
    if (n == 0) {
        return NULL;
    }
    n--;
    nvme->utxn_avail &= ~(1ULL << n);
    STAT_INC_MAX(concur);
    return nvme->utxn + n;
}

static void utxn_put(nvme_device_t* nvme, nvme_utxn_t* utxn) {
    uint64_t n = utxn->id;
    STAT_DEC(concur);
    nvme->utxn_avail |= (1ULL << n);
}

static zx_status_t nvme_admin_cq_get(nvme_device_t* nvme, nvme_cpl_t* cpl) {
    if ((readw(&nvme->admin_cq[nvme->admin_cq_head].status) & 1) != nvme->admin_cq_toggle) {
        return ZX_ERR_SHOULD_WAIT;
    }
    *cpl = nvme->admin_cq[nvme->admin_cq_head];

    // advance the head pointer, wrapping and inverting toggle at max
    uint16_t next = (nvme->admin_cq_head + 1) & (CQMAX - 1);
    if ((nvme->admin_cq_head = next) == 0) {
        nvme->admin_cq_toggle ^= 1;
    }

    // note the new sq head reported by hw
    nvme->admin_sq_head = cpl->sq_head;

    // ring the doorbell
    writel(next, nvme->io_admin_cq_head_db);
    return ZX_OK;
}

static zx_status_t nvme_admin_sq_put(nvme_device_t* nvme, nvme_cmd_t* cmd) {
    uint16_t next = (nvme->admin_sq_tail + 1) & (SQMAX - 1);

    // if head+1 == tail: queue is full
    if (next == nvme->admin_sq_head) {
        return ZX_ERR_SHOULD_WAIT;
    }

    nvme->admin_sq[nvme->admin_sq_tail] = *cmd;
    nvme->admin_sq_tail = next;

    // ring the doorbell
    writel(next, nvme->io_admin_sq_tail_db);
    return ZX_OK;
}

static zx_status_t nvme_io_cq_get(nvme_device_t* nvme, nvme_cpl_t* cpl) {
    if ((readw(&nvme->io_cq[nvme->io_cq_head].status) & 1) != nvme->io_cq_toggle) {
        return ZX_ERR_SHOULD_WAIT;
    }
    *cpl = nvme->io_cq[nvme->io_cq_head];

    // advance the head pointer, wrapping and inverting toggle at max
    uint16_t next = (nvme->io_cq_head + 1) & (CQMAX - 1);
    if ((nvme->io_cq_head = next) == 0) {
        nvme->io_cq_toggle ^= 1;
    }

    // note the new sq head reported by hw
    nvme->io_sq_head = cpl->sq_head;
    return ZX_OK;
}

static void nvme_io_cq_ack(nvme_device_t* nvme) {
    // ring the doorbell
    writel(nvme->io_cq_head, nvme->io_cq_head_db);
}

static zx_status_t nvme_io_sq_put(nvme_device_t* nvme, nvme_cmd_t* cmd) {
    uint16_t next = (nvme->io_sq_tail + 1) & (SQMAX - 1);

    // if head+1 == tail: queue is full
    if (next == nvme->io_sq_head) {
        return ZX_ERR_SHOULD_WAIT;
    }

    nvme->io_sq[nvme->io_sq_tail] = *cmd;
    nvme->io_sq_tail = next;

    // ring the doorbell
    writel(next, nvme->io_sq_tail_db);
    return ZX_OK;
}

static int irq_thread(void* arg) {
    nvme_device_t* nvme = arg;
    for (;;) {
        zx_status_t r;
        uint64_t slots;
        if ((r = zx_interrupt_wait(nvme->irqh, &slots)) != ZX_OK) {
            zxlogf(ERROR, "nvme: irq wait failed: %d\n", r);
            break;
        }

        nvme_cpl_t cpl;
        if (nvme_admin_cq_get(nvme, &cpl) == ZX_OK) {
            nvme->admin_result = cpl;
            completion_signal(&nvme->admin_signal);
        }

        completion_signal(&nvme->io_signal);
    }
    return 0;
}

static zx_status_t nvme_admin_txn(nvme_device_t* nvme, nvme_cmd_t* cmd, nvme_cpl_t* cpl) {
    zx_status_t r;
    mtx_lock(&nvme->admin_lock);
    completion_reset(&nvme->admin_signal);
    if ((r = nvme_admin_sq_put(nvme, cmd)) != ZX_OK) {
        goto done;
    }
    if ((r = completion_wait(&nvme->admin_signal, zx_deadline_after(ZX_SEC(1)))) != ZX_OK) {
        zxlogf(ERROR, "nvme: admin txn: timed out\n");
        goto done;
    }

    unsigned code = NVME_CPL_STATUS_CODE(nvme->admin_result.status);
    if (code != 0) {
        zxlogf(ERROR, "nvme: admin txn: nvm error %03x\n", code);
        r = ZX_ERR_IO;
    }
    if (cpl != NULL) {
        *cpl = nvme->admin_result;
    }
done:
    mtx_unlock(&nvme->admin_lock);
    return r;
}

static inline void txn_complete(nvme_txn_t* txn, zx_status_t status) {
    txn->op.completion_cb(&txn->op, status);
}

// Attempt to generate utxns and queue nvme commands for a txn
// Returns true if this could not be completed due to temporary
// lack of resources or false if either it succeeded or errored out.
static bool io_process_txn(nvme_device_t* nvme, nvme_txn_t* txn) {
    zx_handle_t vmo = txn->op.rw.vmo;
    nvme_utxn_t* utxn;
    zx_paddr_t* pages;
    zx_status_t r;

    for (;;) {
        // If there are no available utxns, we can't proceed
        // and we tell the caller to retain the txn (true)
        if ((utxn = utxn_get(nvme)) == NULL) {
            return true;
        }

        uint32_t blocks = txn->op.rw.length;
        if (blocks > nvme->max_xfer) {
            blocks = nvme->max_xfer;
        }

        // Total transfer size in bytes
        size_t bytes = ((size_t) blocks) * ((size_t) nvme->info.block_size);

        // Page offset of first page of transfer
        size_t pageoffset = txn->op.rw.offset_vmo & (~PAGE_MASK);

        // Byte offset into first page of transfer
        size_t byteoffset = txn->op.rw.offset_vmo & PAGE_MASK;

        // Total pages mapped / touched
        size_t pagecount = (byteoffset + bytes + PAGE_MASK) >> PAGE_SHIFT;

        // read disk (OP_READ) -> memory (PERM_WRITE) or
        // write memory (PERM_READ) -> disk (OP_WRITE)
        uint32_t opt = (txn->opcode == NVME_OP_READ) ? ZX_BTI_PERM_WRITE : ZX_BTI_PERM_READ;

        pages = utxn->virt;

        if ((r = zx_bti_pin(nvme->bti, opt, vmo, pageoffset, pagecount << PAGE_SHIFT,
                            pages, pagecount, &utxn->pmt)) != ZX_OK) {
            zxlogf(ERROR, "nvme: could not pin pages: %d\n", r);
            break;
        }

        nvme_cmd_t cmd;
        memset(&cmd, 0, sizeof(cmd));
        cmd.cmd = NVME_CMD_CID(utxn->id) | NVME_CMD_PRP | NVME_CMD_NORMAL | NVME_CMD_OPC(txn->opcode);
        cmd.nsid = 1;
        cmd.u.rw.start_lba = txn->op.rw.offset_dev;
        cmd.u.rw.block_count = blocks - 1;
        // The NVME command has room for two data pointers inline.
        // The first is always the pointer to the first page where data is.
        // The second is the second page if pagecount is 2.
        // The second is the address of an array of page 2..n if pagecount > 2
        cmd.dptr.prp[0] = pages[0] | byteoffset;
        if (pagecount == 2) {
            cmd.dptr.prp[1] = pages[1];
        } else if (pagecount > 2) {
            cmd.dptr.prp[1] = utxn->phys + sizeof(uint64_t);
        }

        zxlogf(TRACE, "nvme: txn=%p utxn id=%u pages=%zu op=%s\n", txn, utxn->id, pagecount,
               txn->opcode == NVME_OP_WRITE ? "WR" : "RD");
        zxlogf(SPEW, "nvme: prp[0]=%016zx prp[1]=%016zx\n", cmd.dptr.prp[0], cmd.dptr.prp[1]);
        zxlogf(SPEW, "nvme: pages[] = { %016zx, %016zx, %016zx, %016zx, ... }\n",
               pages[0], pages[1], pages[2], pages[3]);

        if ((r = nvme_io_sq_put(nvme, &cmd)) != ZX_OK) {
            zxlogf(ERROR, "nvme: could not submit cmd (txn=%p id=%u)\n", txn, utxn->id);
            break;
        }

        utxn->txn = txn;

        // keep track of where we are
        txn->op.rw.offset_dev += blocks;
        txn->op.rw.offset_vmo += bytes;
        txn->op.rw.length -= blocks;
        txn->pending_utxns++;

        // If there's no more remaining, we're done, and we
        // move this txn to the active list and tell the
        // caller not to retain the txn (false)
        if (txn->op.rw.length == 0) {
            mtx_lock(&nvme->lock);
            list_add_tail(&nvme->active_txns, &txn->node);
            mtx_unlock(&nvme->lock);
            return false;
        }
    }

    // failure
    if ((r = zx_pmt_unpin(utxn->pmt)) != ZX_OK) {
        zxlogf(ERROR, "nvme: cannot unpin io buffer: %d\n", r);
    }
    utxn_put(nvme, utxn);

    mtx_lock(&nvme->lock);
    txn->flags |= TXN_FLAG_FAILED;
    if (txn->pending_utxns) {
        // if there are earlier uncompleted IOs we become active now
        // and will finish erroring out when they complete
        list_add_tail(&nvme->active_txns, &txn->node);
        txn = NULL;
    }
    mtx_unlock(&nvme->lock);

    if (txn != NULL) {
        txn_complete(txn, ZX_ERR_INTERNAL);
    }

    // Either way we tell the caller not to retain the txn (false)
    return false;
}

static void io_process_txns(nvme_device_t* nvme) {
    nvme_txn_t* txn;

    for (;;) {
        mtx_lock(&nvme->lock);
        txn = list_remove_head_type(&nvme->pending_txns, nvme_txn_t, node);
        STAT_DEC_IF(pending, txn != NULL);
        mtx_unlock(&nvme->lock);

        if (txn == NULL) {
            return;
        }

        if (io_process_txn(nvme, txn)) {
            // put txn back at front of queue for further processing later
            mtx_lock(&nvme->lock);
            list_add_head(&nvme->pending_txns, &txn->node);
            STAT_INC_MAX(pending);
            mtx_unlock(&nvme->lock);
            return;
        }
    }
}

static void io_process_cpls(nvme_device_t* nvme) {
    bool ring_doorbell = false;
    nvme_cpl_t cpl;

    while (nvme_io_cq_get(nvme, &cpl) == ZX_OK) {
        ring_doorbell = true;

        if (cpl.cmd_id >= UTXN_COUNT) {
            zxlogf(ERROR, "nvme: unexpected cmd id %u\n", cpl.cmd_id);
            continue;
        }
        nvme_utxn_t* utxn = nvme->utxn + cpl.cmd_id;
        nvme_txn_t* txn = utxn->txn;

        if (txn == NULL) {
            zxlogf(ERROR, "nvme: inactive utxn #%u completed?!\n", cpl.cmd_id);
            continue;
        }

        uint32_t code = NVME_CPL_STATUS_CODE(cpl.status);
        if (code != 0) {
            zxlogf(ERROR, "nvme: utxn #%u txn %p failed: status=%03x\n",
                   cpl.cmd_id, txn, code);
            txn->flags |= TXN_FLAG_FAILED;
            // discard any remaining bytes -- no reason to keep creating
            // further utxns once one has failed
            txn->op.rw.length = 0;
        } else {
            zxlogf(SPEW, "nvme: utxn #%u txn %p OKAY\n", cpl.cmd_id, txn);
        }

        zx_status_t r;
        if ((r = zx_pmt_unpin(utxn->pmt)) != ZX_OK) {
            zxlogf(ERROR, "nvme: cannot unpin io buffer: %d\n", r);
        }

        // release the microtransaction
        utxn->txn = NULL;
        utxn_put(nvme, utxn);

        txn->pending_utxns--;
        if ((txn->pending_utxns == 0) && (txn->op.rw.length == 0)) {
            // remove from either pending or active list
            mtx_lock(&nvme->lock);
            list_delete(&txn->node);
            mtx_unlock(&nvme->lock);
            zxlogf(TRACE, "nvme: txn %p %s\n", txn, txn->flags & TXN_FLAG_FAILED ? "error" : "okay");
            txn_complete(txn, txn->flags & TXN_FLAG_FAILED ? ZX_ERR_IO : ZX_OK);
        }
    }

    if (ring_doorbell) {
        nvme_io_cq_ack(nvme);
    }
}

static int io_thread(void* arg) {
    nvme_device_t* nvme = arg;
    for (;;) {
        if (completion_wait(&nvme->io_signal, ZX_TIME_INFINITE)) {
            break;
        }
        if (nvme->flags & FLAG_SHUTDOWN) {
            //TODO: cancel out pending IO
            zxlogf(INFO, "nvme: io thread exiting\n");
            break;
        }

        completion_reset(&nvme->io_signal);

        // process completion messages
        io_process_cpls(nvme);

        // process work queue
        io_process_txns(nvme);

    }
    return 0;
}

static void nvme_queue(void* ctx, block_op_t* op) {
    nvme_device_t* nvme = ctx;
    nvme_txn_t* txn = containerof(op, nvme_txn_t, op);

    switch (txn->op.command & BLOCK_OP_MASK) {
    case BLOCK_OP_READ:
        txn->opcode = NVME_OP_READ;
        break;
    case BLOCK_OP_WRITE:
        txn->opcode = NVME_OP_WRITE;
        break;
    case BLOCK_OP_FLUSH:
        // TODO
        txn_complete(txn, ZX_OK);
        return;
    default:
        txn_complete(txn, ZX_ERR_NOT_SUPPORTED);
        return;
    }

    if (txn->op.rw.length == 0) {
        txn_complete(txn, ZX_ERR_INVALID_ARGS);
        return;
    }
    // Transaction must fit within device
    if ((txn->op.rw.offset_dev >= nvme->info.block_count) ||
        (nvme->info.block_count - txn->op.rw.offset_dev < txn->op.rw.length)) {
        txn_complete(txn, ZX_ERR_OUT_OF_RANGE);
        return;
    }

    // convert vmo offset to a byte offset
    txn->op.rw.offset_vmo *= nvme->info.block_size;

    txn->pending_utxns = 0;
    txn->flags = 0;

    zxlogf(SPEW, "nvme: io: %s: %ublks @ blk#%zu\n",
           txn->opcode == NVME_OP_WRITE ? "wr" : "rd",
           txn->op.rw.length + 1U, txn->op.rw.offset_dev);

    STAT_INC(total_ops);
    STAT_ADD(total_blocks, txn->op.rw.length);

    mtx_lock(&nvme->lock);
    list_add_tail(&nvme->pending_txns, &txn->node);
    STAT_INC_MAX(pending);
    mtx_unlock(&nvme->lock);

    completion_signal(&nvme->io_signal);
}

static void nvme_query(void* ctx, block_info_t* info_out, size_t* block_op_size_out) {
    nvme_device_t* nvme = ctx;
    *info_out = nvme->info;
    *block_op_size_out = sizeof(nvme_txn_t);
}

static zx_status_t nvme_ioctl(void* ctx, uint32_t op, const void* cmd, size_t cmdlen, void* reply,
                              size_t max, size_t* out_actual) {
    nvme_device_t* nvme = ctx;
    switch (op) {
    case IOCTL_BLOCK_GET_INFO: {
        if (max < sizeof(block_info_t)) {
            return ZX_ERR_BUFFER_TOO_SMALL;
        }
        size_t sz;
        nvme_query(nvme, reply, &sz);
        *out_actual = sizeof(block_info_t);
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
        mtx_lock(&nvme->lock);
        out->max_concur = nvme->stat_max_concur;
        out->max_pending = nvme->stat_max_pending;
        out->total_ops = nvme->stat_total_ops;
        out->total_blocks = nvme->stat_total_blocks;
        bool clear = *(bool *)cmd;
        if (clear) {
            nvme->stat_max_concur = 0;
            nvme->stat_max_pending = 0;
            nvme->stat_total_ops = 0;
            nvme->stat_total_blocks = 0;
        }
        mtx_unlock(&nvme->lock);
        *out_actual = sizeof(*out);
        return ZX_OK;
#else
        return ZX_ERR_NOT_SUPPORTED;
#endif
    }
    case IOCTL_DEVICE_SYNC: {
        return ZX_OK;
    }
    default:
        return ZX_ERR_NOT_SUPPORTED;
    }
}

static zx_off_t nvme_get_size(void* ctx) {
    nvme_device_t* nvme = ctx;
    return nvme->info.block_count * nvme->info.block_size;
}

static zx_status_t nvme_suspend(void* ctx, uint32_t flags) {
    return ZX_OK;
}

static zx_status_t nvme_resume(void* ctx, uint32_t flags) {
    return ZX_OK;
}

static void nvme_release(void* ctx) {
    nvme_device_t* nvme = ctx;
    int r;

    zxlogf(INFO, "nvme: release\n");
    nvme->flags |= FLAG_SHUTDOWN;
    if (nvme->ioh != ZX_HANDLE_INVALID) {
        pci_enable_bus_master(&nvme->pci, false);
        zx_handle_close(nvme->bti);
        zx_handle_close(nvme->ioh);
        // TODO: risks a handle use-after-close, will be resolved by IRQ api
        // changes coming soon
        zx_handle_close(nvme->irqh);
    }
    if (nvme->flags & FLAG_IRQ_THREAD_STARTED) {
        thrd_join(nvme->irqthread, &r);
    }
    if (nvme->flags & FLAG_IO_THREAD_STARTED) {
        completion_signal(&nvme->io_signal);
        thrd_join(nvme->iothread, &r);
    }

    // error out any pending txns
    mtx_lock(&nvme->lock);
    nvme_txn_t* txn;
    while ((txn = list_remove_head_type(&nvme->active_txns, nvme_txn_t, node)) != NULL) {
        txn_complete(txn, ZX_ERR_PEER_CLOSED);
    }
    while ((txn = list_remove_head_type(&nvme->pending_txns, nvme_txn_t, node)) != NULL) {
        txn_complete(txn, ZX_ERR_PEER_CLOSED);
    }
    mtx_unlock(&nvme->lock);

    io_buffer_release(&nvme->iob);
    free(nvme);
}

static zx_protocol_device_t device_ops = {
    .version = DEVICE_OPS_VERSION,

    .ioctl = nvme_ioctl,
    .get_size = nvme_get_size,

    .suspend = nvme_suspend,
    .resume = nvme_resume,
    .release = nvme_release,
};

static void infostring(const char* prefix, uint8_t* str, size_t len) {
    char tmp[len + 1];
    size_t i;
    for (i = 0; i < len; i++) {
        uint8_t c = str[i];
        if (c == 0) {
            break;
        }
        if ((c < ' ') || (c > 127)) {
            c = ' ';
        }
        tmp[i] = c;
    }
    tmp[i] = 0;
    while (i > 0) {
        i--;
        if (tmp[i] == ' ') {
            tmp[i] = 0;
        } else {
            break;
        }
    }
    zxlogf(INFO, "nvme: %s'%s'\n", prefix, tmp);
}

// Convenience accessors for BAR0 registers
#define rd32(r) readl(nvme->io + NVME_REG_##r)
#define rd64(r) readll(nvme->io + NVME_REG_##r)
#define wr32(v,r) writel(v, nvme->io + NVME_REG_##r)
#define wr64(v,r) writell(v, nvme->io + NVME_REG_##r)

// dedicated pages from the page pool
#define IDX_ADMIN_SQ   0
#define IDX_ADMIN_CQ   1
#define IDX_IO_SQ      2
#define IDX_IO_CQ      3
#define IDX_SCRATCH    4
#define IDX_UTXN_POOL  5 // this must always be last

#define IO_PAGE_COUNT  (IDX_UTXN_POOL + UTXN_COUNT)

static inline uint64_t U64(uint8_t* x) {
    return *((uint64_t*) (void*) x);
}
static inline uint32_t U32(uint8_t* x) {
    return *((uint32_t*) (void*) x);
}
static inline uint32_t U16(uint8_t* x) {
    return *((uint16_t*) (void*) x);
}

#define WAIT_MS 5000

static zx_status_t nvme_init(nvme_device_t* nvme) {
    uint32_t n = rd32(VS);
    uint64_t cap = rd64(CAP);

    zxlogf(INFO, "nvme: version %d.%d.%d\n", n >> 16, (n >> 8) & 0xFF, n & 0xFF);
    zxlogf(INFO, "nvme: page size: (MPSMIN): %u (MPSMAX): %u\n",
           (unsigned) (1 << NVME_CAP_MPSMIN(cap)),
           (unsigned) (1 << NVME_CAP_MPSMAX(cap)));
    zxlogf(INFO, "nvme: doorbell stride: %u\n", (unsigned) (1 << NVME_CAP_DSTRD(cap)));
    zxlogf(INFO, "nvme: timeout: %u ms\n", (unsigned) (1 << NVME_CAP_TO(cap)));
    zxlogf(INFO, "nvme: boot partition support (BPS): %c\n", NVME_CAP_BPS(cap) ? 'Y' : 'N');
    zxlogf(INFO, "nvme: supports NVM command set (CSS:NVM): %c\n", NVME_CAP_CSS_NVM(cap) ? 'Y' : 'N');
    zxlogf(INFO, "nvme: subsystem reset supported (NSSRS): %c\n", NVME_CAP_NSSRS(cap) ? 'Y' : 'N');
    zxlogf(INFO, "nvme: weighted-round-robin (AMS:WRR): %c\n", NVME_CAP_AMS_WRR(cap) ? 'Y' : 'N');
    zxlogf(INFO, "nvme: vendor-specific arbitration (AMS:VS): %c\n", NVME_CAP_AMS_VS(cap) ? 'Y' : 'N');
    zxlogf(INFO, "nvme: contiquous queues required (CQR): %c\n", NVME_CAP_CQR(cap) ? 'Y' : 'N');
    zxlogf(INFO, "nvme: maximum queue entries supported (MQES): %u\n", ((unsigned) NVME_CAP_MQES(cap)) + 1);

    if ((1 << NVME_CAP_MPSMIN(cap)) > PAGE_SIZE) {
        zxlogf(ERROR, "nvme: minimum page size larger than platform page size\n");
        return ZX_ERR_NOT_SUPPORTED;
    }
    // allocate pages for various queues and the utxn scatter lists
    // TODO: these should all be RO to hardware apart from the scratch io page(s)
    if (io_buffer_init(&nvme->iob, nvme->bti, PAGE_SIZE * IO_PAGE_COUNT, IO_BUFFER_RW) ||
        io_buffer_physmap(&nvme->iob)) {
        zxlogf(ERROR, "nvme: could not allocate io buffers\n");
        return ZX_ERR_NO_MEMORY;
    }

    // initialize the microtransaction pool
    nvme->utxn_avail = 0x7FFFFFFFFFFFFFFFULL;
    for (unsigned n = 0; n < UTXN_COUNT; n++) {
        nvme->utxn[n].id = n;
        nvme->utxn[n].phys = nvme->iob.phys_list[IDX_UTXN_POOL + n];
        nvme->utxn[n].virt = nvme->iob.virt + (IDX_UTXN_POOL + n) * PAGE_SIZE;
    }

    if (rd32(CSTS) & NVME_CSTS_RDY) {
        zxlogf(INFO, "nvme: controller is active. resetting...\n");
        wr32(rd32(CC) & ~NVME_CC_EN, CC); // disable
    }

    // ensure previous shutdown (by us or bootloader) has completed
    unsigned ms_remain = WAIT_MS;
    while (rd32(CSTS) & NVME_CSTS_RDY) {
        if (--ms_remain == 0) {
            zxlogf(ERROR, "nvme: timed out waiting for CSTS ~RDY\n");
            return ZX_ERR_INTERNAL;
        }
        zx_nanosleep(zx_deadline_after(ZX_MSEC(1)));
    }

    zxlogf(INFO, "nvme: controller inactive. (after %u ms)\n", WAIT_MS - ms_remain);

    // configure admin submission and completion queues
    wr64(nvme->iob.phys_list[IDX_ADMIN_SQ], ASQ);
    wr64(nvme->iob.phys_list[IDX_ADMIN_CQ], ACQ);
    wr32(NVME_AQA_ASQS(SQMAX - 1) | NVME_AQA_ACQS(CQMAX - 1), AQA);

    zxlogf(INFO, "nvme: enabling\n");
    wr32(NVME_CC_EN | NVME_CC_AMS_RR | NVME_CC_MPS(0) |
         NVME_CC_IOCQES(NVME_CPL_SHIFT) |
         NVME_CC_IOSQES(NVME_CMD_SHIFT), CC);

    ms_remain = WAIT_MS;
    while (!(rd32(CSTS) & NVME_CSTS_RDY)) {
        if (--ms_remain == 0) {
            zxlogf(ERROR, "nvme: timed out waiting for CSTS RDY\n");
            return ZX_ERR_INTERNAL;
        }
        zx_nanosleep(zx_deadline_after(ZX_MSEC(1)));
    }
    zxlogf(INFO, "nvme: controller ready. (after %u ms)\n", WAIT_MS - ms_remain);

    // registers and buffers for admin queues
    nvme->io_admin_sq_tail_db = nvme->io + NVME_REG_SQnTDBL(0, cap);
    nvme->io_admin_cq_head_db = nvme->io + NVME_REG_CQnHDBL(0, cap);

    nvme->admin_sq = nvme->iob.virt + PAGE_SIZE * IDX_ADMIN_SQ;
    nvme->admin_sq_head = 0;
    nvme->admin_sq_tail = 0;

    nvme->admin_cq = nvme->iob.virt + PAGE_SIZE * IDX_ADMIN_CQ;
    nvme->admin_cq_head = 0;
    nvme->admin_cq_toggle = 1;

    // registers and buffers for IO queues
    nvme->io_sq_tail_db = nvme->io + NVME_REG_SQnTDBL(1, cap);
    nvme->io_cq_head_db = nvme->io + NVME_REG_CQnHDBL(1, cap);

    nvme->io_sq = nvme->iob.virt + PAGE_SIZE * IDX_IO_SQ;
    nvme->io_sq_head = 0;
    nvme->io_sq_tail = 0;

    nvme->io_cq = nvme->iob.virt + PAGE_SIZE * IDX_IO_CQ;
    nvme->io_cq_head = 0;
    nvme->io_cq_toggle = 1;

    // scratch page for admin ops
    void* scratch = nvme->iob.virt + PAGE_SIZE * IDX_SCRATCH;

    if (thrd_create_with_name(&nvme->irqthread, irq_thread, nvme, "nvme-irq-thread")) {
        zxlogf(ERROR, "nvme; cannot create irq thread\n");
        return ZX_ERR_INTERNAL;
    }
    nvme->flags |= FLAG_IRQ_THREAD_STARTED;

    if (thrd_create_with_name(&nvme->iothread, io_thread, nvme, "nvme-io-thread")) {
        zxlogf(ERROR, "nvme; cannot create io thread\n");
        return ZX_ERR_INTERNAL;
    }
    nvme->flags |= FLAG_IO_THREAD_STARTED;

    nvme_cmd_t cmd;

    // identify device
    cmd.cmd = NVME_CMD_CID(0) | NVME_CMD_PRP | NVME_CMD_NORMAL | NVME_CMD_OPC(NVME_ADMIN_OP_IDENTIFY);
    cmd.nsid = 0;
    cmd.reserved = 0;
    cmd.mptr = 0;
    cmd.dptr.prp[0] = nvme->iob.phys_list[IDX_SCRATCH];
    cmd.dptr.prp[1] = 0;
    cmd.u.raw[0] = 1; // CNS 01

    if (nvme_admin_txn(nvme, &cmd, NULL) != ZX_OK) {
        zxlogf(ERROR, "nvme: device identify op failed\n");
        return ZX_ERR_INTERNAL;
    }

    nvme_identify_t* ci = scratch;
    infostring("model:         ", ci->MN, sizeof(ci->MN));
    infostring("serial number: ", ci->SN, sizeof(ci->SN));
    infostring("firmware:      ", ci->FR, sizeof(ci->FR));

    if ((ci->SQES & 0xF) != NVME_CMD_SHIFT) {
        zxlogf(ERROR, "nvme: SQES minimum is not %ub\n", NVME_CMD_SIZE);
        return ZX_ERR_NOT_SUPPORTED;
    }
    if ((ci->CQES & 0xF) != NVME_CPL_SHIFT) {
        zxlogf(ERROR, "nvme: CQES minimum is not %ub\n", NVME_CPL_SIZE);
        return ZX_ERR_NOT_SUPPORTED;
    }
    zxlogf(INFO, "nvme: max outstanding commands: %u\n", ci->MAXCMD);

    uint32_t nscount = ci->NN;
    zxlogf(INFO, "nvme: max namespaces: %u\n", nscount);
    zxlogf(INFO, "nvme: scatter gather lists (SGL): %c %08x\n",
           (ci->SGLS & 3) ? 'Y' : 'N', ci->SGLS);

    // Maximum transfer is in units of 2^n * PAGESIZE, n == 0 means "infinite"
    nvme->max_xfer = 0xFFFFFFFF;
    if ((ci->MDTS != 0) && (ci->MDTS < (31 - PAGE_SHIFT))) {
        nvme->max_xfer = (1 << ci->MDTS) * PAGE_SIZE;
    }

    zxlogf(INFO, "nvme: max data transfer: %u bytes\n", nvme->max_xfer);
    zxlogf(INFO, "nvme: sanitize caps: %u\n", ci->SANICAP & 3);

    zxlogf(INFO, "nvme: abort command limit (ACL): %u\n", ci->ACL + 1);
    zxlogf(INFO, "nvme: asynch event req limit (AERL): %u\n", ci->AERL + 1);
    zxlogf(INFO, "nvme: firmware: slots: %u reset: %c slot1ro: %c\n", (ci->FRMW >> 1) & 3,
           (ci->FRMW & (1 << 4)) ? 'N' : 'Y', (ci->FRMW & 1) ? 'Y' : 'N');
    zxlogf(INFO, "nvme: host buffer: min/preferred: %u/%u pages\n", ci->HMMIN, ci->HMPRE);
    zxlogf(INFO, "nvme: capacity: total/unalloc: %zu/%zu\n", ci->TNVMCAP_LO, ci->UNVMCAP_LO);

    if (ci->VWC & 1) {
        nvme->flags |= FLAG_HAS_VWC;
    }
    uint32_t awun = ci->AWUN + 1;
    uint32_t awupf = ci->AWUPF + 1;
    zxlogf(INFO, "nvme: volatile write cache (VWC): %s\n", nvme->flags & FLAG_HAS_VWC ? "Y" : "N");
    zxlogf(INFO, "nvme: atomic write unit (AWUN)/(AWUPF): %u/%u blks\n", awun, awupf);

#define FEATURE(a,b) if (ci->a & a##_##b) zxlogf(INFO, "nvme: feature: %s\n", #b)
    FEATURE(OACS, DOORBELL_BUFFER_CONFIG);
    FEATURE(OACS, VIRTUALIZATION_MANAGEMENT);
    FEATURE(OACS, NVME_MI_SEND_RECV);
    FEATURE(OACS, DIRECTIVE_SEND_RECV);
    FEATURE(OACS, DEVICE_SELF_TEST);
    FEATURE(OACS, NAMESPACE_MANAGEMENT);
    FEATURE(OACS, FIRMWARE_DOWNLOAD_COMMIT);
    FEATURE(OACS, FORMAT_NVM);
    FEATURE(OACS, SECURITY_SEND_RECV);
    FEATURE(ONCS, TIMESTAMP);
    FEATURE(ONCS, RESERVATIONS);
    FEATURE(ONCS, SAVE_SELECT_NONZERO);
    FEATURE(ONCS, WRITE_UNCORRECTABLE);
    FEATURE(ONCS, COMPARE);

    // set feature (number of queues) to 1 iosq and 1 iocq
    memset(&cmd, 0, sizeof(cmd));
    cmd.cmd = NVME_CMD_CID(0) | NVME_CMD_PRP | NVME_CMD_NORMAL | NVME_CMD_OPC(NVME_ADMIN_OP_SET_FEATURE);
    cmd.u.raw[0] = NVME_FEATURE_NUMBER_OF_QUEUES;
    cmd.u.raw[1] = 0;

    nvme_cpl_t cpl;
    if (nvme_admin_txn(nvme, &cmd, &cpl) != ZX_OK) {
        zxlogf(ERROR, "nvme: set feature (number queues) op failed\n");
        return ZX_ERR_INTERNAL;
    }
    zxlogf(INFO,"cpl.cmd %08x\n", cpl.cmd);

    // create the IO completion queue
    memset(&cmd, 0, sizeof(cmd));
    cmd.cmd = NVME_CMD_CID(0) | NVME_CMD_PRP | NVME_CMD_NORMAL | NVME_CMD_OPC(NVME_ADMIN_OP_CREATE_IOCQ);
    cmd.dptr.prp[0] = nvme->iob.phys_list[IDX_IO_CQ];
    cmd.u.raw[0] = ((CQMAX - 1) << 16) | 1; // queue size, queue id
    cmd.u.raw[1] = (0 << 16) | 2 | 1; // irq vector, irq enable, phys contig

    if (nvme_admin_txn(nvme, &cmd, NULL) != ZX_OK) {
        zxlogf(ERROR, "nvme: completion queue creation op failed\n");
        return ZX_ERR_INTERNAL;
    }

    // create the IO submit queue
    memset(&cmd, 0, sizeof(cmd));
    cmd.cmd = NVME_CMD_CID(0) | NVME_CMD_PRP | NVME_CMD_NORMAL | NVME_CMD_OPC(NVME_ADMIN_OP_CREATE_IOSQ);
    cmd.dptr.prp[0] = nvme->iob.phys_list[IDX_IO_SQ];
    cmd.u.raw[0] = ((SQMAX - 1) << 16) | 1; // queue size, queue id
    cmd.u.raw[1] = (1 << 16) | 0 | 1; // cqid, qprio, phys contig

    if (nvme_admin_txn(nvme, &cmd, NULL) != ZX_OK) {
        zxlogf(ERROR, "nvme: submit queue creation op failed\n");
        return ZX_ERR_INTERNAL;
    }

    // identify namespace 1
    memset(&cmd, 0, sizeof(cmd));
    cmd.cmd = NVME_CMD_CID(0) | NVME_CMD_PRP | NVME_CMD_NORMAL | NVME_CMD_OPC(NVME_ADMIN_OP_IDENTIFY);
    cmd.nsid = 1;
    cmd.dptr.prp[0] = nvme->iob.phys_list[IDX_SCRATCH];

    if (nvme_admin_txn(nvme, &cmd, NULL) != ZX_OK) {
        zxlogf(ERROR, "nvme: namespace identify op failed\n");
        return ZX_ERR_INTERNAL;
    }

    nvme_identify_ns_t* ni = scratch;

    uint32_t nawun = (ni->NSFEAT & NSFEAT_LOCAL_ATOMIC_SIZES) ? (ni->NAWUN + 1U) : awun;
    uint32_t nawupf = (ni->NSFEAT & NSFEAT_LOCAL_ATOMIC_SIZES) ? (ni->NAWUPF + 1U) : awupf;
    zxlogf(INFO, "nvme: ns: atomic write unit (AWUN)/(AWUPF): %u/%u blks\n", nawun, nawupf);
    zxlogf(INFO, "nvme: ns: NABSN/NABO/NABSPF/NOIOB: %u/%u/%u/%u\n",
           ni->NABSN, ni->NABO, ni->NABSPF, ni->NOIOB);

    // table of block formats
    for (unsigned i = 0; i < 16; i++) {
        if (ni->LBAF[i]) {
            zxlogf(INFO, "nvme: ns: LBA FMT %02d: RP=%u LBADS=2^%ub MS=%ub\n",
                    i, NVME_LBAFMT_RP(ni->LBAF[i]), NVME_LBAFMT_LBADS(ni->LBAF[i]),
                    NVME_LBAFMT_MS(ni->LBAF[i]));
        }
    }

    zxlogf(INFO, "nvme: ns: LBA FMT #%u active\n", ni->FLBAS & 0xF);
    zxlogf(INFO, "nvme: ns: data protection: caps/set: 0x%02x/%u\n",
           ni->DPC & 0x3F, ni->DPS & 3);

    uint32_t fmt = ni->LBAF[ni->FLBAS & 0xF];

    zxlogf(INFO, "nvme: ns: size/cap/util: %zu/%zu/%zu blks\n", ni->NSSZ, ni->NCAP, ni->NUSE);

    nvme->info.block_count = ni->NSSZ;
    nvme->info.block_size = 1 << NVME_LBAFMT_LBADS(fmt);
    nvme->info.max_transfer_size = 0xFFFFFFFF;

    if (NVME_LBAFMT_MS(fmt)) {
        zxlogf(ERROR, "nvme: cannot handle LBA format with metadata\n");
        return ZX_ERR_NOT_SUPPORTED;
    }
    if ((nvme->info.block_size < 512) || (nvme->info.block_size > 32768)) {
        zxlogf(ERROR, "nvme: cannot handle LBA size of %u\n", nvme->info.block_size);
        return ZX_ERR_NOT_SUPPORTED;
    }

    // NVME r/w commands operate in block units, maximum of 64K:
    size_t max_bytes_per_cmd = ((size_t) nvme->info.block_size) * ((size_t) 65536);

    if (nvme->max_xfer > max_bytes_per_cmd) {
        nvme->max_xfer = max_bytes_per_cmd;
    }

    // The device may allow transfers larger than we are prepared
    // to handle.  Clip to our limit.
    if (nvme->max_xfer > MAX_XFER) {
        nvme->max_xfer = MAX_XFER;
    }

    // convert to block units
    nvme->max_xfer /= nvme->info.block_size;
    zxlogf(INFO, "nvme: max transfer per r/w op: %u blocks (%u bytes)\n",
           nvme->max_xfer, nvme->max_xfer * nvme->info.block_size);

    device_make_visible(nvme->zxdev);
    return ZX_OK;
}

block_protocol_ops_t block_ops = {
    .query = nvme_query,
    .queue = nvme_queue,
};

static zx_status_t nvme_bind(void* ctx, zx_device_t* dev) {
    nvme_device_t* nvme;
    if ((nvme = calloc(1, sizeof(nvme_device_t))) == NULL) {
        return ZX_ERR_NO_MEMORY;
    }
    list_initialize(&nvme->pending_txns);
    list_initialize(&nvme->active_txns);
    mtx_init(&nvme->lock, mtx_plain);
    mtx_init(&nvme->admin_lock, mtx_plain);

    if (device_get_protocol(dev, ZX_PROTOCOL_PCI, &nvme->pci)) {
        goto fail;
    }

    if (pci_map_bar(&nvme->pci, 0u, ZX_CACHE_POLICY_UNCACHED_DEVICE,
                    &nvme->io, &nvme->iosz, &nvme->ioh)) {
        zxlogf(ERROR, "nvme: cannot map registers\n");
        goto fail;
    }

    uint32_t modes[3] = {
        ZX_PCIE_IRQ_MODE_MSI_X, ZX_PCIE_IRQ_MODE_MSI, ZX_PCIE_IRQ_MODE_LEGACY,
    };
    uint32_t nirq = 0;
    for (unsigned n = 0; n < countof(modes); n++) {
        if ((pci_query_irq_mode(&nvme->pci, modes[n], &nirq) == ZX_OK) &&
            (pci_set_irq_mode(&nvme->pci, modes[n], 1) == ZX_OK)) {
            zxlogf(INFO, "nvme: irq mode %u, irq count %u (#%u)\n", modes[n], nirq, n);
            goto irq_configured;
        }
    }
    zxlogf(ERROR, "nvme: could not configure irqs\n");
    goto fail;

irq_configured:
    if (pci_map_interrupt(&nvme->pci, 0, &nvme->irqh) != ZX_OK) {
        zxlogf(ERROR, "nvme: could not map irq\n");
        goto fail;
    }
    if (pci_enable_bus_master(&nvme->pci, true)) {
        zxlogf(ERROR, "nvme: cannot enable bus mastering\n");
        goto fail;
    }
    if (pci_get_bti(&nvme->pci, 0, &nvme->bti) != ZX_OK) {
        zxlogf(ERROR, "nvme: cannot obtain bti handle\n");
        goto fail;
    }

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "nvme",
        .ctx = nvme,
        .ops = &device_ops,
        .flags = DEVICE_ADD_INVISIBLE,
        .proto_id = ZX_PROTOCOL_BLOCK_IMPL,
        .proto_ops = &block_ops,
    };

    if (device_add(dev, &args, &nvme->zxdev)) {
        goto fail;
    }

    if (nvme_init(nvme) != ZX_OK) {
        zxlogf(ERROR, "nvme: init failed\n");
        device_remove(nvme->zxdev);
        return ZX_ERR_INTERNAL;
    }

    return ZX_OK;

fail:
    nvme_release(nvme);
    return ZX_ERR_NOT_SUPPORTED;
}

static zx_driver_ops_t driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = nvme_bind,
};

ZIRCON_DRIVER_BEGIN(nvme, driver_ops, "zircon", "0.1", 4)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PCI),
    BI_ABORT_IF(NE, BIND_PCI_CLASS, 1), // Mass Storage
    BI_ABORT_IF(NE, BIND_PCI_SUBCLASS, 8), // NVM
    BI_MATCH_IF(EQ, BIND_PCI_INTERFACE, 2), // NVMHCI
ZIRCON_DRIVER_END(nvme)
