// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/hermetic-compute/hermetic-compute.h>

#include <climits>
#include <elfload/elfload.h>
#include <lib/zx/thread.h>
#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>
#include <zircon/assert.h>
#include <zircon/status.h>

namespace {

constexpr uintptr_t PageTrunc(uintptr_t addr) {
    return addr & -PAGE_SIZE;
}

constexpr size_t PageRound(size_t size) {
    return (size + PAGE_SIZE - 1) & -PAGE_SIZE;
}

// Use huge guards so everything is far away from everything else.
constexpr size_t kGuardSize = size_t{1} << 30; // 1G
static_assert(kGuardSize % PAGE_SIZE == 0);

// Make space for a module to use up to this much address space.
constexpr size_t kMaxModuleSize = kGuardSize;

zx_status_t MapWithGuards(const zx::vmar& vmar, const zx::vmo& vmo,
                          uint64_t vmo_offset, size_t size,
                          zx_vm_option_t perm, uintptr_t* out_address) {
    // Create a VMAR to contain the mapping and the guard pages around it.
    // Once the VMAR handle goes out of scope, these mappings cannot change
    // (except by unmapping the whole region).
    zx::vmar child_vmar;
    uintptr_t base;
    zx_status_t status = vmar.allocate(
        0, size + (2 * kGuardSize),
        ((perm & ZX_VM_PERM_READ) ? ZX_VM_CAN_MAP_READ : 0) |
        ((perm & ZX_VM_PERM_WRITE) ? ZX_VM_CAN_MAP_WRITE : 0) |
        ((perm & ZX_VM_PERM_EXECUTE) ? ZX_VM_CAN_MAP_EXECUTE : 0) |
        ZX_VM_CAN_MAP_SPECIFIC,
        &child_vmar, &base);
    if (status == ZX_OK) {
        status = child_vmar.map(kGuardSize, vmo, vmo_offset, size,
                                perm | ZX_VM_SPECIFIC, out_address);
    }
    return status;
}

constexpr size_t kMaxPhdrs = 16;

constexpr const char kThreadName[] = "hermetic-compute";

zx_status_t CreateThread(const zx::process& process, zx::thread* out_thread) {
    return zx::thread::create(
        process, kThreadName, sizeof(kThreadName) - 1, 0, out_thread);
}

}  // namespace

zx_status_t HermeticComputeProcess::LoadElf(const zx::vmo& vmo,
                                            uintptr_t* out_base,
                                            uintptr_t* out_entry,
                                            size_t* out_stack_size) {
    elf_load_header_t header;
    uintptr_t phoff;
    zx_status_t status = elf_load_prepare(vmo.get(), nullptr, 0,
                                          &header, &phoff);
    if (status != ZX_OK) {
        return status;
    }

    if (header.e_phnum > kMaxPhdrs) {
        return ERR_ELF_BAD_FORMAT;
    }

    elf_phdr_t phdrs[kMaxPhdrs];
    status = elf_load_read_phdrs(vmo.get(), phdrs, phoff, header.e_phnum);
    if (status != ZX_OK) {
        return status;
    }

    uint32_t max_perm = 0;
    for (uint_fast16_t i = 0; i < header.e_phnum; ++i) {
        switch (phdrs[i].p_type) {
        case PT_GNU_STACK:
            if (out_stack_size) {
                // The module must have a PT_GNU_STACK header indicating
                // how much stack it needs (which can be zero).
                if (phdrs[i].p_filesz != 0 ||
                    phdrs[i].p_flags != (PF_R | PF_W)) {
                    return ERR_ELF_BAD_FORMAT;
                }
                *out_stack_size = phdrs[i].p_memsz;
                out_stack_size = nullptr;
            }
            break;
        case PT_LOAD:
            // The first segment should start at zero (no prelinking here!).
            // elfload checks other aspects of the addresses and sizes.
            if (max_perm == 0 && PageTrunc(phdrs[i].p_vaddr) != 0) {
                return ERR_ELF_BAD_FORMAT;
            }
            max_perm |= phdrs[i].p_flags;
            break;
        }
    }
    if (out_stack_size ||               // Never saw PT_GNU_STACK.
        (max_perm & ~(PF_R | PF_W | PF_X))) {
        return ERR_ELF_BAD_FORMAT;
    }

    // Allocate a very large VMAR to put big guard regions around the module.
    zx::vmar guard_vmar;
    uintptr_t base;
    status = vmar_.allocate(0, kMaxModuleSize + (2 * kGuardSize),
                            ((max_perm & PF_R) ? ZX_VM_CAN_MAP_READ : 0) |
                            ((max_perm & PF_W) ? ZX_VM_CAN_MAP_WRITE : 0) |
                            ((max_perm & PF_X) ? ZX_VM_CAN_MAP_EXECUTE : 0) |
                            ZX_VM_CAN_MAP_SPECIFIC,
                            &guard_vmar, &base);
    if (status != ZX_OK) {
        return status;
    }

    // Now allocate a large VMAR between guard regions inside which the
    // code will go at a random location.
    zx::vmar code_vmar;
    status = guard_vmar.allocate(
        kGuardSize, kMaxModuleSize,
        ((max_perm & PF_R) ? ZX_VM_CAN_MAP_READ : 0) |
        ((max_perm & PF_W) ? ZX_VM_CAN_MAP_WRITE : 0) |
        ((max_perm & PF_X) ? ZX_VM_CAN_MAP_EXECUTE : 0) |
        ZX_VM_SPECIFIC,
        &code_vmar, &base);
    if (status != ZX_OK) {
        return status;
    }

    // It's no longer possible to put other things into the guarded region.
    guard_vmar.reset();

    // Now map the segments inside the code VMAR.  elfload creates another
    // right-sized VMAR to contain the segments.  The location of that VMAR
    // within |code_vmar| is random.  We don't hold onto the inner VMAR
    // handle, so the segment mappings can't be modified.
    return elf_load_map_segments(code_vmar.get(), &header, phdrs, vmo.get(),
                                 nullptr, out_base, out_entry);
}

zx_status_t HermeticComputeProcess::LoadStack(size_t* size,
                                              zx::vmo* out_vmo,
                                              uintptr_t* out_stack_base) {
    *size = PageRound(*size);
    zx_status_t status = zx::vmo::create(*size, 0, out_vmo);
    if (status == ZX_OK) {
        status = MapWithGuards(vmar_, *out_vmo, 0, *size,
                               ZX_VM_PERM_READ | ZX_VM_PERM_WRITE,
                               out_stack_base);
    }
    return status;
}

zx_status_t HermeticComputeProcess::Start(uintptr_t entry, uintptr_t sp,
                                          zx::handle arg1, uintptr_t arg2) {

    zx::thread thread;
    zx_status_t status = CreateThread(process_, &thread);
    if (status == ZX_OK) {
        status = process_.start(thread, entry, sp, std::move(arg1), arg2);
    }
    return status;
}

zx_status_t HermeticComputeProcess::Start(zx::handle handle,
                                          zx::thread* out_thread,
                                          zx::suspend_token* out_token) {
    zx_status_t status = CreateThread(process_, out_thread);
    if (status != ZX_OK) {
        return status;
    }

    status = out_thread->suspend(out_token);
    if (status == ZX_OK) {
        // The initial register values are all zeros (except maybe the handle).
        // They'll be changed before the thread ever runs in user mode.
        status = process_.start(*out_thread, 0, 0, std::move(handle), 0);
    }

    if (status == ZX_OK) {
        // It's started and will immediately suspend itself before ever
        // reaching user mode, but we have to wait to ensure it's actually
        // officially suspended before we can access its user register state.
        zx_signals_t signals;
        status = out_thread->wait_one(
            ZX_THREAD_SUSPENDED | ZX_THREAD_TERMINATED,
            zx::time::infinite(), &signals);
        if (status == ZX_OK) {
            if (signals & ZX_THREAD_TERMINATED) {
                status = ZX_ERR_PEER_CLOSED;
            } else {
                ZX_DEBUG_ASSERT(signals & ZX_THREAD_SUSPENDED);
            }
        }
    }

    if (status != ZX_OK) {
        out_thread->kill();
        out_thread->reset();
        out_token->reset();
    }

    return status;
}

zx_status_t HermeticComputeProcess::Wait(int64_t* result, zx::time deadline) {
    zx_signals_t signals;
    zx_status_t status = process_.wait_one(ZX_PROCESS_TERMINATED,
                                           deadline, &signals);
    if (status == ZX_OK) {
        ZX_DEBUG_ASSERT(signals == ZX_PROCESS_TERMINATED);
        if (result) {
            zx_info_process_t info;
            status = process_.get_info(ZX_INFO_PROCESS,
                                       &info, sizeof(info),
                                       nullptr, nullptr);
            if (status == ZX_OK) {
                ZX_DEBUG_ASSERT(info.exited);
                *result = info.return_code;
            }
        }
    }
    return status;
}

zx_status_t HermeticComputeProcess::Map(const zx::vmo& vmo,
                                        uint64_t vmo_offset, size_t size,
                                        bool writable, uintptr_t* ptr) {
    return MapWithGuards(
        vmar(), vmo, vmo_offset, size,
        ZX_VM_PERM_READ | (writable ? ZX_VM_PERM_WRITE : 0), ptr);
}
