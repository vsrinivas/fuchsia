// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <lib/sync/completion.h>
#include <lib/zx/handle.h>
#include <lib/zx/time.h>
#include <fbl/unique_ptr.h>
#include <hw/pci.h>
#include <zircon/types.h>

#include "ahci.h"
#include "port.h"

namespace ahci {

class Controller {
public:
    Controller() {}
    ~Controller();

    // Create a new AHCI Controller.
    static zx_status_t Create(zx_device_t* parent, std::unique_ptr<Controller>* con_out);

    // Release call for device protocol. Deletes this Controller.
    static void Release(void* ctx);

    // Read or write a 32-bit AHCI controller reg. Endinaness is corrected.
    inline uint32_t RegRead(const volatile uint32_t* reg) { return pcie_read32(reg); }
    inline void RegWrite(volatile uint32_t* reg, uint32_t val) { pcie_write32(reg, val); }

    // Wait until all bits in |mask| are cleared in |reg| or timeout expires.
    zx_status_t WaitForClear(const volatile uint32_t* reg, uint32_t mask, zx::duration timeout);
    // Wait until one bit in |mask| is set in |reg| or timeout expires.
    zx_status_t WaitForSet(const volatile uint32_t* reg, uint32_t mask, zx::duration timeout);

    static int WorkerThread(void* arg) {
        return static_cast<Controller*>(arg)->WorkerLoop();
    }

    static int WatchdogThread(void* arg) {
        return static_cast<Controller*>(arg)->WatchdogLoop();
    }

    static int IrqThread(void* arg) {
        return static_cast<Controller*>(arg)->IrqLoop();
    }

    static int InitThread(void* arg) {
        return static_cast<Controller*>(arg)->InitScan();
    }

    // Create worker, irq, and watchdog threads.
    zx_status_t LaunchThreads();

    void HbaReset();
    void AhciEnable();

    zx_status_t SetDevInfo(uint32_t portnr, sata_devinfo_t* devinfo);
    void Queue(uint32_t portnr, sata_txn_t* txn);

    void SignalWorker() { sync_completion_signal(&worker_completion_); }
    void SignalWatchdog() { sync_completion_signal(&watchdog_completion_); }

    // Returns true if controller supports Native Command Queuing.
    bool HasCommandQueue() { return cap_ & AHCI_CAP_NCQ; }

    // Returns maximum number of simultaneous commands on each port.
    uint32_t MaxCommands() { return static_cast<uint32_t>((cap_ >> 8) & 0x1f); }

    zx_handle_t bti_handle() { return bti_handle_.get(); }
    zx_device_t** zxdev_ptr() { return &zxdev_; }

private:
    int WorkerLoop();
    int WatchdogLoop();
    int IrqLoop();
    int InitScan();

    zx_device_t* zxdev_ = nullptr;
    ahci_hba_t* regs_ = nullptr;
    mmio_buffer_t mmio_;
    zx::handle bti_handle_;
    zx::handle irq_handle_;
    uint32_t cap_ = 0;

    thrd_t irq_thread_;
    thrd_t worker_thread_;
    thrd_t watchdog_thread_;

    sync_completion_t worker_completion_;
    sync_completion_t watchdog_completion_;

    pci_protocol_t pci_;
    Port ports_[AHCI_MAX_PORTS]{};
};

} // namespace ahci
