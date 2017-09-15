// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <fbl/array.h>
#include <fbl/unique_ptr.h>
#include <hypervisor/cpu_state.h>

/* Represents a stack for use with EL2. */
class El2Stack {
public:
    El2Stack() = default;
    ~El2Stack();
    DISALLOW_COPY_ASSIGN_AND_MOVE(El2Stack);

    zx_status_t Alloc();
    zx_paddr_t Top() const;

private:
    zx_paddr_t stack_paddr_ = 0;
};

/* Maintains the EL2 state for each CPU. */
class El2CpuState : public hypervisor::CpuState<uint8_t, 64> {
public:
    static zx_status_t Create(fbl::unique_ptr<El2CpuState>* out);
    ~El2CpuState();

private:
    fbl::Array<El2Stack> stacks_;

    El2CpuState() = default;
};

zx_status_t alloc_vmid(uint8_t* vmid);
zx_status_t free_vmid(uint8_t vmid);
