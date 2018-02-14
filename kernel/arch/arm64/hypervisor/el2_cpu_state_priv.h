// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <fbl/array.h>
#include <fbl/unique_ptr.h>
#include <hypervisor/id_allocator.h>
#include <hypervisor/page.h>

class El2TranslationTable {
public:
    zx_status_t Init();
    zx_paddr_t Base() const;

private:
    hypervisor::Page l0_page_;
    hypervisor::Page l1_page_;
};

// Represents a stack for use with EL2/
class El2Stack {
public:
    zx_status_t Alloc();
    zx_paddr_t Top() const;

private:
    hypervisor::Page page_;
};

// Maintains the EL2 state for each CPU.
class El2CpuState : public hypervisor::IdAllocator<uint8_t, 64> {
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
