// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/hermetic-compute/hermetic-compute.h>

#include <climits>
#include <cstdarg>
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
    constexpr const char kThreadName[] = "hermetic-compute";

    zx::thread thread;
    zx_status_t status = zx::thread::create(
        process_, kThreadName, sizeof(kThreadName) - 1, 0, &thread);
    if (status == ZX_OK) {
        status = process_.start(thread, entry, sp, std::move(arg1), arg2);
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

void HermeticComputeProcess::Launcher::LoadModule(zx::unowned_vmo vmo,
                                                  zx::unowned_vmo vdso) {
    // First load up the module and vDSO.  This reports back how much
    // stack the module requested via PT_GNU_STACK.p_memsz.
    uintptr_t base;
    status_ = engine_.LoadElf(*vmo, &base, &entry_, &stack_size_);
    if (status_ == ZX_OK && *vdso) {
        status_ = engine_.LoadElf(*vdso, &vdso_base_, nullptr, nullptr);
    }
}

void HermeticComputeProcess::Launcher::LoadStack(size_t nargs, ...) {
    // Bail out early if parameter packing reporting errors.
    if (status_ != ZX_OK) {
        return;
    }

    // Called before LoadModule?
    if (entry_ == 0) {
        status_ = ZX_ERR_BAD_STATE;
        return;
    }

    // Allocate the stacks and TCB.
    auto allocate =
        [&](size_t size, uintptr_t* base, uintptr_t* tos, size_t* tos_pos) {
            zx::vmo vmo;
            if (status_ == ZX_OK && size > 0) {
                uintptr_t addr = 0;
                status_ = engine_.LoadStack(&size, &vmo, &addr);
                if (base) {
                    *base = addr;
                }
                if (tos) {
                    *tos = addr + size;
                }
                if (tos_pos) {
                    *tos_pos = size;
                }
            }
            return vmo;
        };

    // The TCB points to the unsafe stack, which needs no other setup.
    {
        uintptr_t unsafe_sp = 0;
        allocate(stack_size_, nullptr, &unsafe_sp, nullptr);
        uintptr_t stack_guard = 0;
        zx_cprng_draw(&stack_guard, sizeof(stack_guard));
        zx::vmo tcb_vmo = allocate(sizeof(hermetic::Tcb), &tcb_,
                                   nullptr, nullptr);
        hermetic::Tcb tcb(tcb_, stack_guard, unsafe_sp);
        status_ = tcb_vmo.write(&tcb, 0, sizeof(tcb));
    }

    // The machine stack is used for passing the arguments other than the
    // handle.  The shadow call stack pointer (when used) and the vDSO address
    // are passed as implicit arguments before the nargs uintptr_t arguments.
    // Since the stack VMO is all zero to begin with, the engine sees a stack
    // full of uintptr_t{0} arguments no matter how few are actually passed
    // here.
    //
    // We lay the stack out here so that engine-start.S simply pops off all
    // the register arguments and calls a C entry point with the signature
    // (zx_handle_t, uintptr_t vdso, ...) -> void.  (The first argument is
    // already in its register.)

#ifdef __aarch64__
    constexpr bool kShadowCallStack = true;
    constexpr size_t kRegisterArgs = 8; // x18 for SSC, then x1..x7 for args
#elif defined(__x86_64__)
    constexpr bool kShadowCallStack = false;
    constexpr size_t kRegisterArgs = 5; // 2nd..6th ABI argument registers
#else
# error "unsupported architecture"
#endif

    // There will always be an even number of pops to keep the SP aligned.
    constexpr size_t kImplicitPops = kRegisterArgs % 2;

    constexpr size_t kImplicitArgs = (kShadowCallStack ? 1 : 0) + 1; // vDSO

    const size_t arg_space = sizeof(uintptr_t) *
        ((std::max(kImplicitArgs + nargs, kRegisterArgs) + kImplicitPops +
          1) & -size_t{2});

    uintptr_t stack_top = 0;
    size_t stack_top_offset = 0;
    zx::vmo stack_vmo = allocate(std::max(stack_size_, arg_space),
                                 nullptr, &stack_top, &stack_top_offset);

    sp_ = stack_top - arg_space;

    const size_t register_args = stack_top_offset - arg_space;
    const size_t stack_args =
        register_args + ((kRegisterArgs + kImplicitPops) * sizeof(uintptr_t));
    size_t registers_used = 0;
    size_t stack_args_used = 0;
    auto add_argument =
        [&](uintptr_t value) {
            if (status_ != ZX_OK) {
                // Short-circuit.
            } else if (registers_used < kRegisterArgs) {
                status_ = stack_vmo.write(
                    &value,
                    register_args + (registers_used++ * sizeof(uintptr_t)),
                    sizeof(value));
            } else {
                status_ = stack_vmo.write(
                    &value,
                    stack_args + (stack_args_used++ * sizeof(uintptr_t)),
                    sizeof(value));
            }
        };

    if (kShadowCallStack) {
        // The first (register) argument is the shadow call stack pointer.
        // This is popped into a special register and does not affect the
        // signature of the C entry point.
        uintptr_t sc_sp = 0;
        // TODO(mcgrathr): configurability for ssc size?
        if (stack_size_ > 0) {
            allocate(PAGE_SIZE, nullptr, &sc_sp, nullptr);
        }
        add_argument(sc_sp);
    }

    // The vDSO address is always the first argument to the C entry point.
    add_argument(vdso_base_);

    // Remaining arguments (if any) are passed through.  For any up to
    // kRegisterArgs not filled out here, engine-start.S will pop zeros
    // into those argument registers.
    va_list args;
    va_start(args, nargs);
    for (size_t i = 0; i < nargs; ++i) {
        add_argument(va_arg(args, uintptr_t));
    }
    va_end(args);

    ZX_DEBUG_ASSERT(registers_used + stack_args_used == kImplicitArgs + nargs);
}

zx_status_t HermeticComputeProcess::Map(const zx::vmo& vmo,
                                        uint64_t vmo_offset, size_t size,
                                        bool writable, uintptr_t* ptr) {
    return MapWithGuards(
        vmar(), vmo, vmo_offset, size,
        ZX_VM_PERM_READ | (writable ? ZX_VM_PERM_WRITE : 0), ptr);
}
