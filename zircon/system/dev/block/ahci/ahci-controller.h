// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/types.h>

#include "sata.h"

struct ahci_port_t {
    uint32_t nr; // 0-based
    uint32_t flags = 0;

    sata_devinfo_t devinfo;

    ahci_port_reg_t* regs = nullptr;
    ahci_cl_t* cl = nullptr;
    ahci_fis_t* fis = nullptr;
    ahci_ct_t* ct[AHCI_MAX_COMMANDS] = {};

    mtx_t lock;

    list_node_t txn_list;
    io_buffer_t buffer;

    uint32_t running = 0;   // bitmask of running commands
    uint32_t completed = 0; // bitmask of completed commands
    sata_txn_t* commands[AHCI_MAX_COMMANDS] = {}; // commands in flight
    sata_txn_t* sync = nullptr;   // FLUSH command in flight
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

