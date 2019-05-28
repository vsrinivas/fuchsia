// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <threads.h>
#include <unistd.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/io-buffer.h>
#include <ddk/mmio-buffer.h>
#include <ddk/phys-iter.h>
#include <ddk/protocol/pci.h>
#include <ddk/protocol/pci-lib.h>

#include <fbl/alloc_checker.h>
#include <hw/pci.h>
#include <lib/sync/completion.h>
#include <zircon/assert.h>
#include <zircon/listnode.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include "ahci.h"
#include "ahci-controller.h"
#include "sata.h"

#define ahci_read(reg)       pcie_read32(reg)
#define ahci_write(reg, val) pcie_write32(reg, val)

static inline uint32_t hi32(uint64_t val) { return static_cast<uint32_t>(val >> 32); }
static inline uint32_t lo32(uint64_t val) { return static_cast<uint32_t>(val); }

#define PAGE_MASK (PAGE_SIZE - 1ull)

// port is implemented by the controller
#define AHCI_PORT_FLAG_IMPLEMENTED (1u << 0)
// a device is present on port
#define AHCI_PORT_FLAG_PRESENT     (1u << 1)
// port is paused (no queued transactions will be processed)
// until pending transactions are done
#define AHCI_PORT_FLAG_SYNC_PAUSED (1u << 2)

//clang-format on

// Calculate the physical base of a virtual address.
static zx_paddr_t vtop(zx_paddr_t phys_base, void* virt_base, void* virt_addr) {
    uintptr_t addr = reinterpret_cast<uintptr_t>(virt_addr);
    uintptr_t base = reinterpret_cast<uintptr_t>(virt_base);
    return phys_base + (addr - base);
}

static inline zx_status_t ahci_wait_for_clear(const volatile uint32_t* reg, uint32_t mask,
                                              zx_time_t timeout) {
    int i = 0;
    timeout += zx_clock_get_monotonic();
    do {
        if (!(ahci_read(reg) & mask)) return ZX_OK;
        usleep(10 * 1000);
        i++;
    } while (zx_clock_get_monotonic() < timeout);
    return ZX_ERR_TIMED_OUT;
}

static inline zx_status_t ahci_wait_for_set(const volatile uint32_t* reg, uint32_t mask,
                                            zx_time_t timeout) {
    int i = 0;
    timeout += zx_clock_get_monotonic();
    do {
        if (ahci_read(reg) & mask) return ZX_OK;
        usleep(10 * 1000);
        i++;
    } while (zx_clock_get_monotonic() < timeout);
    return ZX_ERR_TIMED_OUT;
}

bool AhciController::PortValid(uint32_t portnr) {
    if (portnr >= AHCI_MAX_PORTS) {
        return false;
    }
    ahci_port_t* port = &ports_[portnr];
    uint32_t flags = AHCI_PORT_FLAG_IMPLEMENTED | AHCI_PORT_FLAG_PRESENT;
    return (port->flags & flags) == flags;
}

static void ahci_port_disable(ahci_port_t* port) {
    uint32_t cmd = ahci_read(&port->regs->cmd);
    if (!(cmd & AHCI_PORT_CMD_ST)) return;
    cmd &= ~AHCI_PORT_CMD_ST;
    ahci_write(&port->regs->cmd, cmd);
    zx_status_t status = ahci_wait_for_clear(&port->regs->cmd, AHCI_PORT_CMD_CR, ZX_MSEC(500));
    if (status) {
        zxlogf(ERROR, "ahci.%u: port disable timed out\n", port->nr);
    }
}

static void ahci_port_enable(ahci_port_t* port) {
    uint32_t cmd = ahci_read(&port->regs->cmd);
    if (cmd & AHCI_PORT_CMD_ST) return;
    if (!(cmd & AHCI_PORT_CMD_FRE)) {
        zxlogf(ERROR, "ahci.%u: cannot enable port without FRE enabled\n", port->nr);
        return;
    }
    zx_status_t status = ahci_wait_for_clear(&port->regs->cmd, AHCI_PORT_CMD_CR, ZX_MSEC(500));
    if (status) {
        zxlogf(ERROR, "ahci.%u: dma engine still running when enabling port\n", port->nr);
    }
    cmd |= AHCI_PORT_CMD_ST;
    ahci_write(&port->regs->cmd, cmd);
}

static void ahci_port_reset(ahci_port_t* port) {
    // disable port
    ahci_port_disable(port);

    // clear error
    ahci_write(&port->regs->serr, ahci_read(&port->regs->serr));

    // wait for device idle
    zx_status_t status = ahci_wait_for_clear(&port->regs->tfd,
                                             AHCI_PORT_TFD_BUSY | AHCI_PORT_TFD_DATA_REQUEST,
                                             ZX_SEC(1));
    if (status < 0) {
        // if busy is not cleared, do a full comreset
        zxlogf(SPEW, "ahci.%u: timed out waiting for port idle, resetting\n", port->nr);
        // v1.3.1, 10.4.2 port reset
        uint32_t sctl = AHCI_PORT_SCTL_IPM_ACTIVE | AHCI_PORT_SCTL_IPM_PARTIAL | AHCI_PORT_SCTL_DET_INIT;
        ahci_write(&port->regs->sctl, sctl);
        usleep(1000);
        sctl = ahci_read(&port->regs->sctl);
        sctl &= ~AHCI_PORT_SCTL_DET_MASK;
        ahci_write(&port->regs->sctl, sctl);
    }

    // enable port
    ahci_port_enable(port);

    // wait for device detect
    status = ahci_wait_for_set(&port->regs->ssts, AHCI_PORT_SSTS_DET_PRESENT, ZX_SEC(1));
    if ((driver_get_log_flags() & DDK_LOG_SPEW) && (status < 0)) {
        zxlogf(SPEW, "ahci.%u: no device detected\n", port->nr);
    }

    // clear error
    ahci_write(&port->regs->serr, ahci_read(&port->regs->serr));
}

static bool ahci_port_cmd_busy(ahci_port_t* port, uint32_t slot) {
    // a command slot is busy if a transaction is in flight or pending to be completed
    return ((ahci_read(&port->regs->sact) | ahci_read(&port->regs->ci)) & (1u << slot)) ||
           (port->commands[slot] != NULL) ||
           (port->running & (1u << slot)) || (port->completed & (1u << slot));
}

static bool cmd_is_read(uint8_t cmd) {
    if (cmd == SATA_CMD_READ_DMA ||
        cmd == SATA_CMD_READ_DMA_EXT ||
        cmd == SATA_CMD_READ_FPDMA_QUEUED) {
        return true;
    } else {
        return false;
    }
}

static bool cmd_is_write(uint8_t cmd) {
    if (cmd == SATA_CMD_WRITE_DMA ||
        cmd == SATA_CMD_WRITE_DMA_EXT ||
        cmd == SATA_CMD_WRITE_FPDMA_QUEUED) {
        return true;
    } else {
        return false;
    }
}

static bool cmd_is_queued(uint8_t cmd) {
    return (cmd == SATA_CMD_READ_FPDMA_QUEUED) || (cmd == SATA_CMD_WRITE_FPDMA_QUEUED);
}

void AhciController::TxnComplete(ahci_port_t* port, zx_status_t status) {
    mtx_lock(&port->lock);
    uint32_t active = ahci_read(&port->regs->sact); // Transactions active in hardware.
    uint32_t running = port->running;               // Transactions tagged as running.
    // Transactions active in hardware but not tagged as running.
    uint32_t unaccounted = active & ~running;
    // Remove transactions that have been completed by the watchdog.
    unaccounted &= ~port->completed;
    // assert if a command slot without an outstanding transaction is active.
    ZX_DEBUG_ASSERT(unaccounted == 0);

    // Transactions tagged as running but completed by hardware.
    uint32_t done = running & ~active;
    port->completed |= done;
    mtx_unlock(&port->lock);
    // hit the worker thread to complete commands
    sync_completion_signal(&worker_completion_);
}

zx_status_t AhciController::TxnBegin(ahci_port_t* port, uint32_t slot, sata_txn_t* txn) {
    ZX_DEBUG_ASSERT(slot < AHCI_MAX_COMMANDS);
    ZX_DEBUG_ASSERT(!ahci_port_cmd_busy(port, slot));

    uint64_t offset_vmo = txn->bop.rw.offset_vmo * port->devinfo.block_size;
    uint64_t bytes = txn->bop.rw.length * port->devinfo.block_size;
    size_t pagecount = ((offset_vmo & (PAGE_SIZE - 1)) + bytes + (PAGE_SIZE - 1)) /
                       PAGE_SIZE;
    zx_paddr_t pages[AHCI_MAX_PAGES];
    if (pagecount > AHCI_MAX_PAGES) {
        zxlogf(SPEW, "ahci.%u: txn %p too many pages (%zu)\n", port->nr, txn, pagecount);
        return ZX_ERR_INVALID_ARGS;
    }

    zx_handle_t vmo = txn->bop.rw.vmo;
    bool is_write = cmd_is_write(txn->cmd);
    uint32_t options = is_write ? ZX_BTI_PERM_READ : ZX_BTI_PERM_WRITE;
    zx_handle_t pmt;
    zx_status_t st = zx_bti_pin(bti_handle_, options, vmo, offset_vmo & ~PAGE_MASK,
                                pagecount * PAGE_SIZE, pages, pagecount, &pmt);
    if (st != ZX_OK) {
        zxlogf(SPEW, "ahci.%u: failed to pin pages, err = %d\n", port->nr, st);
        return st;
    }
    txn->pmt = pmt;

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
    if (cap_ & AHCI_CAP_NCQ) {
        if (cmd == SATA_CMD_READ_DMA_EXT) {
            cmd = SATA_CMD_READ_FPDMA_QUEUED;
        } else if (cmd == SATA_CMD_WRITE_DMA_EXT) {
            cmd = SATA_CMD_WRITE_FPDMA_QUEUED;
        }
    }

    // build the command
    ahci_cl_t* cl = &port->mem->cl[slot];
    // don't clear the cl since we set up ctba/ctbau at init
    cl->prdtl_flags_cfl = 0;
    cl->cfl = 5; // 20 bytes
    cl->w = is_write ? 1 : 0;
    cl->prdbc = 0;
    memset(&port->mem->tab[slot].ct, 0, sizeof(ahci_ct_t));

    uint8_t* cfis = port->mem->tab[slot].ct.cfis;
    cfis[0] = 0x27; // host-to-device
    cfis[1] = 0x80; // command
    cfis[2] = cmd;
    cfis[7] = device;

    // some commands have lba/count fields
    if (cmd == SATA_CMD_READ_DMA_EXT ||
        cmd == SATA_CMD_WRITE_DMA_EXT) {
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
        cfis[12] = (slot << 3) & 0xff; // tag
        cfis[13] = 0; // normal priority
    }

    cl->prdtl = 0;
    size_t length;
    zx_paddr_t paddr;
    for (uint32_t i = 0; i < AHCI_MAX_PRDS; i++) {
        length = phys_iter_next(&iter, &paddr);
        if (length == 0) {
            break;
        } else if (length > AHCI_PRD_MAX_SIZE) {
            zxlogf(ERROR, "ahci.%u: chunk size > %zu is unsupported\n", port->nr, length);
            return ZX_ERR_NOT_SUPPORTED;;
        } else if (cl->prdtl == AHCI_MAX_PRDS) {
            zxlogf(ERROR, "ahci.%u: txn with more than %d chunks is unsupported\n",
                    port->nr, cl->prdtl);
            return ZX_ERR_NOT_SUPPORTED;
        }

        ahci_prd_t* prd = &port->mem->tab[slot].prd[i];
        prd->dba = lo32(paddr);
        prd->dbau = hi32(paddr);
        prd->dbc = ((length - 1) & (AHCI_PRD_MAX_SIZE - 1)); // 0-based byte count
        cl->prdtl++;
    }

    port->running |= (1u << slot);
    port->commands[slot] = txn;

    zxlogf(SPEW, "ahci.%u: do_txn txn %p (%c) offset 0x%" PRIx64 " length 0x%" PRIx64
                  " slot %d prdtl %u\n",
            port->nr, txn, cl->w ? 'w' : 'r', lba, count, slot, cl->prdtl);
    if (driver_get_log_flags() & DDK_LOG_SPEW) {
        for (uint32_t i = 0; i < cl->prdtl; i++) {
            ahci_prd_t* prd = &port->mem->tab[slot].prd[i];
            zxlogf(SPEW, "%04u: dbau=0x%08x dba=0x%08x dbc=0x%x\n",
                   i, prd->dbau, prd->dba, prd->dbc);
        }
    }

    // start command
    if (cmd_is_queued(cmd)) {
        ahci_write(&port->regs->sact, (1u << slot));
    }
    ahci_write(&port->regs->ci, (1u << slot));

    // set the watchdog
    // TODO: general timeout mechanism
    txn->timeout = zx_clock_get_monotonic() + ZX_SEC(1);
    sync_completion_signal(&watchdog_completion_);
    return ZX_OK;
}

zx_status_t AhciController::PortInit(ahci_port_t* port) {
    uint32_t cmd = ahci_read(&port->regs->cmd);
    if (cmd & (AHCI_PORT_CMD_ST | AHCI_PORT_CMD_FRE | AHCI_PORT_CMD_CR | AHCI_PORT_CMD_FR)) {
        zxlogf(ERROR, "ahci.%u: port busy\n", port->nr);
        return ZX_ERR_UNAVAILABLE;
    }

    // Allocate memory for the command list, FIS receive area, command table and PRDT.
    zx_status_t status = io_buffer_init(&port->buffer, bti_handle_, sizeof(ahci_port_mem_t),
                                        IO_BUFFER_RW | IO_BUFFER_CONTIG);
    if (status != ZX_OK) {
        zxlogf(ERROR, "ahci.%u: error %d allocating dma memory\n", port->nr, status);
        return status;
    }
    zx_paddr_t phys_base = io_buffer_phys(&port->buffer);
    port->mem = static_cast<ahci_port_mem_t*>(io_buffer_virt(&port->buffer));

    // clear memory area
    // order is command list (1024-byte aligned)
    //          FIS receive area (256-byte aligned)
    //          command table + PRDT (128-byte aligned)
    memset(port->mem, 0, sizeof(*port->mem));

    // command list.
    zx_paddr_t paddr = vtop(phys_base, port->mem, &port->mem->cl);
    ahci_write(&port->regs->clb, lo32(paddr));
    ahci_write(&port->regs->clbu, hi32(paddr));

    // FIS receive area.
    paddr = vtop(phys_base, port->mem, &port->mem->fis);
    ahci_write(&port->regs->fb, lo32(paddr));
    ahci_write(&port->regs->fbu, hi32(paddr));

    // command table, followed by PRDT.
    for (int i = 0; i < AHCI_MAX_COMMANDS; i++) {
        paddr = vtop(phys_base, port->mem, &port->mem->tab[i].ct);
        port->mem->cl[i].ctba = lo32(paddr);
        port->mem->cl[i].ctbau = hi32(paddr);
    }

    // clear port interrupts
    ahci_write(&port->regs->is, ahci_read(&port->regs->is));

    // clear error
    ahci_write(&port->regs->serr, ahci_read(&port->regs->serr));

    // spin up
    cmd |= AHCI_PORT_CMD_SUD;
    ahci_write(&port->regs->cmd, cmd);

    // activate link
    cmd &= ~AHCI_PORT_CMD_ICC_MASK;
    cmd |= AHCI_PORT_CMD_ICC_ACTIVE;
    ahci_write(&port->regs->cmd, cmd);

    // enable FIS receive
    cmd |= AHCI_PORT_CMD_FRE;
    ahci_write(&port->regs->cmd, cmd);

    return ZX_OK;
}

void AhciController::AhciEnable() {
    uint32_t ghc = ahci_read(&regs_->ghc);
    if (ghc & AHCI_GHC_AE) return;
    for (int i = 0; i < 5; i++) {
        ghc |= AHCI_GHC_AE;
        ahci_write(&regs_->ghc, ghc);
        ghc = ahci_read(&regs_->ghc);
        if (ghc & AHCI_GHC_AE) return;
        usleep(10 * 1000);
    }
}

void AhciController::HbaReset() {
    // AHCI 1.3: Software may perform an HBA reset prior to initializing the controller
    uint32_t ghc = ahci_read(&regs_->ghc);
    ghc |= AHCI_GHC_AE;
    ahci_write(&regs_->ghc, ghc);
    ghc |= AHCI_GHC_HR;
    ahci_write(&regs_->ghc, ghc);
    // reset should complete within 1 second
    zx_status_t status = ahci_wait_for_clear(&regs_->ghc, AHCI_GHC_HR, ZX_SEC(1));
    if (status) {
        zxlogf(ERROR, "ahci: hba reset timed out\n");
    }
}

void AhciController::SetDevInfo(uint32_t portnr, sata_devinfo_t* devinfo) {
    ZX_DEBUG_ASSERT(PortValid(portnr));
    ahci_port_t* port = &ports_[portnr];
    memcpy(&port->devinfo, devinfo, sizeof(port->devinfo));
}

void AhciController::Queue(uint32_t portnr, sata_txn_t* txn) {
    ZX_DEBUG_ASSERT(PortValid(portnr));

    ahci_port_t* port = &ports_[portnr];

    zxlogf(SPEW, "ahci.%u: queue_txn txn %p offset_dev 0x%" PRIx64 " length 0x%x\n",
            port->nr, txn, txn->bop.rw.offset_dev, txn->bop.rw.length);

    // reset the physical address
    txn->pmt = ZX_HANDLE_INVALID;

    // put the cmd on the queue
    mtx_lock(&port->lock);
    list_add_tail(&port->txn_list, &txn->node);

    // hit the worker thread
    sync_completion_signal(&worker_completion_);
    mtx_unlock(&port->lock);
}

AhciController::~AhciController() {
    zx_handle_close(irq_handle_);
    zx_handle_close(bti_handle_);

    // TODO: Join threads.
    // The current driver doesn't do this - it will be done in a following CL.

    if (regs_ != nullptr) {
        mmio_buffer_release(&mmio_);
    }
}

void AhciController::Release(void* ctx) {
    // FIXME - join threads created by this driver
    AhciController* controller = static_cast<AhciController*>(ctx);
    delete controller;
}

// worker thread

int AhciController::WorkerLoop() {
    ahci_port_t* port;
    sata_txn_t* txn;
    for (;;) {
        // iterate all the ports and run or complete commands
        for (uint32_t i = 0; i < AHCI_MAX_PORTS; i++) {
            port = &ports_[i];
            mtx_lock(&port->lock);
            if (!PortValid(i)) {
                goto next;
            }

            // complete commands first
            while (port->completed) {
                uint32_t slot = 32 - __builtin_clz(port->completed) - 1;
                txn = port->commands[slot];
                if (txn == NULL) {
                    // Transaction was completed by watchdog.
                } else {
                    mtx_unlock(&port->lock);
                    if (txn->pmt != ZX_HANDLE_INVALID) {
                        zx_pmt_unpin(txn->pmt);
                    }
                    zxlogf(SPEW, "ahci.%u: complete txn %p\n", port->nr, txn);
                    block_complete(txn, ZX_OK);
                    mtx_lock(&port->lock);
                }
                port->completed &= ~(1u << slot);
                port->running &= ~(1u << slot);
                port->commands[slot] = NULL;
                // resume the port if paused for sync and no outstanding transactions
                if ((port->flags & AHCI_PORT_FLAG_SYNC_PAUSED) && !port->running) {
                    port->flags &= ~AHCI_PORT_FLAG_SYNC_PAUSED;
                    if (port->sync) {
                        sata_txn_t* sop = port->sync;
                        port->sync = NULL;
                        mtx_unlock(&port->lock);
                        block_complete(sop, ZX_OK);
                        mtx_lock(&port->lock);
                    }
                }
            }

            if (port->flags & AHCI_PORT_FLAG_SYNC_PAUSED) {
                goto next;
            }

            // process queued txns
            for (;;) {
                txn = list_peek_head_type(&port->txn_list, sata_txn_t, node);
                if (!txn) {
                    break;
                }

                // find a free command tag
                uint32_t max = MIN(port->devinfo.max_cmd,
                                   static_cast<uint32_t>((cap_ >> 8) & 0x1f));
                uint32_t i = 0;
                for (i = 0; i <= max; i++) {
                    if (!ahci_port_cmd_busy(port, i)) break;
                }
                if (i > max) {
                    break;
                }

                list_delete(&txn->node);

                if (BLOCK_OP(txn->bop.command) == BLOCK_OP_FLUSH) {
                    if (port->running) {
                        ZX_DEBUG_ASSERT(port->sync == NULL);
                        // pause the port if FLUSH command
                        port->flags |= AHCI_PORT_FLAG_SYNC_PAUSED;
                        port->sync = txn;
                    } else {
                        // complete immediately if nothing in flight
                        mtx_unlock(&port->lock);
                        block_complete(txn, ZX_OK);
                        mtx_lock(&port->lock);
                    }
                } else {
                    // run the transaction
                    zx_status_t st = TxnBegin(port, i, txn);
                    // complete the transaction with if it failed during processing
                    if (st != ZX_OK) {
                        mtx_unlock(&port->lock);
                        block_complete(txn, st);
                        mtx_lock(&port->lock);
                        continue;
                    }
                }
            }
next:
            mtx_unlock(&port->lock);
        }
        // wait here until more commands are queued, or a port becomes idle
        sync_completion_wait(&worker_completion_, ZX_TIME_INFINITE);
        sync_completion_reset(&worker_completion_);
    }
}

int AhciController::WatchdogLoop() {
    for (;;) {
        bool idle = true;
        for (uint32_t i = 0; i < AHCI_MAX_PORTS; i++) {
            ahci_port_t* port = &ports_[i];
            if (!PortValid(i)) {
                continue;
            }

            mtx_lock(&port->lock);
            zx_time_t now = zx_clock_get_monotonic();
            uint32_t pending = port->running & ~port->completed;
            sata_txn_t* failed_txn[AHCI_MAX_COMMANDS];
            static_assert(AHCI_MAX_COMMANDS >= 32,
                          "Failed TXN insufficiently sized to handle all commmand");
            while (pending) {
                idle = false;
                unsigned slot = 32 - __builtin_clz(pending) - 1;
                failed_txn[slot] = NULL;
                sata_txn_t* txn = port->commands[slot];
                pending &= ~(1u << slot);
                if (!txn) {
                    zxlogf(ERROR, "ahci: command %u pending but txn is NULL\n", slot);
                    continue;
                }
                if (txn->timeout >= now) {
                    continue;
                }
                // Check whether this is a real timeout.
                uint32_t active = ahci_read(&port->regs->sact);
                if ((active & (1u << slot)) == 0) {
                    // Command is no longer active, it has completed but not yet serviced by
                    // IRQ thread. Get the time this event happened, compare to time
                    // watchdog loop started to determine whether it has blocked for too
                    // long.
                    zx_time_t looptime = zx_clock_get_monotonic() - now;
                    zxlogf(ERROR,
                           "ahci: spurious watchdog timeout port %u txn %p, time in watchdog = %lu\n",
                           port->nr, txn, looptime);
                } else {
                    // time out
                    zxlogf(ERROR, "ahci: txn time out on port %d txn %p\n", port->nr, txn);
                    port->running &= ~(1u << slot);
                    port->completed |= (1u << slot);
                    port->commands[slot] = NULL;
                    failed_txn[slot] = txn;
                }
            }
            mtx_unlock(&port->lock);
            for (uint32_t i = 0; i < countof(failed_txn); i++) {
                if (failed_txn[i] != NULL) {
                    block_complete(failed_txn[i], ZX_ERR_TIMED_OUT);
                }
            }
        }

        // no need to run the watchdog if there are no active xfers
        sync_completion_wait(&watchdog_completion_, idle ? ZX_TIME_INFINITE : ZX_SEC(5));
        sync_completion_reset(&watchdog_completion_);
    }
    return 0;
}

// irq handler:

void AhciController::PortIrq(uint32_t nr) {
    ahci_port_t* port = &ports_[nr];
    // clear interrupt
    uint32_t int_status = ahci_read(&port->regs->is);
    ahci_write(&port->regs->is, int_status);

    if (int_status & AHCI_PORT_INT_PRC) { // PhyRdy change
        uint32_t serr = ahci_read(&port->regs->serr);
        ahci_write(&port->regs->serr, serr & ~0x1);
    }
    if (int_status & AHCI_PORT_INT_ERROR) { // error
        zxlogf(ERROR, "ahci.%u: error is=0x%08x\n", nr, int_status);
        TxnComplete(port, ZX_ERR_INTERNAL);
    } else if (int_status) {
        TxnComplete(port, ZX_OK);
    }
}

int AhciController::IrqLoop() {
    zx_status_t status;
    for (;;) {
        status = zx_interrupt_wait(irq_handle_, NULL);
        if (status) {
            zxlogf(ERROR, "ahci: error %d waiting for interrupt\n", status);
            continue;
        }
        // mask hba interrupts while interrupts are being handled
        uint32_t ghc = ahci_read(&regs_->ghc);
        ahci_write(&regs_->ghc, ghc & ~AHCI_GHC_IE);

        // handle interrupt for each port
        uint32_t is = ahci_read(&regs_->is);
        ahci_write(&regs_->is, is);
        for (uint32_t i = 0; is && i < AHCI_MAX_PORTS; i++) {
            if (is & 0x1) {
                PortIrq(i);
            }
            is >>= 1;
        }

        // unmask hba interrupts
        ghc = ahci_read(&regs_->ghc);
        ahci_write(&regs_->ghc, ghc | AHCI_GHC_IE);
    }
}

// implement device protocol:

static zx_protocol_device_t ahci_device_proto = []() {
    zx_protocol_device_t device;
    device.version = DEVICE_OPS_VERSION;
    device.release = AhciController::Release;
    return device;
}();

int AhciController::InitScan() {
    // reset
    HbaReset();

    // enable ahci mode
    AhciEnable();

    cap_ = ahci_read(&regs_->cap);

    // count number of ports
    uint32_t port_map = ahci_read(&regs_->pi);

    // initialize ports
    zx_status_t status;
    ahci_port_t* port;
    for (uint32_t i = 0; i < AHCI_MAX_PORTS; i++) {
        port = &ports_[i];
        port->nr = i;

        if (!(port_map & (1u << i))) continue; // port not implemented

        port->flags = AHCI_PORT_FLAG_IMPLEMENTED;
        port->regs = &regs_->ports[i];
        list_initialize(&port->txn_list);

        status = PortInit(port);
        if (status) {
            return status;
        }
    }

    // clear hba interrupts
    ahci_write(&regs_->is, ahci_read(&regs_->is));

    // enable hba interrupts
    uint32_t ghc = ahci_read(&regs_->ghc);
    ghc |= AHCI_GHC_IE;
    ahci_write(&regs_->ghc, ghc);

    // this part of port init happens after enabling interrupts in ghc
    for (uint32_t i = 0; i < AHCI_MAX_PORTS; i++) {
        port = &ports_[i];
        if (!(port->flags & AHCI_PORT_FLAG_IMPLEMENTED)) continue;

        // enable port
        ahci_port_enable(port);

        // enable interrupts
        ahci_write(&port->regs->ie, AHCI_PORT_INT_MASK);

        // reset port
        ahci_port_reset(port);

        // FIXME proper layering?
        if (ahci_read(&port->regs->ssts) & AHCI_PORT_SSTS_DET_PRESENT) {
            port->flags |= AHCI_PORT_FLAG_PRESENT;
            if (ahci_read(&port->regs->sig) == AHCI_PORT_SIG_SATA) {
                sata_bind(this, zxdev_, port->nr);
            }
        }
    }

    return ZX_OK;
}

zx_status_t AhciController::Create(zx_device_t* parent, std::unique_ptr<AhciController>* con_out) {
    fbl::AllocChecker ac;
    std::unique_ptr<AhciController> controller(new (&ac) AhciController());
    if (!ac.check()) {
        zxlogf(ERROR, "ahci: out of memory\n");
        return ZX_ERR_NO_MEMORY;
    }

    zx_status_t status = device_get_protocol(parent, ZX_PROTOCOL_PCI, &controller->pci_);
    if (status != ZX_OK) {
        zxlogf(ERROR, "ahci: error getting config information\n");
        return status;
    }

    // Map register window.
    status = pci_map_bar_buffer(&controller->pci_, 5u, ZX_CACHE_POLICY_UNCACHED_DEVICE,
                                &controller->mmio_);
    if (status != ZX_OK) {
        zxlogf(ERROR, "ahci: error %d mapping register window\n", status);
        return status;
    }
    controller->regs_ = static_cast<ahci_hba_t*>(controller->mmio_.vaddr);

    zx_pcie_device_info_t config;
    status = pci_get_device_info(&controller->pci_, &config);
    if (status != ZX_OK) {
        zxlogf(ERROR, "ahci: error getting config information\n");
        return status;
    }

    // TODO: move this to SATA.
    if (config.sub_class != 0x06 && config.base_class == 0x01) { // SATA
        zxlogf(ERROR, "ahci: device class 0x%x unsupported\n", config.sub_class);
        return ZX_ERR_NOT_SUPPORTED;
    }

    // FIXME intel devices need to set SATA port enable at config + 0x92
    // ahci controller is bus master
    status = pci_enable_bus_master(&controller->pci_, true);
    if (status != ZX_OK) {
        zxlogf(ERROR, "ahci: error %d enabling bus master\n", status);
        return status;
    }

    // Query and configure IRQ modes by trying MSI first and falling back to
    // legacy if necessary.
    uint32_t irq_cnt;
    zx_pci_irq_mode_t irq_mode = ZX_PCIE_IRQ_MODE_MSI;
    status = pci_query_irq_mode(&controller->pci_, ZX_PCIE_IRQ_MODE_MSI, &irq_cnt);
    if (status == ZX_ERR_NOT_SUPPORTED) {
        status = pci_query_irq_mode(&controller->pci_, ZX_PCIE_IRQ_MODE_LEGACY, &irq_cnt);
        if (status != ZX_OK) {
            zxlogf(ERROR, "ahci: neither MSI nor legacy interrupts are supported\n");
            return status;
        }
        irq_mode = ZX_PCIE_IRQ_MODE_LEGACY;
    }

    if (irq_cnt == 0) {
        zxlogf(ERROR, "ahci: no interrupts available\n");
        return ZX_ERR_NO_RESOURCES;
    }

    zxlogf(INFO, "ahci: using %s interrupt\n", (irq_mode == ZX_PCIE_IRQ_MODE_MSI) ? "MSI" : "legacy");
    status = pci_set_irq_mode(&controller->pci_, irq_mode, 1);
    if (status != ZX_OK) {
        zxlogf(ERROR, "ahci: error %d setting irq mode\n", status);
        return status;
    }

    // Get bti handle.
    status = pci_get_bti(&controller->pci_, 0, &controller->bti_handle_);
    if (status != ZX_OK) {
        zxlogf(ERROR, "ahci: error %d getting bti handle\n", status);
        return status;
    }

    // Get irq handle.
    status = pci_map_interrupt(&controller->pci_, 0, &controller->irq_handle_);
    if (status != ZX_OK) {
        zxlogf(ERROR, "ahci: error %d getting irq handle\n", status);
        return status;
    }

    *con_out = std::move(controller);
    return ZX_OK;
}

zx_status_t AhciController::LaunchThreads() {
    int ret = thrd_create_with_name(&irq_thread_, IrqThread, this, "ahci-irq");
    if (ret != thrd_success) {
        zxlogf(ERROR, "ahci: error %d creating irq thread\n", ret);
        return ZX_ERR_NO_MEMORY;
    }
    ret = thrd_create_with_name(&worker_thread_, WorkerThread, this, "ahci-worker");
    if (ret != thrd_success) {
        zxlogf(ERROR, "ahci: error %d creating worker thread\n", ret);
        return ZX_ERR_NO_MEMORY;
    }
    ret = thrd_create_with_name(&watchdog_thread_, WatchdogThread, this, "ahci-watchdog");
    if (ret != thrd_success) {
        zxlogf(ERROR, "ahci: error %d creating watchdog thread\n", ret);
        return ZX_ERR_NO_MEMORY;
    }
    return ZX_OK;
}

void ahci_set_devinfo(AhciController* controller, uint32_t portnr, sata_devinfo_t* devinfo) {
    controller->SetDevInfo(portnr, devinfo);
}

void ahci_queue(AhciController* controller, uint32_t portnr, sata_txn_t* txn) {
    controller->Queue(portnr, txn);
}

// implement driver object:

static zx_status_t ahci_bind(void* ctx, zx_device_t* parent) {
    std::unique_ptr<AhciController> controller;
    zx_status_t status = AhciController::Create(parent, &controller);
    if (status != ZX_OK) {
        zxlogf(ERROR, "ahci: failed to create ahci controller (%d)\n", status);
        return status;
    }

    if ((status = controller->LaunchThreads()) != ZX_OK) {
        zxlogf(ERROR, "ahci: failed to start controller threads (%d)\n", status);
        return status;
    }

    // add the device for the controller
    device_add_args_t args = {};
    args.version = DEVICE_ADD_ARGS_VERSION;
    args.name = "ahci";
    args.ctx = controller.get();
    args.ops = &ahci_device_proto;
    args.flags = DEVICE_ADD_NON_BINDABLE;

    status = device_add(parent, &args, controller->zxdev_ptr());
    if (status != ZX_OK) {
        zxlogf(ERROR, "ahci: error %d in device_add\n", status);
        return status;
    }

    // initialize controller and detect devices
    thrd_t t;
    int ret = thrd_create_with_name(&t, AhciController::InitThread, controller.get(), "ahci-init");
    if (ret != thrd_success) {
        zxlogf(ERROR, "ahci: error %d in init thread create\n", status);
        // This is an error in that no devices will be found, but the AHCI controller is enabled.
        // Not returning an error, but the controller should be removed.
        // TODO: handle this better in upcoming init cleanup CL.
    }

    // Controller is retained by device_add().
    controller.release();
    return ZX_OK;
}

static constexpr zx_driver_ops_t ahci_driver_ops = []() {
    zx_driver_ops_t driver = {};
    driver.version = DRIVER_OPS_VERSION;
    driver.bind = ahci_bind;
    return driver;
}();

// clang-format off
ZIRCON_DRIVER_BEGIN(ahci, ahci_driver_ops, "zircon", "0.1", 4)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PCI),
    BI_ABORT_IF(NE, BIND_PCI_CLASS, 0x01),
    BI_ABORT_IF(NE, BIND_PCI_SUBCLASS, 0x06),
    BI_MATCH_IF(EQ, BIND_PCI_INTERFACE, 0x01),
ZIRCON_DRIVER_END(ahci)
