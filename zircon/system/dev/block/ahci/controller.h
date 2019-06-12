// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <lib/sync/completion.h>
#include <lib/zx/time.h>
#include <fbl/unique_ptr.h>
#include <hw/pci.h>
#include <zircon/types.h>

#include "ahci.h"
#include "bus.h"
#include "port.h"

namespace ahci {

class Controller {
public:
    Controller() {}
    ~Controller();

    DISALLOW_COPY_ASSIGN_AND_MOVE(Controller);

    // Create a new AHCI Controller.
    static zx_status_t Create(zx_device_t* parent, std::unique_ptr<Controller>* con_out);

    // Test function: Create a new Controller with a caller-provided host bus interface.
    static zx_status_t CreateWithBus(zx_device_t* parent, std::unique_ptr<Bus> bus,
                                     std::unique_ptr<Controller>* con_out);

    // Release call for device protocol. Deletes this Controller.
    static void Release(void* ctx);

    // Read or write a 32-bit AHCI controller reg. Endinaness is corrected.
    uint32_t RegRead(const volatile uint32_t* reg);
    zx_status_t RegWrite(volatile uint32_t* reg, uint32_t val);

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

    Bus* bus() { return bus_.get(); }
    zx_device_t** zxdev_ptr() { return &zxdev_; }

private:
    int WorkerLoop();
    int WatchdogLoop();
    int IrqLoop();
    int InitScan();

    zx_device_t* zxdev_ = nullptr;
    ahci_hba_t* regs_ = nullptr;
    uint32_t cap_ = 0;

    thrd_t irq_thread_;
    thrd_t worker_thread_;
    thrd_t watchdog_thread_;

    sync_completion_t worker_completion_;
    sync_completion_t watchdog_completion_;

    std::unique_ptr<Bus> bus_;
    Port ports_[AHCI_MAX_PORTS];
};

} // namespace ahci
