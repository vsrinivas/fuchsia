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

#define COMMAND_FLAG_FAILED 1

struct IoCommand {
  block_op_t op;
  list_node_t node;
  block_impl_queue_callback completion_cb;
  void* cookie;
  uint16_t pending_txns;
  uint8_t opcode;
  uint8_t flags;
};

// Limit maximum transfer size to 1MB which fits comfortably
// within our single scatter gather page per QueuePair setup
#define MAX_XFER (QueuePair::kMaxTransferPages * zx_system_get_page_size())

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

  // The pending list consists of commands that have been received
  // via nvme_queue() and are waiting for io to start.
  // The exception is the head of the pending list which may
  // be partially started, waiting for more txns to become
  // available.
  // The active list consists of commands where all txns have
  // been created and we're waiting for them to complete or
  // error out.
  list_node_t pending_commands;  // inbound commands to process
  list_node_t active_commands;   // commands in flight

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
};

// Takes the log2 of a page size to turn it into a shift.
static inline size_t PageShift(size_t page_size) {
  return (sizeof(int) * 8) - __builtin_clz((int)page_size) - 1;
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

static inline void IoCommandComplete(IoCommand* io_cmd, zx_status_t status) {
  io_cmd->completion_cb(io_cmd->cookie, status, &io_cmd->op);
}

bool Nvme::SubmitAllTxnsForIoCommand(IoCommand* io_cmd) {
  nvme_device_t* nvme = nvme_;

  for (;;) {
    uint32_t blocks = io_cmd->op.rw.length;
    if (blocks > nvme->max_xfer) {
      blocks = nvme->max_xfer;
    }

    // Total transfer size in bytes
    size_t bytes = ((size_t)blocks) * ((size_t)nvme->info.block_size);

    NvmIoSubmission submission(io_cmd->opcode == NVME_OP_WRITE);
    submission.namespace_id = 1;
    ZX_ASSERT(blocks - 1 <= UINT16_MAX);
    submission.set_start_lba(io_cmd->op.rw.offset_dev).set_block_count(blocks - 1);

    zx::result<> status = io_queue_->Submit(submission, zx::unowned_vmo(io_cmd->op.rw.vmo),
                                            io_cmd->op.rw.offset_vmo, bytes, io_cmd);
    if (status.is_error()) {
      if (status.status_value() == ZX_ERR_SHOULD_WAIT) {
        // We can't proceed if there is no available space in the submission queue, and we tell the
        // caller to retain the command (false).
        return false;
      } else {
        zxlogf(ERROR, "Failed to submit transaction (command %p): %s", io_cmd,
               status.status_string());
        break;
      }
    }

    // keep track of where we are
    io_cmd->op.rw.offset_dev += blocks;
    io_cmd->op.rw.offset_vmo += bytes;
    io_cmd->op.rw.length -= blocks;
    io_cmd->pending_txns++;

    // If there are no more transactions remaining, we're done. We move this command to the active
    // list and tell the caller not to retain the command (true).
    if (io_cmd->op.rw.length == 0) {
      fbl::AutoLock lock(&nvme->lock);
      list_add_tail(&nvme->active_commands, &io_cmd->node);
      return true;
    }
  }

  {
    fbl::AutoLock lock(&nvme->lock);
    io_cmd->flags |= COMMAND_FLAG_FAILED;
    if (io_cmd->pending_txns) {
      // If there are earlier uncompleted transactions, we become active now and will finish
      // erroring out when they complete.
      list_add_tail(&nvme->active_commands, &io_cmd->node);
      io_cmd = NULL;
    }
  }

  if (io_cmd != NULL) {
    IoCommandComplete(io_cmd, ZX_ERR_INTERNAL);
  }

  // Either successful or not, we tell the caller not to retain the command (true).
  return true;
}

void Nvme::ProcessIoSubmissions() {
  nvme_device_t* nvme = nvme_;

  IoCommand* io_cmd;
  for (;;) {
    {
      fbl::AutoLock lock(&nvme->lock);
      io_cmd = list_remove_head_type(&nvme->pending_commands, IoCommand, node);
    }

    if (io_cmd == NULL) {
      return;
    }

    if (!SubmitAllTxnsForIoCommand(io_cmd)) {
      // put command back at front of queue for further processing later
      fbl::AutoLock lock(&nvme->lock);
      list_add_head(&nvme->pending_commands, &io_cmd->node);
      return;
    }
  }
}

void Nvme::ProcessIoCompletions() {
  nvme_device_t* nvme = nvme_;

  bool ring_doorbell = false;
  IoCommand* io_cmd = nullptr;
  bool has_error_code = true;

  while (io_queue_->CheckForNewCompletion(&io_cmd, &has_error_code) != ZX_ERR_SHOULD_WAIT) {
    ring_doorbell = true;

    if (io_cmd == nullptr) {
      zxlogf(ERROR, "Completed transaction isn't associated with a command.");
      continue;
    }

    if (has_error_code) {
      io_cmd->flags |= COMMAND_FLAG_FAILED;
      // discard any remaining bytes -- no reason to keep creating
      // further txns once one has failed
      io_cmd->op.rw.length = 0;
    }

    io_cmd->pending_txns--;
    if ((io_cmd->pending_txns == 0) && (io_cmd->op.rw.length == 0)) {
      // remove from either pending or active list
      {
        fbl::AutoLock lock(&nvme->lock);
        list_delete(&io_cmd->node);
      }
      zxlogf(TRACE, "Completed command %p %s", io_cmd,
             io_cmd->flags & COMMAND_FLAG_FAILED ? "FAILED." : "OK.");
      IoCommandComplete(io_cmd, io_cmd->flags & COMMAND_FLAG_FAILED ? ZX_ERR_IO : ZX_OK);
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
    ProcessIoCompletions();

    // process work queue
    ProcessIoSubmissions();
  }
  return 0;
}

void Nvme::BlockImplQueue(block_op_t* op, block_impl_queue_callback completion_cb, void* cookie) {
  nvme_device_t* nvme = nvme_;

  IoCommand* io_cmd = containerof(op, IoCommand, op);
  io_cmd->completion_cb = completion_cb;
  io_cmd->cookie = cookie;

  switch (io_cmd->op.command & BLOCK_OP_MASK) {
    case BLOCK_OP_READ:
      io_cmd->opcode = NVME_OP_READ;
      break;
    case BLOCK_OP_WRITE:
      io_cmd->opcode = NVME_OP_WRITE;
      break;
    case BLOCK_OP_FLUSH:
      // TODO
      IoCommandComplete(io_cmd, ZX_OK);
      return;
    default:
      IoCommandComplete(io_cmd, ZX_ERR_NOT_SUPPORTED);
      return;
  }

  if (io_cmd->op.rw.length == 0) {
    IoCommandComplete(io_cmd, ZX_ERR_INVALID_ARGS);
    return;
  }
  // Transaction must fit within device
  if ((io_cmd->op.rw.offset_dev >= nvme->info.block_count) ||
      (nvme->info.block_count - io_cmd->op.rw.offset_dev < io_cmd->op.rw.length)) {
    IoCommandComplete(io_cmd, ZX_ERR_OUT_OF_RANGE);
    return;
  }

  // convert vmo offset to a byte offset
  io_cmd->op.rw.offset_vmo *= nvme->info.block_size;

  io_cmd->pending_txns = 0;
  io_cmd->flags = 0;

  zxlogf(TRACE, "io: %s: %ublks @ blk#%zu", io_cmd->opcode == NVME_OP_WRITE ? "wr" : "rd",
         io_cmd->op.rw.length + 1U, io_cmd->op.rw.offset_dev);

  {
    fbl::AutoLock lock(&nvme->lock);
    list_add_tail(&nvme->pending_commands, &io_cmd->node);
  }

  sync_completion_signal(&nvme->io_signal);
}

void Nvme::BlockImplQuery(block_info_t* info_out, size_t* block_op_size_out) {
  nvme_device_t* nvme = nvme_;

  *info_out = nvme->info;
  *block_op_size_out = sizeof(IoCommand);
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

  // Error out any pending commands
  {
    fbl::AutoLock lock(&nvme->lock);
    IoCommand* io_cmd;
    while ((io_cmd = list_remove_head_type(&nvme->active_commands, IoCommand, node)) != NULL) {
      IoCommandComplete(io_cmd, ZX_ERR_PEER_CLOSED);
    }
    while ((io_cmd = list_remove_head_type(&nvme->pending_commands, IoCommand, node)) != NULL) {
      IoCommandComplete(io_cmd, ZX_ERR_PEER_CLOSED);
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
#define rd64(r) MmioRead64(reinterpret_cast<MMIO_PTR uint64_t*>(MmioVaddr + NVME_REG_##r))
#define wr32(v, r) MmioWrite32(v, reinterpret_cast<MMIO_PTR uint32_t*>(MmioVaddr + NVME_REG_##r))
#define wr64(v, r) MmioWrite64(v, reinterpret_cast<MMIO_PTR uint64_t*>(MmioVaddr + NVME_REG_##r))

// dedicated pages from the page pool
#define IDX_ADMIN_SQ 0
#define IDX_ADMIN_CQ 1
#define IDX_SCRATCH 2
#define IO_PAGE_COUNT 3  // this must always be last

#define WAIT_MS 5000

void Nvme::DdkInit(ddk::InitTxn txn) {
  nvme_device_t* nvme = nvme_;

  auto cleanup = fit::defer([] { zxlogf(ERROR, "init failed"); });

  caps_ = CapabilityReg::Get().ReadFrom(mmio_.get());
  version_ = VersionReg::Get().ReadFrom(mmio_.get());

  zxlogf(INFO, "Version %d.%d.%d", version_.major(), version_.minor(), version_.tertiary());
  zxlogf(DEBUG, "Memory page size: (MPSMIN) %u bytes, (MPSMAX) %u bytes",
         caps_.memory_page_size_min_bytes(), caps_.memory_page_size_max_bytes());
  zxlogf(DEBUG, "Doorbell stride (DSTRD): %u bytes", caps_.doorbell_stride_bytes());
  zxlogf(DEBUG, "Timeout (TO): %u ms", caps_.timeout_ms());
  zxlogf(DEBUG, "Boot partition support (BPS): %c", caps_.boot_partition_support() ? 'Y' : 'N');
  zxlogf(DEBUG, "Supports NVM command set (CSS:NVM): %c",
         caps_.nvm_command_set_support() ? 'Y' : 'N');
  zxlogf(DEBUG, "NVM subsystem reset supported (NSSRS): %c",
         caps_.nvm_subsystem_reset_supported() ? 'Y' : 'N');
  zxlogf(DEBUG, "Weighted round robin supported (AMS:WRR): %c",
         caps_.weighted_round_robin_arbitration_supported() ? 'Y' : 'N');
  zxlogf(DEBUG, "Vendor specific arbitration supported (AMS:VS): %c",
         caps_.vendor_specific_arbitration_supported() ? 'Y' : 'N');
  zxlogf(DEBUG, "Contiguous queues required (CQR): %c",
         caps_.contiguous_queues_required() ? 'Y' : 'N');
  zxlogf(DEBUG, "Maximum queue entries supported (MQES): %u", caps_.max_queue_entries());

  const size_t kPageSize = zx_system_get_page_size();
  if (kPageSize < caps_.memory_page_size_min_bytes()) {
    zxlogf(ERROR, "Page size is too small (ours: 0x%zd, min: 0x%d).", kPageSize,
           caps_.memory_page_size_min_bytes());
    txn.Reply(ZX_ERR_NOT_SUPPORTED);
    return;
  }
  if (kPageSize > caps_.memory_page_size_max_bytes()) {
    zxlogf(ERROR, "Page size is too large (ours: 0x%zd, max: 0x%d).", kPageSize,
           caps_.memory_page_size_max_bytes());
    txn.Reply(ZX_ERR_NOT_SUPPORTED);
    return;
  }

  // Allocate pages for the admin queue and scratch.
  // TODO: these should all be RO to hardware apart from the scratch io page(s)
  if (io_buffer_init(&nvme->iob, bti_.get(), kPageSize * IO_PAGE_COUNT, IO_BUFFER_RW) ||
      io_buffer_physmap(&nvme->iob)) {
    zxlogf(ERROR, "could not allocate io buffers");
    txn.Reply(ZX_ERR_NO_MEMORY);
    return;
  }

  if (ControllerStatusReg::Get().ReadFrom(&*mmio_).ready()) {
    zxlogf(DEBUG, "Controller is already enabled. Resetting it.");
    ControllerConfigReg::Get().ReadFrom(&*mmio_).set_enabled(0).WriteTo(&*mmio_);

    unsigned ms_remain = WAIT_MS;
    while (ControllerStatusReg::Get().ReadFrom(&*mmio_).ready()) {
      if (--ms_remain == 0) {
        zxlogf(ERROR, "Controller reset timed out.");
        txn.Reply(ZX_ERR_TIMED_OUT);
        return;
      }
      zx_nanosleep(zx_deadline_after(ZX_MSEC(1)));
    }
    zxlogf(DEBUG, "Controller has been reset (took %u ms).", WAIT_MS - ms_remain);
  }

  // configure admin submission and completion queues
  wr64(nvme->iob.phys_list[IDX_ADMIN_SQ], ASQ);
  wr64(nvme->iob.phys_list[IDX_ADMIN_CQ], ACQ);
  wr32(NVME_AQA_ASQS(SQMAX(kPageSize) - 1) | NVME_AQA_ACQS(CQMAX(kPageSize) - 1), AQA);

  zxlogf(DEBUG, "Enabling controller.");
  ControllerConfigReg::Get()
      .ReadFrom(&*mmio_)
      .set_controller_ready_independent_of_media(0)
      // Queue entry sizes are powers of two.
      .set_io_completion_queue_entry_size(__builtin_ctzl(sizeof(Completion)))
      .set_io_submission_queue_entry_size(__builtin_ctzl(sizeof(Submission)))
      .set_arbitration_mechanism(ControllerConfigReg::ArbitrationMechanism::kRoundRobin)
      // We know that page size is always at least 4096 (required by spec), and we check
      // that zx_system_get_page_size is supported by the controller above.
      .set_memory_page_size(__builtin_ctzl(kPageSize) - 12)
      .set_io_command_set(ControllerConfigReg::CommandSet::kNvm)
      .set_enabled(1)
      .WriteTo(&*mmio_);

  // Timeout may have changed, so double check it.
  caps_.ReadFrom(&*mmio_);

  unsigned ms_remain = WAIT_MS;
  while (!(ControllerStatusReg::Get().ReadFrom(&*mmio_).ready())) {
    if (--ms_remain == 0) {
      zxlogf(ERROR, "Timed out waiting for controller to leave reset.");
      txn.Reply(ZX_ERR_TIMED_OUT);
      return;
    }
    zx_nanosleep(zx_deadline_after(ZX_MSEC(1)));
  }
  zxlogf(DEBUG, "Controller enabled (took %u ms).", WAIT_MS - ms_remain);

  // registers and buffers for admin queues
  uint64_t cap = rd64(CAP);
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
  auto io_queue = QueuePair::Create(bti_.borrow(), 1, caps_.max_queue_entries(), caps_, *mmio_,
                                    /*prealloc_prp=*/true);
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

  list_initialize(&nvme->pending_commands);
  list_initialize(&nvme->active_commands);

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
