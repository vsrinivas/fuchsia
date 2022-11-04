// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/block/drivers/nvme/nvme.h"

#include <assert.h>
#include <fuchsia/hardware/block/c/banjo.h>
#include <fuchsia/hardware/pci/c/banjo.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <lib/ddk/hw/reg.h>
#include <lib/ddk/io-buffer.h>
#include <lib/device-protocol/pci.h>
#include <lib/fit/defer.h>
#include <lib/mmio/mmio-buffer.h>
#include <lib/sync/completion.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <zircon/listnode.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <fbl/auto_lock.h>
#include <fbl/mutex.h>

#include "nvme-hw.h"
#include "src/devices/block/drivers/nvme/commands/nvme-io.h"
#include "src/devices/block/drivers/nvme/nvme_bind.h"
#include "src/devices/block/drivers/nvme/registers.h"

namespace nvme {

#define TXN_FLAG_FAILED 1

struct nvme_txn_t {
  block_op_t op;
  list_node_t node;
  block_impl_queue_callback completion_cb;
  void* cookie;
  uint16_t pending_utxns;
  uint8_t opcode;
  uint8_t flags;
};

struct nvme_utxn_t {
  zx_paddr_t phys;  // io buffer phys base (1 page)
  void* virt;       // io buffer virt base
  zx_handle_t pmt;  // pinned memory
  nvme_txn_t* txn;  // related txn
  uint16_t id;
  uint16_t reserved0;
  uint32_t reserved1;
};

#define UTXN_COUNT 63

// Limit maximum transfer size to 1MB which fits comfortably
// within our single scatter gather page per utxn setup
#define MAX_XFER (1024 * 1024)

// Maximum submission and completion queue item counts, for
// queues that are a single page in size.
#define SQMAX(PageSize) ((PageSize) / sizeof(nvme_cmd_t))
#define CQMAX(PageSize) ((PageSize) / sizeof(nvme_cpl_t))

// global driver state bits
#define FLAG_IRQ_THREAD_STARTED 0x0001
#define FLAG_IO_THREAD_STARTED 0x0002
#define FLAG_SHUTDOWN 0x0004

#define FLAG_HAS_VWC 0x0100

struct nvme_device_t {
  uint32_t flags;
  fbl::Mutex lock;

  uint64_t utxn_avail;  // bitmask of available utxns

  // The pending list is txns that have been received
  // via nvme_queue() and are waiting for io to start.
  // The exception is the head of the pending list which may
  // be partially started, waiting for more utxns to become
  // available.
  // The active list consists of txns where all utxns have
  // been created and we're waiting for them to complete or
  // error out.
  list_node_t pending_txns;  // inbound txns to process
  list_node_t active_txns;   // txns in flight

  // The io signal completion is signaled from nvme_queue()
  // or from the irq thread, notifying the io thread that
  // it has work to do.
  sync_completion_t io_signal;

  uint32_t max_xfer;
  block_info_t info;

  // admin queue doorbell registers
  MMIO_PTR void* io_admin_sq_tail_db;
  MMIO_PTR void* io_admin_cq_head_db;

  // admin queues and state
  nvme_cpl_t* admin_cq;
  nvme_cmd_t* admin_sq;
  uint16_t admin_cq_head;
  uint16_t admin_cq_toggle;
  uint16_t admin_sq_tail;
  uint16_t admin_sq_head;

  // context for admin transactions
  // presently we serialize these under the admin_lock
  fbl::Mutex admin_lock;
  sync_completion_t admin_signal;
  nvme_cpl_t admin_result;

  // source of physical pages for queues and admin commands
  io_buffer_t iob;

  // pool of utxns
  nvme_utxn_t utxn[UTXN_COUNT];
};

// Takes the log2 of a page size to turn it into a shift.
static inline size_t PageShift(size_t page_size) {
  return (sizeof(int) * 8) - __builtin_clz((int)page_size) - 1;
}

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
  return nvme->utxn + n;
}

static void utxn_put(nvme_device_t* nvme, nvme_utxn_t* utxn) {
  uint64_t n = utxn->id;
  nvme->utxn_avail |= (1ULL << n);
}

static zx_status_t nvme_admin_cq_get(nvme_device_t* nvme, nvme_cpl_t* cpl) {
  if ((readw(&nvme->admin_cq[nvme->admin_cq_head].status) & 1) != nvme->admin_cq_toggle) {
    return ZX_ERR_SHOULD_WAIT;
  }
  *cpl = nvme->admin_cq[nvme->admin_cq_head];

  // advance the head pointer, wrapping and inverting toggle at max
  uint16_t next = (nvme->admin_cq_head + 1) & (CQMAX(zx_system_get_page_size()) - 1);
  if ((nvme->admin_cq_head = next) == 0) {
    nvme->admin_cq_toggle ^= 1;
  }

  // note the new sq head reported by hw
  nvme->admin_sq_head = cpl->sq_head;

  // ring the doorbell
  MmioWrite32(next, static_cast<MMIO_PTR uint32_t*>(nvme->io_admin_cq_head_db));
  return ZX_OK;
}

static zx_status_t nvme_admin_sq_put(nvme_device_t* nvme, nvme_cmd_t* cmd) {
  uint16_t next = (nvme->admin_sq_tail + 1) & (SQMAX(zx_system_get_page_size()) - 1);

  // if head+1 == tail: queue is full
  if (next == nvme->admin_sq_head) {
    return ZX_ERR_SHOULD_WAIT;
  }

  nvme->admin_sq[nvme->admin_sq_tail] = *cmd;
  nvme->admin_sq_tail = next;

  // ring the doorbell
  MmioWrite32(next, static_cast<MMIO_PTR uint32_t*>(nvme->io_admin_sq_tail_db));
  return ZX_OK;
}

int Nvme::IrqLoop() {
  nvme_device_t* nvme = nvme_;

  for (;;) {
    zx_status_t status;
    if ((status = zx_interrupt_wait(irqh_, NULL)) != ZX_OK) {
      zxlogf(ERROR, "irq wait failed: %s", zx_status_get_string(status));
      break;
    }

    nvme_cpl_t cpl;
    if (nvme_admin_cq_get(nvme, &cpl) == ZX_OK) {
      nvme->admin_result = cpl;
      sync_completion_signal(&nvme->admin_signal);
    }

    sync_completion_signal(&nvme->io_signal);
  }
  return 0;
}

static zx_status_t nvme_admin_txn(nvme_device_t* nvme, nvme_cmd_t* cmd, nvme_cpl_t* cpl) {
  zx_status_t status;
  fbl::AutoLock lock(&nvme->admin_lock);
  sync_completion_reset(&nvme->admin_signal);
  if ((status = nvme_admin_sq_put(nvme, cmd)) != ZX_OK) {
    return status;
  }
  if ((status = sync_completion_wait(&nvme->admin_signal, ZX_SEC(1))) != ZX_OK) {
    zxlogf(ERROR, "admin txn: %s", zx_status_get_string(status));
    return status;
  }

  unsigned code = NVME_CPL_STATUS_CODE(nvme->admin_result.status);
  if (code != 0) {
    zxlogf(ERROR, "admin txn: nvm error %03x", code);
    status = ZX_ERR_IO;
  }
  if (cpl != NULL) {
    *cpl = nvme->admin_result;
  }
  return status;
}

static inline void txn_complete(nvme_txn_t* txn, zx_status_t status) {
  txn->completion_cb(txn->cookie, status, &txn->op);
}

bool Nvme::IoProcessTxn(nvme_txn_t* txn) {
  nvme_device_t* nvme = nvme_;

  zx_handle_t vmo = txn->op.rw.vmo;
  nvme_utxn_t* utxn;
  zx_paddr_t* pages;
  zx_status_t status;

  const size_t kPageSize = zx_system_get_page_size();
  const size_t kPageMask = kPageSize - 1;
  const size_t kPageShift = PageShift(kPageSize);

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
    size_t bytes = ((size_t)blocks) * ((size_t)nvme->info.block_size);

    // Page offset of first page of transfer
    size_t pageoffset = txn->op.rw.offset_vmo & (~kPageMask);

    // Byte offset into first page of transfer
    size_t byteoffset = txn->op.rw.offset_vmo & kPageMask;

    // Total pages mapped / touched
    size_t pagecount = (byteoffset + bytes + kPageMask) >> kPageShift;

    // read disk (OP_READ) -> memory (PERM_WRITE) or
    // write memory (PERM_READ) -> disk (OP_WRITE)
    uint32_t opt = (txn->opcode == NVME_OP_READ) ? ZX_BTI_PERM_WRITE : ZX_BTI_PERM_READ;

    pages = static_cast<zx_paddr_t*>(utxn->virt);

    if ((status = zx_bti_pin(bti_.get(), opt, vmo, pageoffset, pagecount << kPageShift, pages,
                             pagecount, &utxn->pmt)) != ZX_OK) {
      zxlogf(ERROR, "could not pin pages: %s", zx_status_get_string(status));
      break;
    }

    NvmIoSubmission submission(txn->opcode == NVME_OP_WRITE);
    submission.namespace_id = 1;
    ZX_ASSERT(blocks - 1 <= UINT16_MAX);
    submission.set_start_lba(txn->op.rw.offset_dev).set_block_count(blocks - 1);
    // The NVME command has room for two data pointers inline.
    // The first is always the pointer to the first page where data is.
    // The second is the second page if pagecount is 2.
    // The second is the address of an array of page 2..n if pagecount > 2
    submission.data_pointer[0] = pages[0] | byteoffset;
    if (pagecount == 2) {
      submission.data_pointer[1] = pages[1];
    } else if (pagecount > 2) {
      submission.data_pointer[1] = utxn->phys + sizeof(uint64_t);
    }

    zxlogf(TRACE, "txn=%p utxn id=%u pages=%zu op=%s", txn, utxn->id, pagecount,
           txn->opcode == NVME_OP_WRITE ? "WR" : "RD");
    zxlogf(TRACE, "prp[0]=%016zx prp[1]=%016zx", submission.data_pointer[0],
           submission.data_pointer[1]);
    zxlogf(TRACE, "pages[] = { %016zx, %016zx, %016zx, %016zx, ... }", pages[0], pages[1], pages[2],
           pages[3]);

    zx::result<> status =
        io_queue_->Submit(submission, std::nullopt, txn->op.rw.offset_vmo, utxn->id);
    if (status.is_error()) {
      zxlogf(ERROR, "Failed to submit cmd (txn=%p id=%u): %s", txn, utxn->id,
             status.status_string());
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
      fbl::AutoLock lock(&nvme->lock);
      list_add_tail(&nvme->active_txns, &txn->node);
      return false;
    }
  }

  // failure
  if ((status = zx_pmt_unpin(utxn->pmt)) != ZX_OK) {
    zxlogf(ERROR, "cannot unpin io buffer: %s", zx_status_get_string(status));
  }
  utxn_put(nvme, utxn);

  {
    fbl::AutoLock lock(&nvme->lock);
    txn->flags |= TXN_FLAG_FAILED;
    if (txn->pending_utxns) {
      // if there are earlier uncompleted IOs we become active now
      // and will finish erroring out when they complete
      list_add_tail(&nvme->active_txns, &txn->node);
      txn = NULL;
    }
  }

  if (txn != NULL) {
    txn_complete(txn, ZX_ERR_INTERNAL);
  }

  // Either way we tell the caller not to retain the txn (false)
  return false;
}

void Nvme::IoProcessTxns() {
  nvme_device_t* nvme = nvme_;

  nvme_txn_t* txn;

  for (;;) {
    {
      fbl::AutoLock lock(&nvme->lock);
      txn = list_remove_head_type(&nvme->pending_txns, nvme_txn_t, node);
    }

    if (txn == NULL) {
      return;
    }

    if (IoProcessTxn(txn)) {
      // put txn back at front of queue for further processing later
      fbl::AutoLock lock(&nvme->lock);
      list_add_head(&nvme->pending_txns, &txn->node);
      return;
    }
  }
}

void Nvme::IoProcessCpls() {
  nvme_device_t* nvme = nvme_;

  bool ring_doorbell = false;
  Completion* cpl;

  while (io_queue_->CheckForNewCompletion(&cpl) != ZX_ERR_SHOULD_WAIT) {
    ring_doorbell = true;

    if (cpl->command_id() >= UTXN_COUNT) {
      zxlogf(ERROR, "unexpected cmd id %u", cpl->command_id());
      continue;
    }
    nvme_utxn_t* utxn = nvme->utxn + cpl->command_id();
    nvme_txn_t* txn = utxn->txn;

    if (txn == NULL) {
      zxlogf(ERROR, "inactive utxn #%u completed?!", cpl->command_id());
      continue;
    }

    if (cpl->status_code_type() != StatusCodeType::kGeneric || cpl->status_code() != 0) {
      zxlogf(ERROR, "utxn #%u txn %p failed: status type=%01x, status=%02x", cpl->command_id(), txn,
             cpl->status_code_type(), cpl->status_code());
      txn->flags |= TXN_FLAG_FAILED;
      // discard any remaining bytes -- no reason to keep creating
      // further utxns once one has failed
      txn->op.rw.length = 0;
    } else {
      zxlogf(TRACE, "utxn #%u txn %p OKAY", cpl->command_id(), txn);
    }

    zx_status_t status = zx_pmt_unpin(utxn->pmt);
    if (status != ZX_OK) {
      zxlogf(ERROR, "cannot unpin io buffer: %s", zx_status_get_string(status));
    }

    // release the microtransaction
    utxn->txn = NULL;
    utxn_put(nvme, utxn);

    txn->pending_utxns--;
    if ((txn->pending_utxns == 0) && (txn->op.rw.length == 0)) {
      // remove from either pending or active list
      {
        fbl::AutoLock lock(&nvme->lock);
        list_delete(&txn->node);
      }
      zxlogf(TRACE, "txn %p %s", txn, txn->flags & TXN_FLAG_FAILED ? "error" : "okay");
      txn_complete(txn, txn->flags & TXN_FLAG_FAILED ? ZX_ERR_IO : ZX_OK);
    }
  }

  if (ring_doorbell) {
    io_queue_->RingCompletionDb();
  }
}

int Nvme::IoLoop() {
  nvme_device_t* nvme = nvme_;

  for (;;) {
    if (sync_completion_wait(&nvme->io_signal, ZX_TIME_INFINITE)) {
      break;
    }
    if (nvme->flags & FLAG_SHUTDOWN) {
      // TODO: cancel out pending IO
      zxlogf(DEBUG, "io thread exiting");
      break;
    }

    sync_completion_reset(&nvme->io_signal);

    // process completion messages
    IoProcessCpls();

    // process work queue
    IoProcessTxns();
  }
  return 0;
}

void Nvme::BlockImplQueue(block_op_t* op, block_impl_queue_callback completion_cb, void* cookie) {
  nvme_device_t* nvme = nvme_;

  nvme_txn_t* txn = containerof(op, nvme_txn_t, op);
  txn->completion_cb = completion_cb;
  txn->cookie = cookie;

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

  zxlogf(TRACE, "io: %s: %ublks @ blk#%zu", txn->opcode == NVME_OP_WRITE ? "wr" : "rd",
         txn->op.rw.length + 1U, txn->op.rw.offset_dev);

  {
    fbl::AutoLock lock(&nvme->lock);
    list_add_tail(&nvme->pending_txns, &txn->node);
  }

  sync_completion_signal(&nvme->io_signal);
}

void Nvme::BlockImplQuery(block_info_t* info_out, size_t* block_op_size_out) {
  nvme_device_t* nvme = nvme_;

  *info_out = nvme->info;
  *block_op_size_out = sizeof(nvme_txn_t);
}

void Nvme::DdkRelease() {
  nvme_device_t* nvme = nvme_;

  int r;

  zxlogf(DEBUG, "release");
  nvme->flags |= FLAG_SHUTDOWN;
  if (mmio_->get_vmo() != ZX_HANDLE_INVALID) {
    pci_set_bus_mastering(&pci_, false);
    zx_handle_close(bti_.get());
    // TODO: risks a handle use-after-close, will be resolved by IRQ api
    // changes coming soon
    zx_handle_close(irqh_);
  }
  if (nvme->flags & FLAG_IRQ_THREAD_STARTED) {
    thrd_join(irq_thread_, &r);
  }
  if (nvme->flags & FLAG_IO_THREAD_STARTED) {
    sync_completion_signal(&nvme->io_signal);
    thrd_join(io_thread_, &r);
  }

  // error out any pending txns
  {
    fbl::AutoLock lock(&nvme->lock);
    nvme_txn_t* txn;
    while ((txn = list_remove_head_type(&nvme->active_txns, nvme_txn_t, node)) != NULL) {
      txn_complete(txn, ZX_ERR_PEER_CLOSED);
    }
    while ((txn = list_remove_head_type(&nvme->pending_txns, nvme_txn_t, node)) != NULL) {
      txn_complete(txn, ZX_ERR_PEER_CLOSED);
    }
  }

  io_buffer_release(&nvme->iob);
  free(nvme);
}

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
  zxlogf(INFO, "%s'%s'", prefix, tmp);
}

// Convenience accessors for BAR0 registers
#define MmioVaddr static_cast<MMIO_PTR uint8_t*>(mmio_->get())
#define rd32(r) MmioRead32(reinterpret_cast<MMIO_PTR uint32_t*>(MmioVaddr + NVME_REG_##r))
#define rd64(r) MmioRead64(reinterpret_cast<MMIO_PTR uint64_t*>(MmioVaddr + NVME_REG_##r))
#define wr32(v, r) MmioWrite32(v, reinterpret_cast<MMIO_PTR uint32_t*>(MmioVaddr + NVME_REG_##r))
#define wr64(v, r) MmioWrite64(v, reinterpret_cast<MMIO_PTR uint64_t*>(MmioVaddr + NVME_REG_##r))

// dedicated pages from the page pool
#define IDX_ADMIN_SQ 0
#define IDX_ADMIN_CQ 1
#define IDX_SCRATCH 2
#define IDX_UTXN_POOL 3  // this must always be last

#define IO_PAGE_COUNT (IDX_UTXN_POOL + UTXN_COUNT)

#define WAIT_MS 5000

void Nvme::DdkInit(ddk::InitTxn txn) {
  nvme_device_t* nvme = nvme_;

  auto cleanup = fit::defer([] { zxlogf(ERROR, "init failed"); });

  uint32_t n = rd32(VS);
  uint64_t cap = rd64(CAP);

  zxlogf(INFO, "version %d.%d.%d", n >> 16, (n >> 8) & 0xFF, n & 0xFF);
  zxlogf(DEBUG, "page size: (MPSMIN): %u (MPSMAX): %u", (unsigned)(1 << NVME_CAP_MPSMIN(cap)),
         (unsigned)(1 << NVME_CAP_MPSMAX(cap)));
  zxlogf(DEBUG, "doorbell stride: %u", (unsigned)(1 << NVME_CAP_DSTRD(cap)));
  zxlogf(DEBUG, "timeout: %u ms", (unsigned)(1 << NVME_CAP_TO(cap)));
  zxlogf(DEBUG, "boot partition support (BPS): %c", NVME_CAP_BPS(cap) ? 'Y' : 'N');
  zxlogf(DEBUG, "supports NVM command set (CSS:NVM): %c", NVME_CAP_CSS_NVM(cap) ? 'Y' : 'N');
  zxlogf(DEBUG, "subsystem reset supported (NSSRS): %c", NVME_CAP_NSSRS(cap) ? 'Y' : 'N');
  zxlogf(DEBUG, "weighted-round-robin (AMS:WRR): %c", NVME_CAP_AMS_WRR(cap) ? 'Y' : 'N');
  zxlogf(DEBUG, "vendor-specific arbitration (AMS:VS): %c", NVME_CAP_AMS_VS(cap) ? 'Y' : 'N');
  zxlogf(DEBUG, "contiquous queues required (CQR): %c", NVME_CAP_CQR(cap) ? 'Y' : 'N');
  zxlogf(DEBUG, "maximum queue entries supported (MQES): %u", ((unsigned)NVME_CAP_MQES(cap)) + 1);

  const size_t kPageSize = zx_system_get_page_size();

  if ((1 << NVME_CAP_MPSMIN(cap)) > kPageSize) {
    zxlogf(ERROR, "minimum page size larger than platform page size");
    txn.Reply(ZX_ERR_NOT_SUPPORTED);
    return;
  }
  // allocate pages for various queues and the utxn scatter lists
  // TODO: these should all be RO to hardware apart from the scratch io page(s)
  if (io_buffer_init(&nvme->iob, bti_.get(), kPageSize * IO_PAGE_COUNT, IO_BUFFER_RW) ||
      io_buffer_physmap(&nvme->iob)) {
    zxlogf(ERROR, "could not allocate io buffers");
    txn.Reply(ZX_ERR_NO_MEMORY);
    return;
  }

  // initialize the microtransaction pool
  nvme->utxn_avail = 0x7FFFFFFFFFFFFFFFULL;
  for (uint16_t n = 0; n < UTXN_COUNT; n++) {
    nvme->utxn[n].id = n;
    nvme->utxn[n].phys = nvme->iob.phys_list[IDX_UTXN_POOL + n];
    nvme->utxn[n].virt = static_cast<uint8_t*>(nvme->iob.virt) + (IDX_UTXN_POOL + n) * kPageSize;
  }

  if (rd32(CSTS) & NVME_CSTS_RDY) {
    zxlogf(DEBUG, "controller is active. resetting...");
    wr32(rd32(CC) & ~NVME_CC_EN, CC);  // disable
  }

  // ensure previous shutdown (by us or bootloader) has completed
  unsigned ms_remain = WAIT_MS;
  while (rd32(CSTS) & NVME_CSTS_RDY) {
    if (--ms_remain == 0) {
      zxlogf(ERROR, "timed out waiting for CSTS ~RDY");
      txn.Reply(ZX_ERR_INTERNAL);
      return;
    }
    zx_nanosleep(zx_deadline_after(ZX_MSEC(1)));
  }

  zxlogf(DEBUG, "controller inactive. (after %u ms)", WAIT_MS - ms_remain);

  // configure admin submission and completion queues
  wr64(nvme->iob.phys_list[IDX_ADMIN_SQ], ASQ);
  wr64(nvme->iob.phys_list[IDX_ADMIN_CQ], ACQ);
  wr32(NVME_AQA_ASQS(SQMAX(kPageSize) - 1) | NVME_AQA_ACQS(CQMAX(kPageSize) - 1), AQA);

  zxlogf(DEBUG, "enabling");
  wr32(NVME_CC_EN | NVME_CC_AMS_RR | NVME_CC_MPS(0) | NVME_CC_IOCQES(NVME_CPL_SHIFT) |
           NVME_CC_IOSQES(NVME_CMD_SHIFT),
       CC);

  ms_remain = WAIT_MS;
  while (!(rd32(CSTS) & NVME_CSTS_RDY)) {
    if (--ms_remain == 0) {
      zxlogf(ERROR, "timed out waiting for CSTS RDY");
      txn.Reply(ZX_ERR_INTERNAL);
      return;
    }
    zx_nanosleep(zx_deadline_after(ZX_MSEC(1)));
  }
  zxlogf(DEBUG, "controller ready. (after %u ms)", WAIT_MS - ms_remain);

  // registers and buffers for admin queues
  nvme->io_admin_sq_tail_db =
      static_cast<MMIO_PTR uint8_t*>(mmio_->get()) + NVME_REG_SQnTDBL(0, cap);
  nvme->io_admin_cq_head_db =
      static_cast<MMIO_PTR uint8_t*>(mmio_->get()) + NVME_REG_CQnHDBL(0, cap);

  nvme->admin_sq = reinterpret_cast<nvme_cmd_t*>(static_cast<uint8_t*>(nvme->iob.virt) +
                                                 kPageSize * IDX_ADMIN_SQ);
  nvme->admin_sq_head = 0;
  nvme->admin_sq_tail = 0;

  nvme->admin_cq = reinterpret_cast<nvme_cpl_t*>(static_cast<uint8_t*>(nvme->iob.virt) +
                                                 kPageSize * IDX_ADMIN_CQ);
  nvme->admin_cq_head = 0;
  nvme->admin_cq_toggle = 1;

  // Set up IO submission and completion queues.
  caps_ = CapabilityReg::Get().ReadFrom(mmio_.get());
  auto io_queue =
      QueuePair::Create(bti_.borrow(), 1, caps_.max_queue_entries(), caps_, *mmio_, UTXN_COUNT);
  if (io_queue.is_error()) {
    zxlogf(ERROR, "Failed to set up io queue: %s", io_queue.status_string());
    txn.Reply(io_queue.error_value());
    return;
  }
  io_queue_ = std::move(*io_queue);

  // scratch page for admin ops
  void* scratch = static_cast<uint8_t*>(nvme->iob.virt) + kPageSize * IDX_SCRATCH;

  int thrd_status = thrd_create_with_name(&irq_thread_, IrqThread, this, "nvme-irq-thread");
  if (thrd_status) {
    zxlogf(ERROR, " cannot create irq thread: %d", thrd_status);
    txn.Reply(ZX_ERR_INTERNAL);
    return;
  }
  nvme->flags |= FLAG_IRQ_THREAD_STARTED;

  thrd_status = thrd_create_with_name(&io_thread_, IoThread, this, "nvme-io-thread");
  if (thrd_status) {
    zxlogf(ERROR, " cannot create io thread: %d", thrd_status);
    txn.Reply(ZX_ERR_INTERNAL);
    return;
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
  cmd.u.raw[0] = 1;  // CNS 01

  zx_status_t status = nvme_admin_txn(nvme, &cmd, NULL);
  if (status != ZX_OK) {
    zxlogf(ERROR, "device identify op failed: %s", zx_status_get_string(status));
    txn.Reply(ZX_ERR_INTERNAL);
    return;
  }

  nvme_identify_t* ci = static_cast<nvme_identify_t*>(scratch);
  infostring("model:         ", ci->MN, sizeof(ci->MN));
  infostring("serial number: ", ci->SN, sizeof(ci->SN));
  infostring("firmware:      ", ci->FR, sizeof(ci->FR));

  if ((ci->SQES & 0xF) != NVME_CMD_SHIFT) {
    zxlogf(ERROR, "SQES minimum is not %ub", NVME_CMD_SIZE);
    txn.Reply(ZX_ERR_NOT_SUPPORTED);
    return;
  }
  if ((ci->CQES & 0xF) != NVME_CPL_SHIFT) {
    zxlogf(ERROR, "CQES minimum is not %ub", NVME_CPL_SIZE);
    txn.Reply(ZX_ERR_NOT_SUPPORTED);
    return;
  }
  zxlogf(DEBUG, "max outstanding commands: %u", ci->MAXCMD);

  uint32_t nscount = ci->NN;
  zxlogf(DEBUG, "max namespaces: %u", nscount);
  zxlogf(DEBUG, "scatter gather lists (SGL): %c %08x", (ci->SGLS & 3) ? 'Y' : 'N', ci->SGLS);

  // Maximum transfer is in units of 2^n * PAGESIZE, n == 0 means "infinite"
  nvme->max_xfer = 0xFFFFFFFF;
  if ((ci->MDTS != 0) && (ci->MDTS < (31 - PageShift(kPageSize)))) {
    nvme->max_xfer = (1 << ci->MDTS) * kPageSize;
  }

  zxlogf(DEBUG, "max data transfer: %u bytes", nvme->max_xfer);
  zxlogf(DEBUG, "sanitize caps: %u", ci->SANICAP & 3);

  zxlogf(DEBUG, "abort command limit (ACL): %u", ci->ACL + 1);
  zxlogf(DEBUG, "asynch event req limit (AERL): %u", ci->AERL + 1);
  zxlogf(DEBUG, "firmware: slots: %u reset: %c slot1ro: %c", (ci->FRMW >> 1) & 3,
         (ci->FRMW & (1 << 4)) ? 'N' : 'Y', (ci->FRMW & 1) ? 'Y' : 'N');
  zxlogf(DEBUG, "host buffer: min/preferred: %u/%u pages", ci->HMMIN, ci->HMPRE);
  zxlogf(DEBUG, "capacity: total/unalloc: %zu/%zu", ci->TNVMCAP_LO, ci->UNVMCAP_LO);

  if (ci->VWC & 1) {
    nvme->flags |= FLAG_HAS_VWC;
  }
  uint32_t awun = ci->AWUN + 1;
  uint32_t awupf = ci->AWUPF + 1;
  zxlogf(DEBUG, "volatile write cache (VWC): %s", nvme->flags & FLAG_HAS_VWC ? "Y" : "N");
  zxlogf(DEBUG, "atomic write unit (AWUN)/(AWUPF): %u/%u blks", awun, awupf);

#define FEATURE(a, b)  \
  if (ci->a & a##_##b) \
  zxlogf(DEBUG, "feature: %s", #b)
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
  cmd.cmd =
      NVME_CMD_CID(0) | NVME_CMD_PRP | NVME_CMD_NORMAL | NVME_CMD_OPC(NVME_ADMIN_OP_SET_FEATURE);
  cmd.u.raw[0] = NVME_FEATURE_NUMBER_OF_QUEUES;
  cmd.u.raw[1] = 0;

  nvme_cpl_t cpl;
  if (nvme_admin_txn(nvme, &cmd, &cpl) != ZX_OK) {
    zxlogf(ERROR, "set feature (number queues) op failed");
    txn.Reply(ZX_ERR_INTERNAL);
    return;
  }
  zxlogf(DEBUG, "cpl.cmd %08x", cpl.cmd);

  zxlogf(INFO, "Using IO submission queue size of %lu, IO completion queue size of %lu.",
         io_queue_->submission().entry_count(), io_queue_->completion().entry_count());

  // create the IO completion queue
  memset(&cmd, 0, sizeof(cmd));
  cmd.cmd =
      NVME_CMD_CID(0) | NVME_CMD_PRP | NVME_CMD_NORMAL | NVME_CMD_OPC(NVME_ADMIN_OP_CREATE_IOCQ);
  cmd.dptr.prp[0] = io_queue_->completion().GetDeviceAddress();
  cmd.u.raw[0] = ((io_queue_->completion().entry_count() - 1) << 16) | 1;  // queue size, queue id
  cmd.u.raw[1] = (0 << 16) | 2 | 1;  // irq vector, irq enable, phys contig

  if (nvme_admin_txn(nvme, &cmd, NULL) != ZX_OK) {
    zxlogf(ERROR, "completion queue creation op failed");
    txn.Reply(ZX_ERR_INTERNAL);
    return;
  }

  // create the IO submit queue
  memset(&cmd, 0, sizeof(cmd));
  cmd.cmd =
      NVME_CMD_CID(0) | NVME_CMD_PRP | NVME_CMD_NORMAL | NVME_CMD_OPC(NVME_ADMIN_OP_CREATE_IOSQ);
  cmd.dptr.prp[0] = io_queue_->submission().GetDeviceAddress();
  cmd.u.raw[0] = ((io_queue_->submission().entry_count() - 1) << 16) | 1;  // queue size, queue id
  cmd.u.raw[1] = (1 << 16) | 0 | 1;  // cqid, qprio, phys contig

  if (nvme_admin_txn(nvme, &cmd, NULL) != ZX_OK) {
    zxlogf(ERROR, "submit queue creation op failed");
    txn.Reply(ZX_ERR_INTERNAL);
    return;
  }

  // identify namespace 1
  memset(&cmd, 0, sizeof(cmd));
  cmd.cmd = NVME_CMD_CID(0) | NVME_CMD_PRP | NVME_CMD_NORMAL | NVME_CMD_OPC(NVME_ADMIN_OP_IDENTIFY);
  cmd.nsid = 1;
  cmd.dptr.prp[0] = nvme->iob.phys_list[IDX_SCRATCH];

  if (nvme_admin_txn(nvme, &cmd, NULL) != ZX_OK) {
    zxlogf(ERROR, "namespace identify op failed");
    txn.Reply(ZX_ERR_INTERNAL);
    return;
  }

  nvme_identify_ns_t* ni = static_cast<nvme_identify_ns_t*>(scratch);

  uint32_t nawun = (ni->NSFEAT & NSFEAT_LOCAL_ATOMIC_SIZES) ? (ni->NAWUN + 1U) : awun;
  uint32_t nawupf = (ni->NSFEAT & NSFEAT_LOCAL_ATOMIC_SIZES) ? (ni->NAWUPF + 1U) : awupf;
  zxlogf(DEBUG, "ns: atomic write unit (AWUN)/(AWUPF): %u/%u blks", nawun, nawupf);
  zxlogf(DEBUG, "ns: NABSN/NABO/NABSPF/NOIOB: %u/%u/%u/%u", ni->NABSN, ni->NABO, ni->NABSPF,
         ni->NOIOB);

  // table of block formats
  for (unsigned i = 0; i < 16; i++) {
    if (ni->LBAF[i]) {
      zxlogf(DEBUG, "ns: LBA FMT %02d: RP=%u LBADS=2^%ub MS=%ub", i, NVME_LBAFMT_RP(ni->LBAF[i]),
             NVME_LBAFMT_LBADS(ni->LBAF[i]), NVME_LBAFMT_MS(ni->LBAF[i]));
    }
  }

  zxlogf(DEBUG, "ns: LBA FMT #%u active", ni->FLBAS & 0xF);
  zxlogf(DEBUG, "ns: data protection: caps/set: 0x%02x/%u", ni->DPC & 0x3F, ni->DPS & 3);

  uint32_t fmt = ni->LBAF[ni->FLBAS & 0xF];

  zxlogf(DEBUG, "ns: size/cap/util: %zu/%zu/%zu blks", ni->NSSZ, ni->NCAP, ni->NUSE);

  nvme->info.block_count = ni->NSSZ;
  nvme->info.block_size = 1 << NVME_LBAFMT_LBADS(fmt);
  nvme->info.max_transfer_size = BLOCK_MAX_TRANSFER_UNBOUNDED;

  if (NVME_LBAFMT_MS(fmt)) {
    zxlogf(ERROR, "cannot handle LBA format with metadata");
    txn.Reply(ZX_ERR_NOT_SUPPORTED);
    return;
  }
  if ((nvme->info.block_size < 512) || (nvme->info.block_size > 32768)) {
    zxlogf(ERROR, "cannot handle LBA size of %u", nvme->info.block_size);
    txn.Reply(ZX_ERR_NOT_SUPPORTED);
    return;
  }

  // NVME r/w commands operate in block units, maximum of 64K:
  uint32_t max_bytes_per_cmd = nvme->info.block_size * 65536;

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
  zxlogf(DEBUG, "max transfer per r/w op: %u blocks (%u bytes)", nvme->max_xfer,
         nvme->max_xfer * nvme->info.block_size);

  cleanup.cancel();
  txn.Reply(ZX_OK);
}

zx_status_t Nvme::AddDevice(zx_device_t* dev) {
  if ((nvme_ = static_cast<nvme_device_t*>(calloc(1, sizeof(nvme_device_t)))) == NULL) {
    return ZX_ERR_NO_MEMORY;
  }
  nvme_device_t* nvme = nvme_;

  list_initialize(&nvme->pending_txns);
  list_initialize(&nvme->active_txns);

  auto cleanup = fit::defer([&] { DdkRelease(); });

  zx_status_t status = ZX_OK;
  if ((status = device_get_fragment_protocol(dev, "pci", ZX_PROTOCOL_PCI, &pci_)) != ZX_OK) {
    zxlogf(ERROR, "Failed to find PCI fragment: %s", zx_status_get_string(status));
    return ZX_ERR_NOT_SUPPORTED;
  }

  mmio_buffer_t mmio_buffer;
  status = pci_map_bar_buffer(&pci_, 0u, ZX_CACHE_POLICY_UNCACHED_DEVICE, &mmio_buffer);
  if (status != ZX_OK) {
    zxlogf(ERROR, "cannot map registers: %s", zx_status_get_string(status));
    return ZX_ERR_NOT_SUPPORTED;
  }
  mmio_ = std::make_unique<fdf::MmioBuffer>(mmio_buffer);

  status = pci_configure_interrupt_mode(&pci_, 1, NULL);
  if (status != ZX_OK) {
    zxlogf(ERROR, "could not configure irqs: %s", zx_status_get_string(status));
    return ZX_ERR_NOT_SUPPORTED;
  }

  status = pci_map_interrupt(&pci_, 0, &irqh_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "could not map irq: %s", zx_status_get_string(status));
    return ZX_ERR_NOT_SUPPORTED;
  }

  status = pci_set_bus_mastering(&pci_, true);
  if (status != ZX_OK) {
    zxlogf(ERROR, "cannot enable bus mastering: %s", zx_status_get_string(status));
    return ZX_ERR_NOT_SUPPORTED;
  }

  zx_handle_t bti_handle;
  status = pci_get_bti(&pci_, 0, &bti_handle);
  if (status != ZX_OK) {
    zxlogf(ERROR, "cannot obtain bti handle: %s", zx_status_get_string(status));
    return ZX_ERR_NOT_SUPPORTED;
  }
  bti_ = zx::bti(bti_handle);

  status = DdkAdd(ddk::DeviceAddArgs("nvme"));
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed DdkAdd: %s", zx_status_get_string(status));
    return ZX_ERR_NOT_SUPPORTED;
  }

  cleanup.cancel();
  return ZX_OK;
}

zx_status_t Nvme::Bind(void* ctx, zx_device_t* dev) {
  auto driver = std::make_unique<nvme::Nvme>(dev);
  if (zx_status_t status = driver->AddDevice(dev); status != ZX_OK) {
    return status;
  }

  // The DriverFramework now owns driver.
  __UNUSED auto placeholder = driver.release();
  return ZX_OK;
}

static zx_driver_ops_t driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = Nvme::Bind,
};

}  // namespace nvme

ZIRCON_DRIVER(nvme, nvme::driver_ops, "zircon", "0.1");
