// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/completion.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/binding.h>
#include <ddk/protocol/pci.h>

#include <hexdump/hexdump.h>
#include <magenta/syscalls.h>
#include <magenta/types.h>
#include <magenta/listnode.h>
#include <sys/param.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <threads.h>
#include <unistd.h>

#include "ahci.h"
#include "sata.h"

#define INTEL_AHCI_VID      (0x8086)
#define LYNX_POINT_AHCI_DID (0x8c02)
#define WILDCAT_AHCI_DID    (0x9c83)
#define SUNRISE_AHCI_DID    (0x9d03)
#define ICH9_AHCI_DID       (0x2922)

#define AMD_AHCI_VID        (0x1022)
#define AMD_FCH_AHCI_DID    (0x7801)

#define TRACE 1

#if TRACE
#define xprintf(fmt...) printf(fmt)
#else
#define xprintf(fmt...) \
    do {                \
    } while (0)
#endif

#define ahci_read(reg)       pcie_read32(reg)
#define ahci_write(reg, val) pcie_write32(reg, val)

#ifdef IS_64BIT
#define HI32(val) (((val) >> 32) & 0xffffffff)
#define LO32(val) ((val) & 0xffffffff)
#else
#define HI32(val) 0
#define LO32(val) (val)
#endif

#define AHCI_PORT_FLAG_IMPLEMENTED (1 << 0)
#define AHCI_PORT_FLAG_PRESENT     (1 << 1)

typedef struct ahci_port {
    int nr; // 0-based
    int flags;

    ahci_port_reg_t* regs;
    ahci_cl_t* cl;
    ahci_fis_t* fis;
    ahci_ct_t* ct[AHCI_MAX_COMMANDS];

    uint32_t running; // bitmask of running commands
    iotxn_t* commands[AHCI_MAX_COMMANDS]; // commands in flight

    mtx_t lock; // protects txn_list
    list_node_t txn_list;
} ahci_port_t;

typedef struct ahci_device {
    mx_device_t device;

    ahci_hba_t* regs;
    uint64_t regs_size;
    mx_handle_t regs_handle;

    pci_protocol_t* pci;

    mx_handle_t irq_handle;
    thrd_t irq_thread;

    thrd_t worker_thread;
    completion_t worker_completion;

    thrd_t watchdog_thread;
    completion_t watchdog_completion;

    uint32_t cap;

    ahci_port_t ports[AHCI_MAX_PORTS];
} ahci_device_t;

#define get_ahci_device(dev) containerof(dev, ahci_device_t, device)

static inline mx_status_t ahci_wait_for_clear(const volatile uint32_t* reg, uint32_t mask, mx_time_t timeout) {
    int i = 0;
    mx_time_t start_time = mx_current_time();
    do {
        if (!(ahci_read(reg) & mask)) return NO_ERROR;
        usleep(10 * 1000);
        i++;
    } while (mx_current_time() - start_time < timeout);
    return ERR_TIMED_OUT;
}

static inline mx_status_t ahci_wait_for_set(const volatile uint32_t* reg, uint32_t mask, mx_time_t timeout) {
    int i = 0;
    mx_time_t start_time = mx_current_time();
    do {
        if (ahci_read(reg) & mask) return NO_ERROR;
        usleep(10 * 1000);
        i++;
    } while (mx_current_time() - start_time < timeout);
    return ERR_TIMED_OUT;
}

static void ahci_port_disable(ahci_port_t* port) {
    uint32_t cmd = ahci_read(&port->regs->cmd);
    if (!(cmd & AHCI_PORT_CMD_ST)) return;
    cmd &= ~AHCI_PORT_CMD_ST;
    ahci_write(&port->regs->cmd, cmd);
    mx_status_t status = ahci_wait_for_clear(&port->regs->cmd, AHCI_PORT_CMD_CR, 500 * 1000 * 1000);
    if (status) {
        xprintf("ahci.%d: port disable timed out\n", port->nr);
    }
}

static void ahci_port_enable(ahci_port_t* port) {
    uint32_t cmd = ahci_read(&port->regs->cmd);
    if (cmd & AHCI_PORT_CMD_ST) return;
    if (!(cmd & AHCI_PORT_CMD_FRE)) {
        xprintf("ahci.%d: cannot enable port without FRE enabled\n", port->nr);
        return;
    }
    mx_status_t status = ahci_wait_for_clear(&port->regs->cmd, AHCI_PORT_CMD_CR, 500 * 1000 * 1000);
    if (status) {
        xprintf("ahci.%d: dma engine still running when enabling port\n", port->nr);
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
    mx_status_t status = ahci_wait_for_clear(&port->regs->tfd, AHCI_PORT_TFD_BUSY | AHCI_PORT_TFD_DATA_REQUEST, 1000 * 1000 * 1000);
    if (status < 0) {
        // if busy is not cleared, do a full comreset
        xprintf("ahci.%d: timed out waiting for port idle, resetting\n", port->nr);
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
#if VERBOSE
    if (status < 0) {
        xprintf("ahci.%d: no device detected\n", port->nr);
    }
#endif

    // clear error
    ahci_write(&port->regs->serr, ahci_read(&port->regs->serr));
}

static bool ahci_port_cmd_busy(ahci_port_t* port, int slot) {
    return ((ahci_read(&port->regs->sact) | ahci_read(&port->regs->ci)) & slot) || (port->commands[slot] != NULL);
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

static mx_status_t ahci_do_txn(ahci_device_t* dev, ahci_port_t* port, int slot, iotxn_t* txn) {
    assert(slot < AHCI_MAX_COMMANDS);
    assert(!ahci_port_cmd_busy(port, slot));

    sata_pdata_t* pdata = sata_iotxn_pdata(txn);
    mx_paddr_t phys;
    txn->ops->physmap(txn, &phys);

    if (dev->cap & AHCI_CAP_NCQ) {
        if (pdata->cmd == SATA_CMD_READ_DMA_EXT) {
            pdata->cmd = SATA_CMD_READ_FPDMA_QUEUED;
        } else if (pdata->cmd == SATA_CMD_WRITE_DMA_EXT) {
            pdata->cmd = SATA_CMD_WRITE_FPDMA_QUEUED;
        }
    }

    //xprintf("ahci.%d: do_txn slot=%d cmd=0x%x device=0x%x lba=0x%llx count=%u phys=0x%lx data_sz=0x%llx offset=0x%llx\n", port->nr, slot, pdata->cmd, pdata->device, pdata->lba, pdata->count, phys, txn->length, txn->offset);

    // build the command
    ahci_cl_t* cl = port->cl + slot;
    // don't clear the cl since we set up ctba/ctbau at init
    cl->prdtl_flags_cfl = 0;
    cl->cfl = 5; // 20 bytes
    cl->w = cmd_is_write(pdata->cmd) ? 1 : 0;
    cl->prdtl = txn->length / AHCI_PRD_MAX_SIZE + 1;
    cl->prdbc = 0;
    memset(port->ct[slot], 0, sizeof(ahci_ct_t));

    uint8_t* cfis = port->ct[slot]->cfis;
    cfis[0] = 0x27; // host-to-device
    cfis[1] = 0x80; // command
    cfis[2] = pdata->cmd;
    cfis[7] = pdata->device;

    // some commands have lba/count fields
    if (pdata->cmd == SATA_CMD_READ_DMA_EXT ||
        pdata->cmd == SATA_CMD_WRITE_DMA_EXT) {
        cfis[4] = pdata->lba & 0xff;
        cfis[5] = (pdata->lba >> 8) & 0xff;
        cfis[6] = (pdata->lba >> 16) & 0xff;
        cfis[8] = (pdata->lba >> 24) & 0xff;
        cfis[9] = (pdata->lba >> 32) & 0xff;
        cfis[10] = (pdata->lba >> 40) & 0xff;
        cfis[12] = pdata->count & 0xff;
        cfis[13] = (pdata->count >> 8) & 0xff;
    } else if (cmd_is_queued(pdata->cmd)) {
        cfis[4] = pdata->lba & 0xff;
        cfis[5] = (pdata->lba >> 8) & 0xff;
        cfis[6] = (pdata->lba >> 16) & 0xff;
        cfis[8] = (pdata->lba >> 24) & 0xff;
        cfis[9] = (pdata->lba >> 32) & 0xff;
        cfis[10] = (pdata->lba >> 40) & 0xff;
        cfis[3] = pdata->count & 0xff;
        cfis[11] = (pdata->count >> 8) & 0xff;
        cfis[12] = (slot << 3) & 0xff; // tag
        cfis[13] = 0; // normal priority
    }

    ahci_prd_t* prd = NULL;
    for (int i = 0; i < cl->prdtl; i++) {
        prd = (ahci_prd_t*)((void*)port->ct[slot] + sizeof(ahci_ct_t)) + i;
        prd->dba = LO32(phys);
        prd->dbau = HI32(phys);
        prd->dbc = ((txn->length - 1) & 0x3fffff); // 0-based byte count

        phys += AHCI_PRD_MAX_SIZE;
    }

    port->running |= (1 << slot);
    port->commands[slot] = txn;

    // start command
    if (cmd_is_queued(pdata->cmd)) {
        ahci_write(&port->regs->sact, ahci_read(&port->regs->sact) | (1 << slot));
    }
    ahci_write(&port->regs->ci, ahci_read(&port->regs->ci) | (1 << slot));

    // set the watchdog
    // TODO: general timeout mechanism
    pdata->timeout = mx_current_time() + 1000 * 1000 * 1000; // 1 second
    completion_signal(&dev->watchdog_completion);
    return NO_ERROR;
}

static void ahci_port_complete_txn(ahci_device_t* dev, ahci_port_t* port, mx_status_t status) {
    iotxn_t* txn;
    uint32_t sact = ahci_read(&port->regs->sact);
    for (int i = 0; i < AHCI_MAX_COMMANDS; i++) {
        txn = port->commands[i];
        if (txn == NULL) {
            continue;
        }
        if (!(sact & (1 << i))) {
            // clear state before calling the complete() hook
            mtx_lock(&port->lock);
            port->running &= ~(1 << i);
            port->commands[i] = NULL;
            mtx_unlock(&port->lock);

            txn->ops->complete(txn, status, txn->length);
        }
    }
    // hit the worker thread to do the next txn
    completion_signal(&dev->worker_completion);
}

static mx_status_t ahci_port_initialize(ahci_port_t* port) {
    uint32_t cmd = ahci_read(&port->regs->cmd);
    if (cmd & (AHCI_PORT_CMD_ST | AHCI_PORT_CMD_FRE | AHCI_PORT_CMD_CR | AHCI_PORT_CMD_FR)) {
        xprintf("ahci.%d: port busy\n", port->nr);
        return ERR_UNAVAILABLE;
    }

    // allocate memory for the command list, FIS receive area, command table and PRDT
    size_t mem_sz = sizeof(ahci_fis_t) + sizeof(ahci_cl_t) * AHCI_MAX_COMMANDS + (sizeof(ahci_ct_t) + sizeof(ahci_prd_t) * AHCI_MAX_PRDS) * AHCI_MAX_COMMANDS;
    mx_paddr_t mem_phys;
    void* mem;
    mx_status_t status = mx_alloc_device_memory(get_root_resource(), mem_sz, &mem_phys, &mem);
    if (status < 0) {
        xprintf("ahci.%d: error %d allocating dma memory\n", port->nr, status);
        return status;
    }

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
        mem_phys += sizeof(ahci_ct_t) + sizeof(ahci_prd_t) * AHCI_MAX_PRDS;
        port->ct[i] = mem;
        mem += sizeof(ahci_ct_t) + sizeof(ahci_prd_t) * AHCI_MAX_PRDS;
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

    return NO_ERROR;
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
    mx_status_t status = ahci_wait_for_clear(&dev->regs->ghc, AHCI_GHC_HR, 1000 * 1000 * 1000);
    if (status) {
        xprintf("ahci: hba reset timed out\n");
    }
}

// public api:

void ahci_iotxn_queue(mx_device_t* dev, iotxn_t* txn) {
    sata_pdata_t* pdata = sata_iotxn_pdata(txn);
    ahci_device_t* device = get_ahci_device(dev);
    ahci_port_t* port = &device->ports[pdata->port];

    assert(pdata->port < AHCI_MAX_PORTS);
    assert(port->flags & (AHCI_PORT_FLAG_IMPLEMENTED | AHCI_PORT_FLAG_PRESENT));

    // put the cmd on the queue
    mtx_lock(&port->lock);
    list_add_tail(&port->txn_list, &txn->node);
    mtx_unlock(&port->lock);

    // hit the worker thread
    completion_signal(&device->worker_completion);
}

// worker thread (for iotxn queue):

static int ahci_worker_thread(void* arg) {
    ahci_device_t* dev = (ahci_device_t*)arg;
    ahci_port_t* port;
    iotxn_t* txn;
    for (;;) {
        // iterate all the ports and run commands
        for (int i = 0; i < AHCI_MAX_PORTS; i++) {
            port = &dev->ports[i];
            if (!(port->flags & (AHCI_PORT_FLAG_IMPLEMENTED | AHCI_PORT_FLAG_PRESENT))) continue;

            mtx_lock(&port->lock);
            txn = list_peek_head_type(&port->txn_list, iotxn_t, node);
            if (!txn) {
                mtx_unlock(&port->lock);
                continue;
            }

            // find a free command tag
            sata_pdata_t* pdata = sata_iotxn_pdata(txn);
            int max = MIN(pdata->max_cmd, (int)((dev->cap >> 8) & 0x1f));
            int i = 0;
            for (i = 0; i <= max; i++) {
                if (!ahci_port_cmd_busy(port, i)) break;
            }
            if (i > max) {
                mtx_unlock(&port->lock);
                continue;
            }

            // run the command
            list_delete(&txn->node);
            ahci_do_txn(dev, port, i, txn);
            mtx_unlock(&port->lock);
        }
        // wait here until more commands are queued, or a port becomes idle
        completion_wait(&dev->worker_completion, MX_TIME_INFINITE);
        completion_reset(&dev->worker_completion);
    }
    return 0;
}

static int ahci_watchdog_thread(void* arg) {
    ahci_device_t* dev = (ahci_device_t*)arg;
    for (;;) {
        bool idle = true;
        mx_time_t now = mx_current_time();
        for (int i = 0; i < AHCI_MAX_PORTS; i++) {
            ahci_port_t* port = &dev->ports[i];
            if (!(port->flags & (AHCI_PORT_FLAG_IMPLEMENTED | AHCI_PORT_FLAG_PRESENT))) {
                continue;
            }

            mtx_lock(&port->lock);
            if (port->running) {
                idle = false;
                for (int j = 0; j < AHCI_MAX_COMMANDS; j++) {
                    if (!port->commands[j]) {
                        continue;
                    }
                    sata_pdata_t* pdata = sata_iotxn_pdata(port->commands[j]);
                    if (pdata->timeout < now) {
                        // time out
                        printf("ahci: txn time out on port %d\n", port->nr);
                        iotxn_t* txn = port->commands[i];
                        port->running &= ~(1 << i);
                        port->commands[i] = NULL;
                        mtx_unlock(&port->lock);
                        txn->ops->complete(txn, ERR_TIMED_OUT, 0);
                        mtx_lock(&port->lock);
                    }
                }
            }
            mtx_unlock(&port->lock);
        }

        // no need to run the watchdog if there are no active xfers
        completion_wait(&dev->watchdog_completion, idle ? MX_TIME_INFINITE : 5ULL * 1000 * 1000 * 1000);
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

    if (is & AHCI_PORT_INT_DHR) { // RFIS received
        ahci_port_complete_txn(dev, port, NO_ERROR);
    }
    if (is & AHCI_PORT_INT_PS) { // PSFIS rcv
        ahci_port_complete_txn(dev, port, NO_ERROR);
    }
    if (is & AHCI_PORT_INT_SDB) { // SDBFIS
        ahci_port_complete_txn(dev, port, NO_ERROR);
    }
    if (is & AHCI_PORT_INT_PRC) { // PhyRdy change
        uint32_t serr = ahci_read(&port->regs->serr);
        ahci_write(&port->regs->serr, serr & ~0x1);
    }
    if (is & AHCI_PORT_INT_TFE) { // taskfile error
        xprintf("tfe error\n");
        ahci_port_complete_txn(dev, port, ERR_INTERNAL);
    }
}

static int ahci_irq_thread(void* arg) {
    ahci_device_t* dev = (ahci_device_t*)arg;
    mx_status_t status;
    for (;;) {
        status = mx_handle_wait_one(dev->irq_handle, MX_SIGNAL_SIGNALED, MX_TIME_INFINITE, NULL);
        if (status) {
            xprintf("ahci: error %d waiting for interrupt\n", status);
            continue;
        }
        // mask hba interrupts while interrupts are being handled
        uint32_t ghc = ahci_read(&dev->regs->ghc);
        ahci_write(&dev->regs->ghc, ghc & ~AHCI_GHC_IE);
        mx_interrupt_complete(dev->irq_handle);

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

static mx_protocol_device_t ahci_device_proto = {
};

extern mx_protocol_device_t ahci_port_device_proto;

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
    mx_status_t status;
    ahci_port_t* port;
    for (int i = 0; i < AHCI_MAX_PORTS; i++) {
        port = &dev->ports[i];
        port->nr = i;

        if (!(port_map & (1 << i))) continue; // port not implemented

        port->flags = AHCI_PORT_FLAG_IMPLEMENTED;
        port->regs = &dev->regs->ports[i];
        list_initialize(&port->txn_list);

        status = ahci_port_initialize(port);
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
                sata_bind(&dev->device, port->nr);
            }
        }
    }

    return NO_ERROR;
fail:
    free(dev->ports);
    return status;
}

// implement driver object:

static mx_status_t ahci_bind(mx_driver_t* drv, mx_device_t* dev) {
    pci_protocol_t* pci;
    if (device_get_protocol(dev, MX_PROTOCOL_PCI, (void**)&pci)) return ERR_NOT_SUPPORTED;

    mx_status_t status = pci->claim_device(dev);
    if (status < 0) {
        xprintf("ahci: error %d claiming pci device\n", status);
        return status;
    }

    // map resources and initalize the device
    ahci_device_t* device = calloc(1, sizeof(ahci_device_t));
    if (!device) {
        xprintf("ahci: out of memory\n");
        return ERR_NO_MEMORY;
    }

    device_init(&device->device, drv, "ahci", &ahci_device_proto);

    // map register window
    device->regs_handle = pci->map_mmio(dev, 5, MX_CACHE_POLICY_UNCACHED_DEVICE, (void*)&device->regs, &device->regs_size);
    if (device->regs_handle < 0) {
        status = device->regs_handle;
        xprintf("ahci: error %d mapping register window\n", status);
        goto fail;
    }

    const pci_config_t* config;
    mx_handle_t config_handle = pci->get_config(dev, &config);
    if (config_handle < 0) {
        status = config_handle;
        xprintf("ahci: error %d getting pci config\n", status);
        goto fail;
    }
    if (config->sub_class != 0x06 && config->base_class == 0x01) { // SATA
        status = ERR_NOT_SUPPORTED;
        xprintf("ahci: device class 0x%x unsupported!\n", config->sub_class);
        mx_handle_close(config_handle);
        goto fail;
    }
    // FIXME intel devices need to set SATA port enable at config + 0x92
    mx_handle_close(config_handle);

    // save for interrupt handler
    device->pci = pci;

    // ahci controller is bus master
    status = pci->enable_bus_master(dev, true);
    if (status < 0) {
        xprintf("ahci: error %d in enable bus master\n", status);
        goto fail;
    }

    // set msi irq mode
    status = pci->set_irq_mode(dev, MX_PCIE_IRQ_MODE_MSI, 1);
    if (status < 0) {
        xprintf("ahci: error %d setting irq mode\n", status);
        goto fail;
    }

    // get irq handle
    device->irq_handle = pci->map_interrupt(dev, 0);
    if (device->irq_handle < 0) {
        status = device->irq_handle;
        xprintf("ahci: error %d getting irq handle\n", status);
        goto fail;
    }

    // start irq thread
    int ret = thrd_create_with_name(&device->irq_thread, ahci_irq_thread, device, "ahci-irq");
    if (ret != thrd_success) {
        xprintf("ahci: error %d in irq thread create\n", ret);
        goto fail;
    }

    // start watchdog thread
    device->watchdog_completion = COMPLETION_INIT;
    thrd_create_with_name(&device->watchdog_thread, ahci_watchdog_thread, device, "ahci-watchdog");

    // start worker thread (for iotxn queue)
    device->worker_completion = COMPLETION_INIT;
    ret = thrd_create_with_name(&device->worker_thread, ahci_worker_thread, device, "ahci-worker");
    if (ret != thrd_success) {
        xprintf("ahci: error %d in worker thread create\n", ret);
        goto fail;
    }

    // add the device for the controller
    device_add(&device->device, dev);

    // initialize controller and detect devices
    thrd_t t;
    ret = thrd_create_with_name(&t, ahci_init_thread, device, "ahci-init");
    if (ret != thrd_success) {
        xprintf("ahci: error %d in init thread create\n", status);
        goto fail;
    }

    return NO_ERROR;
fail:
    // FIXME unmap
    free(device);
    return status;
}

mx_driver_t _driver_ahci = {
    .ops = {
        .bind = ahci_bind,
    },
};

MAGENTA_DRIVER_BEGIN(_driver_ahci, "ahci", "magenta", "0.1", 11)
    BI_ABORT_IF(NE, BIND_PROTOCOL, MX_PROTOCOL_PCI),
    BI_GOTO_IF(EQ, BIND_PCI_VID, AMD_AHCI_VID, 1),
    // intel devices
    BI_ABORT_IF(NE, BIND_PCI_VID, INTEL_AHCI_VID),
    BI_MATCH_IF(EQ, BIND_PCI_DID, LYNX_POINT_AHCI_DID), // Simics
    BI_MATCH_IF(EQ, BIND_PCI_DID, WILDCAT_AHCI_DID),    // Pixel2
    BI_MATCH_IF(EQ, BIND_PCI_DID, SUNRISE_AHCI_DID),    // NUC
    BI_MATCH_IF(EQ, BIND_PCI_DID, ICH9_AHCI_DID),       // QEMU
    BI_ABORT(),
    // AMD devices
    BI_LABEL(1),
    BI_MATCH_IF(EQ, BIND_PCI_DID, AMD_FCH_AHCI_DID),
    BI_ABORT(),
MAGENTA_DRIVER_END(_driver_ahci)
