// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/types.h>

#include "ahci.h"
#include "sata.h"

// port is implemented by the controller
#define AHCI_PORT_FLAG_IMPLEMENTED (1u << 0)
// a device is present on port
#define AHCI_PORT_FLAG_PRESENT     (1u << 1)
// port is paused (no queued transactions will be processed)
// until pending transactions are done
#define AHCI_PORT_FLAG_SYNC_PAUSED (1u << 2)


// Command table for a port.
struct ahci_command_tab_t {
    ahci_ct_t ct;
    ahci_prd_t prd[AHCI_MAX_PRDS];
} __attribute__((aligned(128)));

// Memory for port command lists is laid out in the order described by this struct.
struct ahci_port_mem_t {
    ahci_cl_t cl[AHCI_MAX_COMMANDS];            // 1024-byte aligned.
    ahci_fis_t fis;                             // 256-byte aligned.
    ahci_command_tab_t tab[AHCI_MAX_COMMANDS];  // 128-byte aligned.
};

static_assert(sizeof(ahci_port_mem_t) == 271616, "port memory layout size invalid");

struct ahci_port_t {
    uint32_t nr; // 0-based
    uint32_t flags = 0;

    sata_devinfo_t devinfo;

    ahci_port_reg_t* regs = nullptr;
    ahci_port_mem_t* mem = nullptr;

    mtx_t lock;

    list_node_t txn_list;
    io_buffer_t buffer;

    uint32_t running = 0;   // bitmask of running commands
    uint32_t completed = 0; // bitmask of completed commands
    sata_txn_t* commands[AHCI_MAX_COMMANDS] = {}; // commands in flight
    sata_txn_t* sync = nullptr;   // FLUSH command in flight

    bool is_valid() {
        uint32_t valid_flags = AHCI_PORT_FLAG_IMPLEMENTED | AHCI_PORT_FLAG_PRESENT;
        return (flags & valid_flags) == valid_flags;
    }

    bool is_paused() {
        return (flags & AHCI_PORT_FLAG_SYNC_PAUSED) != 0;
    }
};

class AhciController {
public:
    AhciController() {}
    ~AhciController();

    static zx_status_t Create(zx_device_t* parent, std::unique_ptr<AhciController>* con_out);
    static void Release(void* ctx);

    static int WorkerThread(void* arg) {
        return static_cast<AhciController*>(arg)->WorkerLoop();
    }

    static int WatchdogThread(void* arg) {
        return static_cast<AhciController*>(arg)->WatchdogLoop();
    }

    static int IrqThread(void* arg) {
        return static_cast<AhciController*>(arg)->IrqLoop();
    }

    static int InitThread(void* arg) {
        return static_cast<AhciController*>(arg)->InitScan();
    }

    zx_status_t LaunchThreads();

    bool PortValid(uint32_t portnum);
    zx_status_t PortInit(ahci_port_t* port);

    void PortIrq(uint32_t nr);

    void HbaReset();
    void AhciEnable();

    void SetDevInfo(uint32_t portnr, sata_devinfo_t* devinfo);
    void Queue(uint32_t portnr, sata_txn_t* txn);

    zx_status_t TxnBegin(ahci_port_t* port, uint32_t slot, sata_txn_t* txn);
    void TxnComplete(ahci_port_t* port, zx_status_t status);

    zx_device_t** zxdev_ptr() { return &zxdev_; }

private:
    int WorkerLoop();
    void PortComplete(ahci_port_t* port);
    void PortProcessQueued(ahci_port_t* port);

    int WatchdogLoop();
    int IrqLoop();
    int InitScan();

    zx_device_t* zxdev_ = nullptr;
    ahci_hba_t* regs_ = nullptr;
    mmio_buffer_t mmio_;
    zx_handle_t bti_handle_ = ZX_HANDLE_INVALID;
    zx_handle_t irq_handle_ = ZX_HANDLE_INVALID;
    uint32_t cap_ = 0;

    thrd_t irq_thread_;
    thrd_t worker_thread_;
    thrd_t watchdog_thread_;

    sync_completion_t worker_completion_;
    sync_completion_t watchdog_completion_;

    pci_protocol_t pci_;

    // TODO(ZX-1641): lazily allocate these
    ahci_port_t ports_[AHCI_MAX_PORTS]{};

};

