// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "port.h"

#include <inttypes.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/phys-iter.h>
#include <lib/zx/clock.h>
#include <unistd.h>

#include <algorithm>

#include <fbl/auto_lock.h>

#include "controller.h"

#define AHCI_PAGE_MASK (AHCI_PAGE_SIZE - 1ull)

namespace ahci {

constexpr zx::duration kTransactionTimeout(ZX_SEC(5));

constexpr uint32_t hi32(uint64_t val) { return static_cast<uint32_t>(val >> 32); }
constexpr uint32_t lo32(uint64_t val) { return static_cast<uint32_t>(val); }

// Calculate the physical base of a virtual address.
zx_paddr_t vtop(zx_paddr_t phys_base, void* virt_base, void* virt_addr) {
  uintptr_t addr = reinterpret_cast<uintptr_t>(virt_addr);
  uintptr_t base = reinterpret_cast<uintptr_t>(virt_base);
  return phys_base + (addr - base);
}

bool cmd_is_read(uint8_t cmd) {
  if (cmd == SATA_CMD_READ_DMA || cmd == SATA_CMD_READ_DMA_EXT ||
      cmd == SATA_CMD_READ_FPDMA_QUEUED) {
    return true;
  } else {
    return false;
  }
}

bool cmd_is_write(uint8_t cmd) {
  if (cmd == SATA_CMD_WRITE_DMA || cmd == SATA_CMD_WRITE_DMA_EXT ||
      cmd == SATA_CMD_WRITE_FPDMA_QUEUED) {
    return true;
  } else {
    return false;
  }
}

bool cmd_is_queued(uint8_t cmd) {
  return (cmd == SATA_CMD_READ_FPDMA_QUEUED) || (cmd == SATA_CMD_WRITE_FPDMA_QUEUED);
}

Port::Port() { list_initialize(&txn_list_); }

Port::~Port() { ZX_DEBUG_ASSERT(list_is_empty(&txn_list_)); }

uint32_t Port::RegRead(size_t offset) {
  uint32_t val = 0;
  bus_->RegRead(reg_base_ + offset, &val);
  return val;
}

void Port::RegWrite(size_t offset, uint32_t val) { bus_->RegWrite(reg_base_ + offset, val); }

bool Port::SlotBusyLocked(uint32_t slot) {
  // a command slot is busy if a transaction is in flight or pending to be completed
  return ((RegRead(kPortSataActive) | RegRead(kPortCommandIssue)) & (1u << slot)) ||
         (commands_[slot] != nullptr) || (running_ & (1u << slot)) || (completed_ & (1u << slot));
}

zx_status_t Port::Configure(uint32_t num, Bus* bus, size_t reg_base, uint32_t capabilities) {
  fbl::AutoLock lock(&lock_);
  num_ = num;
  cap_ = capabilities;
  bus_ = bus;
  reg_base_ = reg_base + (num * sizeof(ahci_port_reg_t));
  flags_ = kPortFlagImplemented;
  uint32_t cmd = RegRead(kPortCommand);
  if (cmd & (AHCI_PORT_CMD_ST | AHCI_PORT_CMD_FRE | AHCI_PORT_CMD_CR | AHCI_PORT_CMD_FR)) {
    zxlogf(ERROR, "ahci.%u: port busy", num_);
    return ZX_ERR_UNAVAILABLE;
  }

  // Allocate memory for the command list, FIS receive area, command table and PRDT.
  zx_paddr_t phys_base;
  void* virt_base;
  zx_status_t status = bus_->IoBufferInit(&buffer_, sizeof(ahci_port_mem_t),
                                          IO_BUFFER_RW | IO_BUFFER_CONTIG, &phys_base, &virt_base);
  if (status != ZX_OK) {
    zxlogf(ERROR, "ahci.%u: error %d allocating dma memory", num_, status);
    return status;
  }
  mem_ = static_cast<ahci_port_mem_t*>(virt_base);

  // clear memory area
  // order is command list (1024-byte aligned)
  //          FIS receive area (256-byte aligned)
  //          command table + PRDT (128-byte aligned)
  memset(mem_, 0, sizeof(*mem_));

  // command list.
  zx_paddr_t paddr = vtop(phys_base, mem_, &mem_->cl);
  RegWrite(kPortCommandListBase, lo32(paddr));
  RegWrite(kPortCommandListBaseUpper, hi32(paddr));

  // FIS receive area.
  paddr = vtop(phys_base, mem_, &mem_->fis);
  RegWrite(kPortFISBase, lo32(paddr));
  RegWrite(kPortFISBaseUpper, hi32(paddr));

  // command table, followed by PRDT.
  for (int i = 0; i < AHCI_MAX_COMMANDS; i++) {
    paddr = vtop(phys_base, mem_, &mem_->tab[i].ct);
    mem_->cl[i].ctba = lo32(paddr);
    mem_->cl[i].ctbau = hi32(paddr);
  }

  // clear port interrupts
  RegWrite(kPortInterruptStatus, RegRead(kPortInterruptStatus));

  // clear error
  RegWrite(kPortSataError, RegRead(kPortSataError));

  // spin up
  cmd |= AHCI_PORT_CMD_SUD;
  RegWrite(kPortCommand, cmd);

  // activate link
  cmd &= ~AHCI_PORT_CMD_ICC_MASK;
  cmd |= AHCI_PORT_CMD_ICC_ACTIVE;
  RegWrite(kPortCommand, cmd);

  // enable FIS receive
  cmd |= AHCI_PORT_CMD_FRE;
  RegWrite(kPortCommand, cmd);

  return ZX_OK;
}

zx_status_t Port::Enable() {
  uint32_t cmd = RegRead(kPortCommand);
  if (cmd & AHCI_PORT_CMD_ST)
    return ZX_OK;
  if (!(cmd & AHCI_PORT_CMD_FRE)) {
    zxlogf(ERROR, "ahci.%u: cannot enable port without FRE enabled", num_);
    return ZX_ERR_BAD_STATE;
  }
  zx_status_t status =
      bus_->WaitForClear(reg_base_ + kPortCommand, AHCI_PORT_CMD_CR, zx::msec(500));
  if (status) {
    zxlogf(ERROR, "ahci.%u: dma engine still running when enabling port", num_);
    return ZX_ERR_BAD_STATE;
  }
  cmd |= AHCI_PORT_CMD_ST;
  RegWrite(kPortCommand, cmd);
  return ZX_OK;
}

void Port::Disable() {
  uint32_t cmd = RegRead(kPortCommand);
  if (!(cmd & AHCI_PORT_CMD_ST))
    return;
  cmd &= ~AHCI_PORT_CMD_ST;
  RegWrite(kPortCommand, cmd);
  zx_status_t status =
      bus_->WaitForClear(reg_base_ + kPortCommand, AHCI_PORT_CMD_CR, zx::msec(500));
  if (status) {
    zxlogf(ERROR, "ahci.%u: port disable timed out", num_);
  }
}

void Port::Reset() {
  // disable port
  Disable();

  // clear error
  RegWrite(kPortSataError, RegRead(kPortSataError));

  // wait for device idle
  zx_status_t status = bus_->WaitForClear(
      reg_base_ + kPortTaskFileData, AHCI_PORT_TFD_BUSY | AHCI_PORT_TFD_DATA_REQUEST, zx::sec(1));
  if (status != ZX_OK) {
    // if busy is not cleared, do a full comreset
    zxlogf(TRACE, "ahci.%u: timed out waiting for port idle, resetting", num_);
    // v1.3.1, 10.4.2 port reset
    uint32_t sctl =
        AHCI_PORT_SCTL_IPM_ACTIVE | AHCI_PORT_SCTL_IPM_PARTIAL | AHCI_PORT_SCTL_DET_INIT;
    RegWrite(kPortSataControl, sctl);
    usleep(1000);
    sctl = RegRead(kPortSataControl);
    sctl &= ~AHCI_PORT_SCTL_DET_MASK;
    RegWrite(kPortSataControl, sctl);
  }

  // enable port
  Enable();

  // wait for device detect
  status = bus_->WaitForSet(reg_base_ + kPortSataStatus, AHCI_PORT_SSTS_DET_PRESENT, zx::sec(1));
  if (status < 0) {
    zxlogf(TRACE, "ahci.%u: no device detected", num_);
  }

  // clear error
  RegWrite(kPortSataError, RegRead(kPortSataError));
}

void Port::SetDevInfo(const sata_devinfo_t* devinfo) {
  fbl::AutoLock lock(&lock_);
  memcpy(&devinfo_, devinfo, sizeof(devinfo_));
}

zx_status_t Port::Queue(sata_txn_t* txn) {
  fbl::AutoLock lock(&lock_);
  if (!is_valid()) {
    return ZX_ERR_BAD_STATE;
  }

  // reset the physical address
  txn->pmt = ZX_HANDLE_INVALID;

  // put the cmd on the queue
  list_add_tail(&txn_list_, &txn->node);

  return ZX_OK;
}

bool Port::Complete() {
  fbl::AutoLock lock(&lock_);
  if (!is_valid()) {
    return false;
  }

  sata_txn_t* txn_complete[AHCI_MAX_COMMANDS];
  size_t complete_count = 0;
  bool active_txns = false;

  for (uint32_t slot = 0; slot < AHCI_MAX_COMMANDS; slot++) {
    sata_txn_t* txn = commands_[slot];
    if (txn == nullptr) {
      continue;  // No transaction in this slot.
    }
    uint32_t slot_bit = (1u << slot);
    if ((completed_ & slot_bit) == 0) {
      // Not complete, check if timeout expired.
      zx::time now = zx::clock::get_monotonic();
      if (txn->timeout > now) {
        active_txns = true;
        continue;  // Still in progress.
      }
      // Timed out.
      zx::duration delta = now - txn->timeout;
      zxlogf(ERROR, "ahci: txn time out on port %d txn %p (%ld ms)", num_, txn, delta.to_msecs());
      txn->timeout = zx::time::infinite_past();  // Signal that timeout occurred.
    }
    // Completed or timed out.
    commands_[slot] = nullptr;
    running_ &= ~slot_bit;
    completed_ &= ~slot_bit;
    txn_complete[complete_count] = txn;
    complete_count++;
  }

  sata_txn_t* sync_op = nullptr;
  // resume the port if paused for sync and no outstanding transactions
  if ((is_paused()) && !running_) {
    flags_ &= ~kPortFlagSyncPaused;
    if (sync_) {
      sync_op = sync_;
      sync_ = nullptr;
    }
  }
  lock.release();

  for (size_t i = 0; i < complete_count; i++) {
    sata_txn_t* txn = txn_complete[i];
    if (txn->pmt != ZX_HANDLE_INVALID) {
      zx_pmt_unpin(txn->pmt);
    }
    if (txn->timeout == zx::time::infinite_past()) {
      block_complete(txn, ZX_ERR_TIMED_OUT);
    } else {
      zxlogf(TRACE, "ahci.%u: complete txn %p", num_, txn);
      block_complete(txn, ZX_OK);
    }
  }

  if (sync_op != nullptr) {
    block_complete(sync_op, ZX_OK);
  }
  return active_txns;
}

bool Port::ProcessQueued() {
  lock_.Acquire();
  if ((!is_valid()) || is_paused()) {
    lock_.Release();
    return false;
  }

  bool added_txns = false;
  for (;;) {
    sata_txn_t* txn = list_peek_head_type(&txn_list_, sata_txn_t, node);
    if (!txn) {
      break;
    }

    // find a free command tag
    uint32_t max = std::min(devinfo_.max_cmd, MaxCommands());
    uint32_t i = 0;
    for (i = 0; i <= max; i++) {
      if (!SlotBusyLocked(i))
        break;
    }
    if (i > max) {
      break;
    }

    list_delete(&txn->node);

    if (BLOCK_OP(txn->bop.command) == BLOCK_OP_FLUSH) {
      if (running_) {
        ZX_DEBUG_ASSERT(sync_ == nullptr);
        // pause the port if FLUSH command
        flags_ |= kPortFlagSyncPaused;
        sync_ = txn;
        added_txns = true;
      } else {
        // complete immediately if nothing in flight
        lock_.Release();
        block_complete(txn, ZX_OK);
        lock_.Acquire();
      }
    } else {
      // run the transaction
      zx_status_t st = TxnBeginLocked(i, txn);
      // complete the transaction with if it failed during processing
      if (st != ZX_OK) {
        lock_.Release();
        block_complete(txn, st);
        lock_.Acquire();
        continue;
      }
      added_txns = true;
    }
  }
  lock_.Release();
  return added_txns;
}

void Port::TxnComplete(zx_status_t status) {
  fbl::AutoLock lock(&lock_);
  uint32_t active = RegRead(kPortSataActive);  // Transactions active in hardware.
  uint32_t running = running_;                 // Transactions tagged as running.
  // Transactions active in hardware but not tagged as running.
  uint32_t unaccounted = active & ~running;
  // Remove transactions that have been completed by the watchdog.
  unaccounted &= ~completed_;
  // assert if a command slot without an outstanding transaction is active.
  ZX_DEBUG_ASSERT(unaccounted == 0);

  // Transactions tagged as running but completed by hardware.
  uint32_t done = running & ~active;
  completed_ |= done;
}

zx_status_t Port::TxnBeginLocked(uint32_t slot, sata_txn_t* txn) {
  ZX_DEBUG_ASSERT(slot < AHCI_MAX_COMMANDS);
  ZX_DEBUG_ASSERT(!SlotBusyLocked(slot));

  uint64_t offset_vmo = txn->bop.rw.offset_vmo * devinfo_.block_size;
  uint64_t bytes = txn->bop.rw.length * devinfo_.block_size;
  size_t pagecount =
      ((offset_vmo & (AHCI_PAGE_SIZE - 1)) + bytes + (AHCI_PAGE_SIZE - 1)) / AHCI_PAGE_SIZE;
  zx_paddr_t pages[AHCI_MAX_PAGES];
  if (pagecount > AHCI_MAX_PAGES) {
    zxlogf(TRACE, "ahci.%u: txn %p too many pages (%zu)", num_, txn, pagecount);
    return ZX_ERR_INVALID_ARGS;
  }

  zx::unowned_vmo vmo(txn->bop.rw.vmo);
  bool is_write = cmd_is_write(txn->cmd);
  uint32_t options = is_write ? ZX_BTI_PERM_READ : ZX_BTI_PERM_WRITE;
  zx::pmt pmt;
  zx_status_t st = bus_->BtiPin(options, vmo, offset_vmo & ~AHCI_PAGE_MASK,
                                pagecount * AHCI_PAGE_SIZE, pages, pagecount, &pmt);
  if (st != ZX_OK) {
    zxlogf(TRACE, "ahci.%u: failed to pin pages, err = %d", num_, st);
    return st;
  }
  txn->pmt = pmt.release();

  phys_iter_buffer_t physbuf = {};
  physbuf.phys = pages;
  physbuf.phys_count = pagecount;
  physbuf.length = bytes;
  physbuf.vmo_offset = offset_vmo;

  phys_iter_t iter;
  phys_iter_init(&iter, &physbuf, AHCI_PRD_MAX_SIZE);

  uint8_t cmd = txn->cmd;
  uint8_t device = txn->device;
  uint64_t lba = txn->bop.rw.offset_dev;
  uint64_t count = txn->bop.rw.length;

  // use queued command if available
  if (HasCommandQueue()) {
    if (cmd == SATA_CMD_READ_DMA_EXT) {
      cmd = SATA_CMD_READ_FPDMA_QUEUED;
    } else if (cmd == SATA_CMD_WRITE_DMA_EXT) {
      cmd = SATA_CMD_WRITE_FPDMA_QUEUED;
    }
  }

  // build the command
  ahci_cl_t* cl = &mem_->cl[slot];
  // don't clear the cl since we set up ctba/ctbau at init
  cl->prdtl_flags_cfl = 0;
  cl->cfl = 5;  // 20 bytes
  cl->w = is_write ? 1 : 0;
  cl->prdbc = 0;
  memset(&mem_->tab[slot].ct, 0, sizeof(ahci_ct_t));

  uint8_t* cfis = mem_->tab[slot].ct.cfis;
  cfis[0] = 0x27;  // host-to-device
  cfis[1] = 0x80;  // command
  cfis[2] = cmd;
  cfis[7] = device;

  // some commands have lba/count fields
  if (cmd == SATA_CMD_READ_DMA_EXT || cmd == SATA_CMD_WRITE_DMA_EXT) {
    cfis[4] = lba & 0xff;
    cfis[5] = (lba >> 8) & 0xff;
    cfis[6] = (lba >> 16) & 0xff;
    cfis[8] = (lba >> 24) & 0xff;
    cfis[9] = (lba >> 32) & 0xff;
    cfis[10] = (lba >> 40) & 0xff;
    cfis[12] = count & 0xff;
    cfis[13] = (count >> 8) & 0xff;
  } else if (cmd_is_queued(cmd)) {
    cfis[4] = lba & 0xff;
    cfis[5] = (lba >> 8) & 0xff;
    cfis[6] = (lba >> 16) & 0xff;
    cfis[8] = (lba >> 24) & 0xff;
    cfis[9] = (lba >> 32) & 0xff;
    cfis[10] = (lba >> 40) & 0xff;
    cfis[3] = count & 0xff;
    cfis[11] = (count >> 8) & 0xff;
    cfis[12] = (slot << 3) & 0xff;  // tag
    cfis[13] = 0;                   // normal priority
  }

  cl->prdtl = 0;
  size_t length;
  zx_paddr_t paddr;
  for (uint32_t i = 0; i < AHCI_MAX_PRDS; i++) {
    length = phys_iter_next(&iter, &paddr);
    if (length == 0) {
      break;
    } else if (length > AHCI_PRD_MAX_SIZE) {
      zxlogf(ERROR, "ahci.%u: chunk size > %zu is unsupported", num_, length);
      return ZX_ERR_NOT_SUPPORTED;
    } else if (cl->prdtl == AHCI_MAX_PRDS) {
      zxlogf(ERROR, "ahci.%u: txn with more than %d chunks is unsupported", num_, cl->prdtl);
      return ZX_ERR_NOT_SUPPORTED;
    }

    ahci_prd_t* prd = &mem_->tab[slot].prd[i];
    prd->dba = lo32(paddr);
    prd->dbau = hi32(paddr);
    prd->dbc = ((length - 1) & (AHCI_PRD_MAX_SIZE - 1));  // 0-based byte count
    cl->prdtl++;
  }

  running_ |= (1u << slot);
  commands_[slot] = txn;

  zxlogf(TRACE,
         "ahci.%u: do_txn txn %p (%c) offset 0x%" PRIx64 " length 0x%" PRIx64 " slot %d prdtl %u\n",
         num_, txn, cl->w ? 'w' : 'r', lba, count, slot, cl->prdtl);
  if (zxlog_level_enabled(TRACE)) {
    for (uint32_t i = 0; i < cl->prdtl; i++) {
      ahci_prd_t* prd = &mem_->tab[slot].prd[i];
      zxlogf(TRACE, "%04u: dbau=0x%08x dba=0x%08x dbc=0x%x", i, prd->dbau, prd->dba, prd->dbc);
    }
  }

  // start command
  if (cmd_is_queued(cmd)) {
    RegWrite(kPortSataActive, (1u << slot));
  }
  RegWrite(kPortCommandIssue, (1u << slot));

  txn->timeout = zx::clock::get_monotonic() + kTransactionTimeout;
  return ZX_OK;
}

// HandleIrq does not lock the port or check whether it is valid.
// It is assumed that an invalid port can not have scheduled operations that trigger an interrupt.
// The port may have been disabled or unplugged, but is still valid.
bool Port::HandleIrq() {
  // Clear interrupt status.
  uint32_t int_status = RegRead(kPortInterruptStatus);
  RegWrite(kPortInterruptStatus, int_status);

  if (int_status & AHCI_PORT_INT_PRC) {  // PhyRdy change
    uint32_t serr = RegRead(kPortSataError);
    RegWrite(kPortSataError, serr & ~0x1);
  }
  if (int_status & AHCI_PORT_INT_ERROR) {  // error
    zxlogf(ERROR, "ahci.%u: error is=0x%08x", num_, int_status);
    TxnComplete(ZX_ERR_INTERNAL);
    return true;
  } else if (int_status) {
    TxnComplete(ZX_OK);
    return true;
  }
  return false;
}

// Set up the running state for testing Complete()
void Port::TestSetRunning(sata_txn_t* txn, uint32_t slot) {
  ZX_DEBUG_ASSERT(slot < AHCI_MAX_COMMANDS);
  commands_[slot] = txn;
  running_ |= (1u << slot);
  completed_ &= ~(1u << slot);
}

}  // namespace ahci
