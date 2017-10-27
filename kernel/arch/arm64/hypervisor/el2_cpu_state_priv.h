// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <fbl/array.h>
#include <fbl/unique_ptr.h>
#include <hypervisor/id_tracker.h>

class El2TranslationTable {
public:
    El2TranslationTable() = default;
    ~El2TranslationTable();
    DISALLOW_COPY_ASSIGN_AND_MOVE(El2TranslationTable);

    zx_status_t Init();
    zx_paddr_t Base() const;

private:
    zx_paddr_t l0_pa_ = 0;
    zx_paddr_t l1_pa_ = 0;
};

/* Represents a stack for use with EL2. */
class El2Stack {
public:
    El2Stack() = default;
    ~El2Stack();
    DISALLOW_COPY_ASSIGN_AND_MOVE(El2Stack);

    zx_status_t Alloc();
    zx_paddr_t Top() const;

private:
    zx_paddr_t pa_ = 0;
};

/* Maintains the EL2 state for each CPU. */
class El2CpuState : public hypervisor::IdTracker<uint8_t, 64> {
public:
    static zx_status_t Create(fbl::unique_ptr<El2CpuState>* out);
    ~El2CpuState();

private:
    El2TranslationTable table_;
    fbl::Array<El2Stack> stacks_;

    El2CpuState() = default;

    static zx_status_t OnTask(void* context, uint cpu_num);
};

// Allocate and free virtual machine IDs.
zx_status_t alloc_vmid(uint8_t* vmid);
zx_status_t free_vmid(uint8_t vmid);

// Allocate and free virtual processor IDs.
zx_status_t alloc_vpid(uint8_t* vpid);
zx_status_t free_vpid(uint8_t vpid);
