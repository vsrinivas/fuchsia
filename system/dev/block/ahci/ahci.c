// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/io-buffer.h>
#include <ddk/phys-iter.h>
#include <ddk/protocol/pci.h>

#include <assert.h>
#include <zircon/listnode.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>
#include <zircon/assert.h>
#include <pretty/hexdump.h>
#include <sync/completion.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <threads.h>
#include <unistd.h>

#include "ahci.h"
#include "sata.h"

#define ahci_read(reg)       pcie_read32(reg)
#define ahci_write(reg, val) pcie_write32(reg, val)

#define HI32(val) (((val) >> 32) & 0xffffffff)
#define LO32(val) ((val) & 0xffffffff)

#define PAGE_MASK (PAGE_SIZE - 1ull)

// port is implemented by the controller
#define AHCI_PORT_FLAG_IMPLEMENTED (1 << 0)
// a device is present on port
#define AHCI_PORT_FLAG_PRESENT     (1 << 1)
// port is paused (no queued transactions will be processed)
// until pending transactions are done
#define AHCI_PORT_FLAG_SYNC_PAUSED (1 << 2)

//clang-format on

typedef struct ahci_port {
    int nr; // 0-based
    int flags;

    sata_devinfo_t devinfo;

    ahci_port_reg_t* regs;
    ahci_cl_t* cl;
    ahci_fis_t* fis;
    ahci_ct_t* ct[AHCI_MAX_COMMANDS];

    mtx_t lock;

    list_node_t txn_list;
    io_buffer_t buffer;

    uint32_t running;   // bitmask of running commands
    uint32_t completed; // bitmask of completed commands
    sata_txn_t* commands[AHCI_MAX_COMMANDS]; // commands in flight
    sata_txn_t* sync;   // FLUSH command in flight
} ahci_port_t;

struct ahci_device {
    zx_device_t* zxdev;

    ahci_hba_t* regs;
    uint64_t regs_size;
    zx_handle_t regs_handle;

    pci_protocol_t pci;

    zx_handle_t irq_handle;
    thrd_t irq_thread;

    zx_handle_t bti_handle;

    thrd_t worker_thread;
    completion_t worker_completion;

    thrd_t watchdog_thread;
    completion_t watchdog_completion;

    uint32_t cap;

    // TODO(ZX-1641): lazily allocate these
    ahci_port_t ports[AHCI_MAX_PORTS];
};

static inline zx_status_t ahci_wait_for_clear(const volatile uint32_t* reg, uint32_t mask,
                                              zx_time_t timeout) {
    int i = 0;
    zx_time_t start_time = zx_clock_get_monotonic();
    do {
        if (!(ahci_read(reg) & mask)) return ZX_OK;
        usleep(10 * 1000);
        i++;
    } while (zx_clock_get_monotonic() - start_time < timeout);
    return ZX_ERR_TIMED_OUT;
}

static inline zx_status_t ahci_wait_for_set(const volatile uint32_t* reg, uint32_t mask,
                                            zx_time_t timeout) {
    int i = 0;
    zx_time_t start_time = zx_clock_get_monotonic();
    do {
        if (ahci_read(reg) & mask) return ZX_OK;
        usleep(10 * 1000);
        i++;
    } while (zx_clock_get_monotonic() - start_time < timeout);
    return ZX_ERR_TIMED_OUT;
}

static bool ahci_port_valid(ahci_device_t* dev, int portnr) {
    if (portnr >= AHCI_MAX_PORTS) {
        return false;
    }
    ahci_port_t* port = &dev->ports[portnr];
    int flags = AHCI_PORT_FLAG_IMPLEMENTED | AHCI_PORT_FLAG_PRESENT;
    return (port->flags & flags) == flags;
}

static void ahci_port_disable(ahci_port_t* port) {
    uint32_t cmd = ahci_read(&port->regs->cmd);
    if (!(cmd & AHCI_PORT_CMD_ST)) return;
    cmd &= ~AHCI_PORT_CMD_ST;
    ahci_write(&port->regs->cmd, cmd);
    zx_status_t status = ahci_wait_for_clear(&port->regs->cmd, AHCI_PORT_CMD_CR, 500 * 1000 * 1000);
    if (status) {
        zxlogf(ERROR, "ahci.%d: port disable timed out\n", port->nr);
    }
}

static void ahci_port_enable(ahci_port_t* port) {
    uint32_t cmd = ahci_read(&port->regs->cmd);
    if (cmd & AHCI_PORT_CMD_ST) return;
    if (!(cmd & AHCI_PORT_CMD_FRE)) {
        zxlogf(ERROR, "ahci.%d: cannot enable port without FRE enabled\n", port->nr);
        return;
    }
    zx_status_t status = ahci_wait_for_clear(&port->regs->cmd, AHCI_PORT_CMD_CR, 500 * 1000 * 1000);
    if (status) {
        zxlogf(ERROR, "ahci.%d: dma engine still running when enabling port\n", port->nr);
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
    zx_status_t status = ahci_wait_for_clear(&port->regs->tfd, AHCI_PORT_TFD_BUSY | AHCI_PORT_TFD_DATA_REQUEST, 1000 * 1000 * 1000);
    if (status < 0) {
        // if busy is not cleared, do a full comreset
        zxlogf(SPEW, "ahci.%d: timed out waiting for port idle, resetting\n", port->nr);
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
    status = ahci_wait_for_set(&port->regs->ssts, AHCI_PORT_SSTS_DET_PRESENT, 1llu * 1000 * 1000 * 1000);
    if ((driver_get_log_flags() & DDK_LOG_SPEW) && (status < 0)) {
        zxlogf(SPEW, "ahci.%d: no device detected\n", port->nr);
    }

    // clear error
    ahci_write(&port->regs->serr, ahci_read(&port->regs->serr));
}

static bool ahci_port_cmd_busy(ahci_port_t* port, int slot) {
    // a command slot is busy if a transaction is in flight or pending to be completed
    return ((ahci_read(&port->regs->sact) | ahci_read(&port->regs->ci)) & (1 << slot)) ||
           (port->commands[slot] != NULL) ||
           (port->running & (1 << slot)) || (port->completed & (1 << slot));
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

static void ahci_port_complete_txn(ahci_device_t* dev, ahci_port_t* port, zx_status_t status) {
    mtx_lock(&port->lock);
    uint32_t sact = ahci_read(&port->regs->sact);
    uint32_t running = port->running;
    uint32_t done = sact ^ running;
    // assert if a command slot without an outstanding transaction is active
    ZX_DEBUG_ASSERT(!(done & sact));
    port->completed |= done;
    mtx_unlock(&port->lock);
    // hit the worker thread to complete commands
    completion_signal(&dev->worker_completion);
}

static zx_status_t ahci_do_txn(ahci_device_t* dev, ahci_port_t* port, int slot, sata_txn_t* txn) {
    assert(slot < AHCI_MAX_COMMANDS);
    assert(!ahci_port_cmd_busy(port, slot));

    uint64_t offset_vmo = txn->bop.rw.offset_vmo * port->devinfo.block_size;
    uint64_t bytes = txn->bop.rw.length * port->devinfo.block_size;
    size_t pagecount = ((offset_vmo & (PAGE_SIZE - 1)) + bytes + (PAGE_SIZE - 1)) /
                       PAGE_SIZE;
    zx_paddr_t pages[AHCI_MAX_PAGES];
    if (pagecount > AHCI_MAX_PAGES) {
        zxlogf(SPEW, "ahci.%d: txn %p too many pages (%zd)\n", port->nr, txn, pagecount);
        return ZX_ERR_INVALID_ARGS;
    }

    zx_handle_t vmo = txn->bop.rw.vmo;
    bool is_write = cmd_is_write(txn->cmd);
    uint32_t options = is_write ? ZX_BTI_PERM_READ : ZX_BTI_PERM_WRITE;
    zx_handle_t pmt;
    zx_status_t st = zx_bti_pin(dev->bti_handle, options, vmo, offset_vmo & ~PAGE_MASK,
                                pagecount * PAGE_SIZE, pages, pagecount, &pmt);
    if (st != ZX_OK) {
        zxlogf(SPEW, "ahci.%d: failed to pin pages, err = %d\n", port->nr, st);
        return st;
    }
    txn->pmt = pmt;

    phys_iter_buffer_t physbuf = {
        .phys = pages,
        .phys_count = pagecount,
        .length = bytes,
        .vmo_offset = offset_vmo,
    };
    phys_iter_t iter;
    phys_iter_init(&iter, &physbuf, AHCI_PRD_MAX_SIZE);

    uint8_t cmd = txn->cmd;
    uint8_t device = txn->device;
    uint64_t lba = txn->bop.rw.offset_dev;
    uint64_t count = txn->bop.rw.length;

    // use queued command if available
    if (dev->cap & AHCI_CAP_NCQ) {
        if (cmd == SATA_CMD_READ_DMA_EXT) {
            cmd = SATA_CMD_READ_FPDMA_QUEUED;
        } else if (cmd == SATA_CMD_WRITE_DMA_EXT) {
            cmd = SATA_CMD_WRITE_FPDMA_QUEUED;
        }
    }

    // build the command
    ahci_cl_t* cl = port->cl + slot;
    // don't clear the cl since we set up ctba/ctbau at init
    cl->prdtl_flags_cfl = 0;
    cl->cfl = 5; // 20 bytes
    cl->w = is_write ? 1 : 0;
    cl->prdbc = 0;
    memset(port->ct[slot], 0, sizeof(ahci_ct_t));

    uint8_t* cfis = port->ct[slot]->cfis;
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
    ahci_prd_t* prd = (ahci_prd_t*)((void*)port->ct[slot] + sizeof(ahci_ct_t));
    size_t length;
    zx_paddr_t paddr;
    for (;;) {
        length = phys_iter_next(&iter, &paddr);
        if (length == 0) {
            break;
        } else if (length > AHCI_PRD_MAX_SIZE) {
            zxlogf(ERROR, "ahci.%d: chunk size > %zu is unsupported\n", port->nr, length);
            return ZX_ERR_NOT_SUPPORTED;;
        } else if (cl->prdtl == AHCI_MAX_PRDS) {
            zxlogf(ERROR, "ahci.%d: txn with more than %d chunks is unsupported\n",
                    port->nr, cl->prdtl);
            return ZX_ERR_NOT_SUPPORTED;
        }

        prd->dba = LO32(paddr);
        prd->dbau = HI32(paddr);
        prd->dbc = ((length - 1) & (AHCI_PRD_MAX_SIZE - 1)); // 0-based byte count
        cl->prdtl += 1;
        prd += 1;
    }

    port->running |= (1 << slot);
    port->commands[slot] = txn;

    zxlogf(SPEW, "ahci.%d: do_txn txn %p (%c) offset 0x%" PRIx64 " length 0x%" PRIx64
                  " slot %d prdtl %u\n",
            port->nr, txn, cl->w ? 'w' : 'r', lba, count, slot, cl->prdtl);
    prd = (ahci_prd_t*)((void*)port->ct[slot] + sizeof(ahci_ct_t));
    if (driver_get_log_flags() & DDK_LOG_SPEW) {
        for (uint i = 0; i < cl->prdtl; i++) {
            zxlogf(SPEW, "%04u: dbau=0x%08x dba=0x%08x dbc=0x%x\n",
                    i, prd->dbau, prd->dba, prd->dbc);
            prd += 1;
        }
    }

    // start command
    if (cmd_is_queued(cmd)) {
        ahci_write(&port->regs->sact, (1 << slot));
    }
    ahci_write(&port->regs->ci, (1 << slot));

    // set the watchdog
    // TODO: general timeout mechanism
    txn->timeout = zx_clock_get_monotonic() + ZX_SEC(1);
    completion_signal(&dev->watchdog_completion);
    return ZX_OK;
}

static zx_status_t ahci_port_initialize(ahci_device_t* dev, ahci_port_t* port) {
    uint32_t cmd = ahci_read(&port->regs->cmd);
    if (cmd & (AHCI_PORT_CMD_ST | AHCI_PORT_CMD_FRE | AHCI_PORT_CMD_CR | AHCI_PORT_CMD_FR)) {
        zxlogf(ERROR, "ahci.%d: port busy\n", port->nr);
        return ZX_ERR_UNAVAILABLE;
    }

    // allocate memory for the command list, FIS receive area, command table and PRDT
    size_t ct_prd_sz = sizeof(ahci_ct_t) + sizeof(ahci_prd_t) * AHCI_MAX_PRDS;
    size_t ct_prd_padding = 0x80 - (ct_prd_sz & (0x80 - 1)); // 128-byte aligned
    size_t mem_sz = sizeof(ahci_fis_t) + sizeof(ahci_cl_t) * AHCI_MAX_COMMANDS
                    + (ct_prd_sz + ct_prd_padding) * AHCI_MAX_COMMANDS;
    zx_status_t status = io_buffer_init(&port->buffer, dev->bti_handle,
                                        mem_sz, IO_BUFFER_RW | IO_BUFFER_CONTIG);
    if (status < 0) {
        zxlogf(ERROR, "ahci.%d: error %d allocating dma memory\n", port->nr, status);
        return status;
    }
    zx_paddr_t mem_phys = io_buffer_phys(&port->buffer);
    void* mem = io_buffer_virt(&port->buffer);

    // clear memory area
    // order is command list (1024-byte aligned)
    //          FIS receive area (256-byte aligned)
    //          command table + PRDT (127-byte aligned)
    memset(mem, 0, mem_sz);

    // command list
    ahci_write(&port->regs->clb, LO32(mem_phys));
    ahci_write(&port->regs->clbu, HI32(mem_phys));
    mem_phys += sizeof(ahci_cl_t) * AHCI_MAX_COMMANDS;
    port->cl = mem;
    mem += sizeof(ahci_cl_t) * AHCI_MAX_COMMANDS;

    // FIS receive area
    ahci_write(&port->regs->fb, LO32(mem_phys));
    ahci_write(&port->regs->fbu, HI32(mem_phys));
    mem_phys += sizeof(ahci_fis_t);
    port->fis = mem;
    mem += sizeof(ahci_fis_t);

    // command table, followed by PRDT
    for (int i = 0; i < AHCI_MAX_COMMANDS; i++) {
        port->cl[i].ctba = LO32(mem_phys);
        port->cl[i].ctbau = HI32(mem_phys);
        mem_phys += ct_prd_sz + ct_prd_padding;
        port->ct[i] = mem;
        mem += ct_prd_sz + ct_prd_padding;
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

static void ahci_enable_ahci(ahci_device_t* dev) {
    uint32_t ghc = ahci_read(&dev->regs->ghc);
    if (ghc & AHCI_GHC_AE) return;
    for (int i = 0; i < 5; i++) {
        ghc |= AHCI_GHC_AE;
        ahci_write(&dev->regs->ghc, ghc);
        ghc = ahci_read(&dev->regs->ghc);
        if (ghc & AHCI_GHC_AE) return;
        usleep(10 * 1000);
    }
}

static void ahci_hba_reset(ahci_device_t* dev) {
    // AHCI 1.3: Software may perform an HBA reset prior to initializing the controller
    uint32_t ghc = ahci_read(&dev->regs->ghc);
    ghc |= AHCI_GHC_AE;
    ahci_write(&dev->regs->ghc, ghc);
    ghc |= AHCI_GHC_HR;
    ahci_write(&dev->regs->ghc, ghc);
    // reset should complete within 1 second
    zx_status_t status = ahci_wait_for_clear(&dev->regs->ghc, AHCI_GHC_HR, 1000 * 1000 * 1000);
    if (status) {
        zxlogf(ERROR, "ahci: hba reset timed out\n");
    }
}

void ahci_set_devinfo(ahci_device_t* device, int portnr, sata_devinfo_t* devinfo) {
    ZX_DEBUG_ASSERT(ahci_port_valid(device, portnr));
    ahci_port_t* port = &device->ports[portnr];
    memcpy(&port->devinfo, devinfo, sizeof(port->devinfo));
}

void ahci_queue(ahci_device_t* device, int portnr, sata_txn_t* txn) {
    ZX_DEBUG_ASSERT(ahci_port_valid(device, portnr));

    ahci_port_t* port = &device->ports[portnr];

    zxlogf(SPEW, "ahci.%d: queue_txn txn %p offset_dev 0x%" PRIx64 " length 0x%x\n",
            port->nr, txn, txn->bop.rw.offset_dev, txn->bop.rw.length);

    // reset the physical address
    txn->pmt = ZX_HANDLE_INVALID;

    // put the cmd on the queue
    mtx_lock(&port->lock);
    list_add_tail(&port->txn_list, &txn->node);

    // hit the worker thread
    completion_signal(&device->worker_completion);
    mtx_unlock(&port->lock);
}

static void ahci_release(void* ctx) {
    // FIXME - join threads created by this driver
    ahci_device_t* device = ctx;
    zx_handle_close(device->irq_handle);
    zx_handle_close(device->bti_handle);
    free(device);
}

// worker thread

static int ahci_worker_thread(void* arg) {
    ahci_device_t* dev = (ahci_device_t*)arg;
    ahci_port_t* port;
    sata_txn_t* txn;
    for (;;) {
        // iterate all the ports and run or complete commands
        for (int i = 0; i < AHCI_MAX_PORTS; i++) {
            port = &dev->ports[i];
            mtx_lock(&port->lock);
            if (!ahci_port_valid(dev, i)) {
                goto next;
            }

            // complete commands first
            while (port->completed) {
                unsigned slot = 32 - __builtin_clz(port->completed) - 1;
                txn = port->commands[slot];
                if (txn == NULL) {
                    zxlogf(ERROR, "ahci.%d: illegal state, completing slot %d but txn == NULL\n",
                            port->nr, slot);
                } else {
                    mtx_unlock(&port->lock);
                    if (txn->pmt != ZX_HANDLE_INVALID) {
                        zx_pmt_unpin(txn->pmt);
                    }
                    zxlogf(SPEW, "ahci.%d: complete txn %p\n", port->nr, txn);
                    block_complete(&txn->bop, ZX_OK);
                    mtx_lock(&port->lock);
                }
                port->completed &= ~(1 << slot);
                port->running &= ~(1 << slot);
                port->commands[slot] = NULL;
                // resume the port if paused for sync and no outstanding transactions
                if ((port->flags & AHCI_PORT_FLAG_SYNC_PAUSED) && !port->running) {
                    port->flags &= ~AHCI_PORT_FLAG_SYNC_PAUSED;
                    if (port->sync) {
                        block_op_t* sop = &port->sync->bop;
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
                int max = MIN(port->devinfo.max_cmd, (int)((dev->cap >> 8) & 0x1f));
                int i = 0;
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
                        block_complete(&txn->bop, ZX_OK);
                        mtx_lock(&port->lock);
                    }
                } else {
                    // run the transaction
                    zx_status_t st = ahci_do_txn(dev, port, i, txn);
                    // complete the transaction with if it failed during processing
                    if (st != ZX_OK) {
                        mtx_unlock(&port->lock);
                        block_complete(&txn->bop, st);
                        mtx_lock(&port->lock);
                        continue;
                    }
                }
            }
next:
            mtx_unlock(&port->lock);
        }
        // wait here until more commands are queued, or a port becomes idle
        completion_wait(&dev->worker_completion, ZX_TIME_INFINITE);
        completion_reset(&dev->worker_completion);
    }
    return 0;
}

static int ahci_watchdog_thread(void* arg) {
    ahci_device_t* dev = (ahci_device_t*)arg;
    for (;;) {
        bool idle = true;
        zx_time_t now = zx_clock_get_monotonic();
        for (int i = 0; i < AHCI_MAX_PORTS; i++) {
            ahci_port_t* port = &dev->ports[i];
            if (!ahci_port_valid(dev, i)) {
                continue;
            }

            mtx_lock(&port->lock);
            uint32_t pending = port->running & ~port->completed;
            while (pending) {
                idle = false;
                unsigned slot = 32 - __builtin_clz(pending) - 1;
                sata_txn_t* txn = port->commands[slot];
                if (!txn) {
                    zxlogf(ERROR, "ahci: command %u pending but txn is NULL\n", slot);
                } else {
                    if (txn->timeout < now) {
                        // time out
                        zxlogf(ERROR, "ahci: txn time out on port %d txn %p\n", port->nr, txn);
                        port->running &= ~(1 << slot);
                        port->commands[slot] = NULL;
                        mtx_unlock(&port->lock);
                        block_complete(&txn->bop, ZX_ERR_TIMED_OUT);
                        mtx_lock(&port->lock);
                    }
                }
                pending &= ~(1 << slot);
            }
            mtx_unlock(&port->lock);
        }

        // no need to run the watchdog if there are no active xfers
        completion_wait(&dev->watchdog_completion, idle ? ZX_TIME_INFINITE : 5ULL * 1000 * 1000 * 1000);
        completion_reset(&dev->watchdog_completion);
    }
    return 0;
}

// irq handler:

static void ahci_port_irq(ahci_device_t* dev, int nr) {
    ahci_port_t* port = &dev->ports[nr];
    // clear interrupt
    uint32_t is = ahci_read(&port->regs->is);
    ahci_write(&port->regs->is, is);

    if (is & AHCI_PORT_INT_PRC) { // PhyRdy change
        uint32_t serr = ahci_read(&port->regs->serr);
        ahci_write(&port->regs->serr, serr & ~0x1);
    }
    if (is & AHCI_PORT_INT_ERROR) { // error
        zxlogf(ERROR, "ahci.%d: error is=0x%08x\n", nr, is);
        ahci_port_complete_txn(dev, port, ZX_ERR_INTERNAL);
    } else if (is) {
        ahci_port_complete_txn(dev, port, ZX_OK);
    }
}

static int ahci_irq_thread(void* arg) {
    ahci_device_t* dev = (ahci_device_t*)arg;
    zx_status_t status;
    for (;;) {
        status = zx_interrupt_wait(dev->irq_handle, NULL);
        if (status) {
            zxlogf(ERROR, "ahci: error %d waiting for interrupt\n", status);
            continue;
        }
        // mask hba interrupts while interrupts are being handled
        uint32_t ghc = ahci_read(&dev->regs->ghc);
        ahci_write(&dev->regs->ghc, ghc & ~AHCI_GHC_IE);

        // handle interrupt for each port
        uint32_t is = ahci_read(&dev->regs->is);
        ahci_write(&dev->regs->is, is);
        for (int i = 0; is && i < AHCI_MAX_PORTS; i++) {
            if (is & 0x1) {
                ahci_port_irq(dev, i);
            }
            is >>= 1;
        }

        // unmask hba interrupts
        ghc = ahci_read(&dev->regs->ghc);
        ahci_write(&dev->regs->ghc, ghc | AHCI_GHC_IE);
    }
    return 0;
}

// implement device protocol:

static zx_protocol_device_t ahci_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .release = ahci_release,
};

extern zx_protocol_device_t ahci_port_device_proto;

static int ahci_init_thread(void* arg) {
    ahci_device_t* dev = (ahci_device_t*)arg;

    // reset
    ahci_hba_reset(dev);

    // enable ahci mode
    ahci_enable_ahci(dev);

    dev->cap = ahci_read(&dev->regs->cap);

    // count number of ports
    uint32_t port_map = ahci_read(&dev->regs->pi);

    // initialize ports
    zx_status_t status;
    ahci_port_t* port;
    for (int i = 0; i < AHCI_MAX_PORTS; i++) {
        port = &dev->ports[i];
        port->nr = i;

        if (!(port_map & (1 << i))) continue; // port not implemented

        port->flags = AHCI_PORT_FLAG_IMPLEMENTED;
        port->regs = &dev->regs->ports[i];
        list_initialize(&port->txn_list);

        status = ahci_port_initialize(dev, port);
        if (status) goto fail;
    }

    // clear hba interrupts
    ahci_write(&dev->regs->is, ahci_read(&dev->regs->is));

    // enable hba interrupts
    uint32_t ghc = ahci_read(&dev->regs->ghc);
    ghc |= AHCI_GHC_IE;
    ahci_write(&dev->regs->ghc, ghc);

    // this part of port init happens after enabling interrupts in ghc
    for (int i = 0; i < AHCI_MAX_PORTS; i++) {
        port = &dev->ports[i];
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
                sata_bind(dev, dev->zxdev, port->nr);
            }
        }
    }

    return ZX_OK;
fail:
    free(dev->ports);
    return status;
}

// implement driver object:

static zx_status_t ahci_bind(void* ctx, zx_device_t* dev) {
    // map resources and initalize the device
    ahci_device_t* device = calloc(1, sizeof(ahci_device_t));
    if (!device) {
        zxlogf(ERROR, "ahci: out of memory\n");
        return ZX_ERR_NO_MEMORY;
    }

    if (device_get_protocol(dev, ZX_PROTOCOL_PCI, &device->pci)) {
        free(device);
        return ZX_ERR_NOT_SUPPORTED;
    }

    // map register window
    zx_status_t status = pci_map_bar(&device->pci,
                                          5u,
                                          ZX_CACHE_POLICY_UNCACHED_DEVICE,
                                          (void**)&device->regs,
                                          &device->regs_size,
                                          &device->regs_handle);
    if (status != ZX_OK) {
        zxlogf(ERROR, "ahci: error %d mapping register window\n", status);
        goto fail;
    }

    zx_pcie_device_info_t config;
    status = pci_get_device_info(&device->pci, &config);
    if (status != ZX_OK) {
        zxlogf(ERROR, "ahci: error getting config information\n");
        goto fail;
    }

    if (config.sub_class != 0x06 && config.base_class == 0x01) { // SATA
        status = ZX_ERR_NOT_SUPPORTED;
        zxlogf(ERROR, "ahci: device class 0x%x unsupported!\n", config.sub_class);
        goto fail;
    }

    // FIXME intel devices need to set SATA port enable at config + 0x92
    // ahci controller is bus master
    status = pci_enable_bus_master(&device->pci, true);
    if (status < 0) {
        zxlogf(ERROR, "ahci: error %d in enable bus master\n", status);
        goto fail;
    }

    // Query and configure IRQ modes by trying MSI first and falling back to
    // legacy if necessary.
    uint32_t irq_cnt;
    zx_pci_irq_mode_t irq_mode = ZX_PCIE_IRQ_MODE_MSI;
    status = pci_query_irq_mode(&device->pci, ZX_PCIE_IRQ_MODE_MSI, &irq_cnt);
    if (status == ZX_ERR_NOT_SUPPORTED) {
        status = pci_query_irq_mode(&device->pci, ZX_PCIE_IRQ_MODE_LEGACY, &irq_cnt);
        if (status != ZX_OK) {
            zxlogf(ERROR, "ahci: neither MSI nor legacy interrupts are supported\n");
            goto fail;
        } else {
            irq_mode = ZX_PCIE_IRQ_MODE_LEGACY;
        }
    }

    if (irq_cnt == 0) {
        zxlogf(ERROR, "ahci: no interrupts available\n");
        status = ZX_ERR_NO_RESOURCES;
        goto fail;
    }

    zxlogf(INFO, "ahci: using %s interrupt\n", (irq_mode == ZX_PCIE_IRQ_MODE_MSI) ? "MSI" : "legacy");
    status = pci_set_irq_mode(&device->pci, irq_mode, 1);
    if (status != ZX_OK) {
        zxlogf(ERROR, "ahci: error %d setting irq mode\n", status);
        goto fail;
    }

    // get bti handle
    status = pci_get_bti(&device->pci, 0, &device->bti_handle);
    if (status != ZX_OK) {
        zxlogf(ERROR, "ahci: error %d getting bti handle\n", status);
        goto fail;
    }

    // get irq handle
    status = pci_map_interrupt(&device->pci, 0, &device->irq_handle);
    if (status != ZX_OK) {
        zxlogf(ERROR, "ahci: error %d getting irq handle\n", status);
        goto fail;
    }

    // start irq thread
    int ret = thrd_create_with_name(&device->irq_thread, ahci_irq_thread, device, "ahci-irq");
    if (ret != thrd_success) {
        zxlogf(ERROR, "ahci: error %d in irq thread create\n", ret);
        goto fail;
    }

    // start watchdog thread
    device->watchdog_completion = COMPLETION_INIT;
    thrd_create_with_name(&device->watchdog_thread, ahci_watchdog_thread, device, "ahci-watchdog");

    // start worker thread (for iotxn queue)
    device->worker_completion = COMPLETION_INIT;
    ret = thrd_create_with_name(&device->worker_thread, ahci_worker_thread, device, "ahci-worker");
    if (ret != thrd_success) {
        zxlogf(ERROR, "ahci: error %d in worker thread create\n", ret);
        goto fail;
    }

    // add the device for the controller
    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "ahci",
        .ctx = device,
        .ops = &ahci_device_proto,
        .flags = DEVICE_ADD_NON_BINDABLE,
    };

    status = device_add(dev, &args, &device->zxdev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "ahci: error %d in device_add\n", status);
        goto fail;
    }

    // initialize controller and detect devices
    thrd_t t;
    ret = thrd_create_with_name(&t, ahci_init_thread, device, "ahci-init");
    if (ret != thrd_success) {
        zxlogf(ERROR, "ahci: error %d in init thread create\n", status);
        goto fail;
    }

    return ZX_OK;
fail:
    // FIXME unmap, and join any threads created above
    free(device);
    return status;
}

static zx_driver_ops_t ahci_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = ahci_bind,
};

// clang-format off
ZIRCON_DRIVER_BEGIN(ahci, ahci_driver_ops, "zircon", "0.1", 4)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PCI),
    BI_ABORT_IF(NE, BIND_PCI_CLASS, 0x01),
    BI_ABORT_IF(NE, BIND_PCI_SUBCLASS, 0x06),
    BI_MATCH_IF(EQ, BIND_PCI_INTERFACE, 0x01),
ZIRCON_DRIVER_END(ahci)
