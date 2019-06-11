// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <threads.h>

#include <ddk/io-buffer.h>
#include <ddk/mmio-buffer.h>
#include <zircon/types.h>

#include "sata.h"

namespace ahci {

// port is implemented by the controller
constexpr uint32_t kPortFlagImplemented = (1u << 0);
// a device is present on port
constexpr uint32_t kPortFlagPresent = (1u << 1);
// port is paused (no queued transactions will be processed)
// until pending transactions are done
constexpr uint32_t kPortFlagSyncPaused = (1u << 2);


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

class Controller;

class Port {
public:
    Port();
    ~Port();

    // Configure a port for use.
    zx_status_t Configure(uint32_t num, Controller* con, ahci_port_reg_t* regs);

    void Enable();
    void Disable();
    void Reset();

    void SetDevInfo(const sata_devinfo_t* devinfo);

    zx_status_t Queue(sata_txn_t* txn);

    void Complete();
    void ProcessQueued();

    // Returns true if a transaction was handled.
    bool HandleIrq();
    // Returns true if there are transactions pending.
    bool HandleWatchdog();

    uint32_t num() { return num_; }

    // These flag-access functions should require holding the port lock.
    // In their current use, they frequently access them unlocked. This
    // will be fixed and thread annotations will be added in future CLs.

    bool is_implemented() { return flags_ & kPortFlagImplemented; }

    bool is_present() { return flags_ & kPortFlagPresent; }
    void set_present(bool present) {
        present ? flags_ |= kPortFlagPresent : flags_ &= ~kPortFlagPresent;
    }

    bool is_valid() {
        uint32_t valid_flags = kPortFlagImplemented | kPortFlagPresent;
        return (flags_ & valid_flags) == valid_flags;
    }

    bool is_paused() {
        return (flags_ & kPortFlagSyncPaused);
    }

    // Temporary until InitScan is fixed.
    ahci_port_reg_t* regs() { return regs_; }

private:
    inline uint32_t RegRead(const volatile uint32_t* reg);
    inline void RegWrite(volatile uint32_t* reg, uint32_t val);
    bool SlotBusyLocked(uint32_t slot);
    zx_status_t TxnBeginLocked(uint32_t slot, sata_txn_t* txn);
    void TxnComplete(zx_status_t status);


    uint32_t num_ = 0; // 0-based
    Controller* con_ = nullptr;

    mtx_t lock_;
    uint32_t flags_ = 0;
    list_node_t txn_list_;
    uint32_t running_ = 0;   // bitmask of running commands
    uint32_t completed_ = 0; // bitmask of completed commands
    sata_txn_t* sync_ = nullptr;   // FLUSH command in flight

    io_buffer_t buffer_;
    ahci_port_reg_t* regs_ = nullptr;
    ahci_port_mem_t* mem_ = nullptr;

    sata_devinfo_t devinfo_;
    sata_txn_t* commands_[AHCI_MAX_COMMANDS] = {}; // commands in flight
};

} // namespace ahci
