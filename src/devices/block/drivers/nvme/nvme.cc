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
#include <lib/fzl/vmo-mapper.h>
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
#include "src/devices/block/drivers/nvme/commands/features.h"
#include "src/devices/block/drivers/nvme/commands/identify.h"
#include "src/devices/block/drivers/nvme/commands/nvme-io.h"
#include "src/devices/block/drivers/nvme/commands/queue.h"
#include "src/devices/block/drivers/nvme/nvme_bind.h"
#include "src/devices/block/drivers/nvme/registers.h"

namespace nvme {

// c.f. NVMe Base Specification 2.0, section 3.1.3.8 "AQA - Admin Queue Attributes"
constexpr size_t kAdminQueueMaxEntries = 4096;

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

  uint32_t max_transfer_blocks;
  block_info_t info;

  // context for admin transactions
  // presently we serialize these under the admin_lock
  fbl::Mutex admin_lock;
  sync_completion_t admin_signal;
  Completion admin_result;
};

int Nvme::IrqLoop() {
  nvme_device_t* nvme = nvme_;

  for (;;) {
    zx_status_t status = zx_interrupt_wait(irqh_, NULL);
    if (status != ZX_OK) {
      zxlogf(ERROR, "irq wait failed: %s", zx_status_get_string(status));
      break;
    }

    Completion* admin_completion;
    if (admin_queue_->CheckForNewCompletion(&admin_completion) != ZX_ERR_SHOULD_WAIT) {
      nvme->admin_result = *admin_completion;
      sync_completion_signal(&nvme->admin_signal);
      admin_queue_->RingCompletionDb();
    }

    sync_completion_signal(&nvme->io_signal);
  }
  return 0;
}

zx_status_t Nvme::DoAdminCommandSync(Submission& submission,
                                     std::optional<zx::unowned_vmo> admin_data) {
  nvme_device_t* nvme = nvme_;

  zx_status_t status;
  fbl::AutoLock lock(&nvme->admin_lock);
  sync_completion_reset(&nvme->admin_signal);

  uint64_t data_size = 0;
  if (admin_data.has_value()) {
    status = admin_data.value()->get_size(&data_size);
    if (status != ZX_OK) {
      zxlogf(ERROR, "Failed to get size of vmo: %s", zx_status_get_string(status));
      return status;
    }
  }
  status = admin_queue_->Submit(submission, admin_data, 0, data_size, nullptr);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to submit admin command: %s", zx_status_get_string(status));
    return status;
  }

  status = sync_completion_wait(&nvme->admin_signal, ZX_SEC(1));
  if (status != ZX_OK) {
    zxlogf(ERROR, "Timed out waiting for admin command: %s", zx_status_get_string(status));
    return status;
  }

  if (nvme->admin_result.status_code_type() == StatusCodeType::kGeneric &&
      nvme->admin_result.status_code() == 0) {
    zxlogf(TRACE, "Completed admin command OK.");
  } else {
    zxlogf(ERROR, "Completed admin command ERROR: status type=%01x, status=%02x",
           nvme->admin_result.status_code_type(), nvme->admin_result.status_code());
    return ZX_ERR_IO;
  }
  return ZX_OK;
}

static inline void IoCommandComplete(IoCommand* io_cmd, zx_status_t status) {
  io_cmd->completion_cb(io_cmd->cookie, status, &io_cmd->op);
}

bool Nvme::SubmitAllTxnsForIoCommand(IoCommand* io_cmd) {
  nvme_device_t* nvme = nvme_;

  for (;;) {
    uint32_t blocks = io_cmd->op.rw.length;
    if (blocks > nvme->max_transfer_blocks) {
      blocks = nvme->max_transfer_blocks;
    }

    // Total transfer size in bytes
    size_t bytes = ((size_t)blocks) * ((size_t)nvme->info.block_size);

    NvmIoSubmission submission(io_cmd->opcode == NVME_OP_WRITE);
    submission.namespace_id = 1;
    ZX_ASSERT(blocks - 1 <= UINT16_MAX);
    submission.set_start_lba(io_cmd->op.rw.offset_dev).set_block_count(blocks - 1);

    zx_status_t status = io_queue_->Submit(submission, zx::unowned_vmo(io_cmd->op.rw.vmo),
                                           io_cmd->op.rw.offset_vmo, bytes, io_cmd);
    if (status != ZX_OK) {
      if (status == ZX_ERR_SHOULD_WAIT) {
        // We can't proceed if there is no available space in the submission queue, and we tell the
        // caller to retain the command (false).
        return false;
      } else {
        zxlogf(ERROR, "Failed to submit transaction (command %p): %s", io_cmd,
               zx_status_get_string(status));
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
  Completion* completion = nullptr;
  IoCommand* io_cmd = nullptr;
  while (io_queue_->CheckForNewCompletion(&completion, &io_cmd) != ZX_ERR_SHOULD_WAIT) {
    ring_doorbell = true;

    if (io_cmd == nullptr) {
      zxlogf(ERROR, "Completed transaction isn't associated with a command.");
      continue;
    }

    if (completion->status_code_type() == StatusCodeType::kGeneric &&
        completion->status_code() == 0) {
      zxlogf(TRACE, "Completed transaction #%u command %p OK.", completion->command_id(), io_cmd);
    } else {
      zxlogf(ERROR, "Completed transaction #%u command %p ERROR: status type=%01x, status=%02x",
             completion->command_id(), io_cmd, completion->status_code_type(),
             completion->status_code());
      io_cmd->flags |= COMMAND_FLAG_FAILED;
      // Discard any remaining bytes -- no reason to keep creating further txns once one has failed.
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

  free(nvme);
}

#define WAIT_MS 5000

static zx_status_t CheckMinMaxSize(const std::string& name, size_t our_size, size_t min_size,
                                   size_t max_size) {
  if (our_size < min_size) {
    zxlogf(ERROR, "%s size is too small (ours: %zu, min: %zu).", name.c_str(), our_size, min_size);
    return ZX_ERR_NOT_SUPPORTED;
  }
  if (our_size > max_size) {
    zxlogf(ERROR, "%s size is too large (ours: %zu, max: %zu).", name.c_str(), our_size, max_size);
    return ZX_ERR_NOT_SUPPORTED;
  }
  return ZX_OK;
}

void Nvme::DdkInit(ddk::InitTxn txn) {
  // The drive initialization has numerous error conditions. Wrap the initialization here to ensure
  // we always call txn.Reply() in any outcome.
  zx_status_t status = Init();
  if (status != ZX_OK) {
    zxlogf(ERROR, "Driver initialization failed: %s", zx_status_get_string(status));
  }
  txn.Reply(status);
}

zx_status_t Nvme::Init() {
  nvme_device_t* nvme = nvme_;

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
  zx_status_t status = CheckMinMaxSize("System page", kPageSize, caps_.memory_page_size_min_bytes(),
                                       caps_.memory_page_size_max_bytes());
  if (status != ZX_OK) {
    return status;
  }

  if (ControllerStatusReg::Get().ReadFrom(&*mmio_).ready()) {
    zxlogf(DEBUG, "Controller is already enabled. Resetting it.");
    ControllerConfigReg::Get().ReadFrom(&*mmio_).set_enabled(0).WriteTo(&*mmio_);

    unsigned ms_remain = WAIT_MS;
    while (ControllerStatusReg::Get().ReadFrom(&*mmio_).ready()) {
      if (--ms_remain == 0) {
        zxlogf(ERROR, "Controller reset timed out.");
        return ZX_ERR_TIMED_OUT;
      }
      zx_nanosleep(zx_deadline_after(ZX_MSEC(1)));
    }
    zxlogf(DEBUG, "Controller has been reset (took %u ms).", WAIT_MS - ms_remain);
  }

  // Set up admin submission and completion queues.
  auto admin_queue = QueuePair::Create(bti_.borrow(), 0, kAdminQueueMaxEntries, caps_, *mmio_,
                                       /*prealloc_prp=*/false);
  if (admin_queue.is_error()) {
    zxlogf(ERROR, "Failed to set up admin queue: %s", admin_queue.status_string());
    return admin_queue.status_value();
  }
  admin_queue_ = std::move(*admin_queue);

  // Configure the admin queue.
  AdminQueueAttributesReg::Get()
      .ReadFrom(&*mmio_)
      .set_completion_queue_size(admin_queue_->completion().entry_count() - 1)
      .set_submission_queue_size(admin_queue_->submission().entry_count() - 1)
      .WriteTo(&*mmio_);

  AdminQueueAddressReg::CompletionQueue()
      .ReadFrom(&*mmio_)
      .set_addr(admin_queue_->completion().GetDeviceAddress())
      .WriteTo(&*mmio_);
  AdminQueueAddressReg::SubmissionQueue()
      .ReadFrom(&*mmio_)
      .set_addr(admin_queue_->submission().GetDeviceAddress())
      .WriteTo(&*mmio_);

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
      return ZX_ERR_TIMED_OUT;
    }
    zx_nanosleep(zx_deadline_after(ZX_MSEC(1)));
  }
  zxlogf(DEBUG, "Controller enabled (took %u ms).", WAIT_MS - ms_remain);

  // Set up IO submission and completion queues.
  auto io_queue = QueuePair::Create(bti_.borrow(), 1, caps_.max_queue_entries(), caps_, *mmio_,
                                    /*prealloc_prp=*/true);
  if (io_queue.is_error()) {
    zxlogf(ERROR, "Failed to set up io queue: %s", io_queue.status_string());
    return io_queue.status_value();
  }
  io_queue_ = std::move(*io_queue);
  zxlogf(DEBUG, "Using IO submission queue size of %lu, IO completion queue size of %lu.",
         io_queue_->submission().entry_count(), io_queue_->completion().entry_count());

  int thrd_status = thrd_create_with_name(&irq_thread_, IrqThread, this, "nvme-irq-thread");
  if (thrd_status) {
    zxlogf(ERROR, " cannot create irq thread: %d", thrd_status);
    return ZX_ERR_INTERNAL;
  }
  nvme->flags |= FLAG_IRQ_THREAD_STARTED;

  thrd_status = thrd_create_with_name(&io_thread_, IoThread, this, "nvme-io-thread");
  if (thrd_status) {
    zxlogf(ERROR, " cannot create io thread: %d", thrd_status);
    return ZX_ERR_INTERNAL;
  }
  nvme->flags |= FLAG_IO_THREAD_STARTED;

  zx::vmo admin_data;
  status = zx::vmo::create(kPageSize, 0, &admin_data);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to create vmo: %s", zx_status_get_string(status));
    return status;
  }

  fzl::VmoMapper mapper;
  status = mapper.Map(admin_data);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to map vmo: %s", zx_status_get_string(status));
    return status;
  }

  IdentifySubmission identify_controller;
  identify_controller.set_structure(IdentifySubmission::IdentifyCns::kIdentifyController);
  status = DoAdminCommandSync(identify_controller, admin_data.borrow());
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to identify controller: %s", zx_status_get_string(status));
    return status;
  }

  auto identify = static_cast<IdentifyController*>(mapper.start());
  zxlogf(INFO, "Model number:  '%s'",
         std::string(identify->model_number, sizeof(identify->model_number)).c_str());
  zxlogf(INFO, "Serial number: '%s'",
         std::string(identify->serial_number, sizeof(identify->serial_number)).c_str());
  zxlogf(INFO, "Firmware rev.: '%s'",
         std::string(identify->firmware_rev, sizeof(identify->firmware_rev)).c_str());

  status = CheckMinMaxSize("Submission queue entry", sizeof(Submission),
                           identify->minimum_sq_entry_size(), identify->maximum_sq_entry_size());
  if (status != ZX_OK) {
    return status;
  }
  status = CheckMinMaxSize("Completion queue entry", sizeof(Completion),
                           identify->minimum_cq_entry_size(), identify->maximum_cq_entry_size());
  if (status != ZX_OK) {
    return status;
  }

  zxlogf(DEBUG, "Maximum outstanding commands: %u", identify->max_cmd);
  zxlogf(DEBUG, "Number of namespaces: %u", identify->num_namespaces);
  if (identify->max_allowed_namespaces != 0) {
    zxlogf(DEBUG, "Maximum number of allowed namespaces: %u", identify->max_allowed_namespaces);
  }
  zxlogf(DEBUG, "SGL support: %c (0x%08x)", (identify->sgl_support & 3) ? 'Y' : 'N',
         identify->sgl_support);
  uint32_t max_data_transfer_bytes = 0;
  if (identify->max_data_transfer != 0) {
    max_data_transfer_bytes =
        caps_.memory_page_size_min_bytes() * (1 << identify->max_data_transfer);
    zxlogf(DEBUG, "Maximum data transfer size: %u bytes", max_data_transfer_bytes);
  }

  zxlogf(DEBUG, "sanitize caps: %u", identify->sanicap & 3);
  zxlogf(DEBUG, "abort command limit (ACL): %u", identify->acl + 1);
  zxlogf(DEBUG, "asynch event req limit (AERL): %u", identify->aerl + 1);
  zxlogf(DEBUG, "firmware: slots: %u reset: %c slot1ro: %c", (identify->frmw >> 1) & 3,
         (identify->frmw & (1 << 4)) ? 'N' : 'Y', (identify->frmw & 1) ? 'Y' : 'N');
  zxlogf(DEBUG, "host buffer: min/preferred: %u/%u pages", identify->hmmin, identify->hmpre);
  zxlogf(DEBUG, "capacity: total/unalloc: %zu/%zu", identify->tnvmcap[0], identify->unvmcap[0]);

  if (identify->vwc & 1) {
    nvme->flags |= FLAG_HAS_VWC;
  }
  uint32_t awun = identify->atomic_write_unit_normal + 1;
  uint32_t awupf = identify->atomic_write_unit_power_fail + 1;
  zxlogf(DEBUG, "volatile write cache (VWC): %s", nvme->flags & FLAG_HAS_VWC ? "Y" : "N");
  zxlogf(DEBUG, "atomic write unit (AWUN)/(AWUPF): %u/%u blks", awun, awupf);

#define FEATURE(a, b)  \
  if (identify->a & b) \
  zxlogf(DEBUG, "feature: %s", #b)
  FEATURE(oacs, OACS_DOORBELL_BUFFER_CONFIG);
  FEATURE(oacs, OACS_VIRTUALIZATION_MANAGEMENT);
  FEATURE(oacs, OACS_NVME_MI_SEND_RECV);
  FEATURE(oacs, OACS_DIRECTIVE_SEND_RECV);
  FEATURE(oacs, OACS_DEVICE_SELF_TEST);
  FEATURE(oacs, OACS_NAMESPACE_MANAGEMENT);
  FEATURE(oacs, OACS_FIRMWARE_DOWNLOAD_COMMIT);
  FEATURE(oacs, OACS_FORMAT_NVM);
  FEATURE(oacs, OACS_SECURITY_SEND_RECV);
  FEATURE(oncs, ONCS_TIMESTAMP);
  FEATURE(oncs, ONCS_RESERVATIONS);
  FEATURE(oncs, ONCS_SAVE_SELECT_NONZERO);
  FEATURE(oncs, ONCS_WRITE_UNCORRECTABLE);
  FEATURE(oncs, ONCS_COMPARE);

  // Set feature (number of queues) to 1 IO submission queue and 1 IO completion queue.
  SetIoQueueCountSubmission set_queue_count;
  set_queue_count.set_num_submission_queues(1).set_num_completion_queues(1);
  status = DoAdminCommandSync(set_queue_count);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to set feature (number of queues): %s", zx_status_get_string(status));
    return status;
  }
  auto result = static_cast<SetIoQueueCountCompletion*>(&nvme->admin_result);
  if (result->num_submission_queues() < 1) {
    zxlogf(ERROR, "Unexpected IO submission queue count: %u", result->num_submission_queues());
    return ZX_ERR_IO;
  }
  if (result->num_completion_queues() < 1) {
    zxlogf(ERROR, "Unexpected IO completion queue count: %u", result->num_completion_queues());
    return ZX_ERR_IO;
  }

  // Create IO completion queue.
  CreateIoCompletionQueueSubmission create_iocq;
  create_iocq.set_queue_id(io_queue_->completion().id())
      .set_queue_size(io_queue_->completion().entry_count() - 1)
      .set_contiguous(true)
      .set_interrupt_en(true)
      .set_interrupt_vector(0);
  create_iocq.data_pointer[0] = io_queue_->completion().GetDeviceAddress();
  status = DoAdminCommandSync(create_iocq);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to create IO completion queue: %s", zx_status_get_string(status));
    return status;
  }

  // Create IO submission queue.
  CreateIoSubmissionQueueSubmission create_iosq;
  create_iosq.set_queue_id(io_queue_->submission().id())
      .set_queue_size(io_queue_->submission().entry_count() - 1)
      .set_completion_queue_id(io_queue_->completion().id())
      .set_contiguous(true);
  create_iosq.data_pointer[0] = io_queue_->submission().GetDeviceAddress();
  status = DoAdminCommandSync(create_iosq);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to create IO submission queue: %s", zx_status_get_string(status));
    return status;
  }

  // Identify namespace 1.
  IdentifySubmission identify_ns;
  identify_ns.namespace_id = 1;
  identify_ns.set_structure(IdentifySubmission::IdentifyCns::kIdentifyNamespace);
  status = DoAdminCommandSync(identify_ns, admin_data.borrow());
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to identify namespace 1: %s", zx_status_get_string(status));
    return status;
  }

  auto ns = static_cast<IdentifyNvmeNamespace*>(mapper.start());

  uint32_t nawun = (ns->ns_feat & NSFEAT_LOCAL_ATOMIC_SIZES) ? (ns->n_aw_un + 1U) : awun;
  uint32_t nawupf = (ns->ns_feat & NSFEAT_LOCAL_ATOMIC_SIZES) ? (ns->n_aw_u_pf + 1U) : awupf;
  zxlogf(DEBUG, "ns: atomic write unit (AWUN)/(AWUPF): %u/%u blks", nawun, nawupf);
  zxlogf(DEBUG, "ns: NABSN/NABO/NABSPF/NOIOB: %u/%u/%u/%u", ns->n_abs_n, ns->n_ab_o, ns->n_abs_pf,
         ns->n_oio_b);

  // table of block formats
  for (unsigned i = 0; i < 16; i++) {
    if (ns->lba_formats[i].value) {
      zxlogf(DEBUG, "ns: LBA FMT %02d: RP=%u LBADS=2^%ub MS=%ub", i,
             ns->lba_formats[i].relative_performance(), ns->lba_formats[i].lba_data_size_log2(),
             ns->lba_formats[i].metadata_size_bytes());
    }
  }

  zxlogf(DEBUG, "ns: LBA FMT #%u active", ns->f_lba_s & 0xF);
  zxlogf(DEBUG, "ns: data protection: caps/set: 0x%02x/%u", ns->dpc & 0x3F, ns->dps & 3);

  auto fmt = ns->lba_formats[ns->f_lba_s & 0xF];

  zxlogf(DEBUG, "ns: size/cap/util: %zu/%zu/%zu blks", ns->n_sze, ns->n_cap, ns->n_use);

  nvme->info.block_count = ns->n_sze;
  nvme->info.block_size = 1 << fmt.lba_data_size_log2();
  // TODO(fxbug.dev/102133): Explore the option of bounding this and relying on the block driver to
  // break up large IOs.
  nvme->info.max_transfer_size = BLOCK_MAX_TRANSFER_UNBOUNDED;

  if (fmt.metadata_size_bytes()) {
    zxlogf(ERROR, "cannot handle LBA format with metadata");
    return ZX_ERR_NOT_SUPPORTED;
  }
  // The NVMe spec only mentions a lower bound. The upper bound may be a false requirement.
  if ((nvme->info.block_size < 512) || (nvme->info.block_size > 32768)) {
    zxlogf(ERROR, "cannot handle LBA size of %u", nvme->info.block_size);
    return ZX_ERR_NOT_SUPPORTED;
  }

  // NVME r/w commands operate in block units, maximum of 64K:
  const uint32_t max_bytes_per_cmd = nvme->info.block_size * 65536;
  if (max_data_transfer_bytes == 0) {
    max_data_transfer_bytes = max_bytes_per_cmd;
  } else {
    max_data_transfer_bytes = std::min(max_data_transfer_bytes, max_bytes_per_cmd);
  }

  // Limit maximum transfer size to 1MB which fits comfortably within our single PRP page per
  // QueuePair setup.
  const uint32_t prp_restricted_transfer_bytes = QueuePair::kMaxTransferPages * kPageSize;
  if (max_data_transfer_bytes > prp_restricted_transfer_bytes) {
    max_data_transfer_bytes = prp_restricted_transfer_bytes;
  }

  // convert to block units
  nvme->max_transfer_blocks = max_data_transfer_bytes / nvme->info.block_size;
  zxlogf(DEBUG, "max transfer per r/w op: %u blocks (%u bytes)", nvme->max_transfer_blocks,
         nvme->max_transfer_blocks * nvme->info.block_size);

  return ZX_OK;
}

zx_status_t Nvme::AddDevice(zx_device_t* dev) {
  if ((nvme_ = static_cast<nvme_device_t*>(calloc(1, sizeof(nvme_device_t)))) == NULL) {
    return ZX_ERR_NO_MEMORY;
  }
  nvme_device_t* nvme = nvme_;

  list_initialize(&nvme->pending_commands);
  list_initialize(&nvme->active_commands);

  auto cleanup = fit::defer([&] { DdkRelease(); });

  zx_status_t status = device_get_fragment_protocol(dev, "pci", ZX_PROTOCOL_PCI, &pci_);
  if (status != ZX_OK) {
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
