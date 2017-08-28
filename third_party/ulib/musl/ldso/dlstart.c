#include "dynlink.h"
#include "libc.h"
#include <magenta/compiler.h>
#include <stdatomic.h>
#include <stddef.h>

#ifdef __clang__
// TODO(mcgrathr): Really we want to compile just this file without
// -fsanitize-coverage, but this works around the issue for now.
__asm__(".weakref __sanitizer_cov_trace_pc_guard, _dlstart_sancov_dummy");
__asm__(".pushsection .text._dlstart_sancov_dummy,\"ax\",%progbits\n"
        ".local _dlstart_sancov_dummy\n"
        ".type _dlstart_sancov_dummy,%function\n"
        "_dlstart_sancov_dummy: ret\n"
        ".size _dlstart_sancov_dummy, . - _dlstart_sancov_dummy\n"
        ".popsection");
#endif

__LOCAL __NO_SAFESTACK NO_ASAN dl_start_return_t _dl_start(void* start_arg,
                                                           void* vdso) {
    ElfW(Addr) base = (uintptr_t)__ehdr_start;
    const ElfW(Rel)* rel = NULL;
    const ElfW(Rela)* rela = NULL;
    size_t relcount = 0, relacount = 0;

    // We rely on having been linked with -z combreloc so we get
    // the DT_REL(A)COUNT tag and relocs are sorted with all the
    // R_*_RELATIVE cases first.

    for (const ElfW(Dyn)* d = _DYNAMIC; d->d_tag != DT_NULL; ++d) {
        switch (d->d_tag) {
        case DT_REL:
            rel = (const void*)(base + d->d_un.d_ptr);
            break;
        case DT_RELA:
            rela = (const void*)(base + d->d_un.d_ptr);
            break;
        case DT_RELCOUNT:
            relcount = d->d_un.d_val;
            break;
        case DT_RELACOUNT:
            relacount = d->d_un.d_val;
            break;
        }
    }

    for (size_t i = 0; i < relcount; ++i) {
        ElfW(Addr)* addr = (uintptr_t*)(base + rel[i].r_offset);
        // Invariant (no asserts here): R_TYPE(rel[i].r_info) == REL_RELATIVE
        *addr += base;
    }

    for (size_t i = 0; i < relacount; ++i) {
        ElfW(Addr)* addr = (uintptr_t*)(base + rela[i].r_offset);
        // Invariant (no asserts here): R_TYPE(rela[i].r_info) == REL_RELATIVE
        *addr = base + rela[i].r_addend;
    }

    // Make sure all the relocations have landed before calling __dls2,
    // which relies on them.
    atomic_signal_fence(memory_order_seq_cst);

    return __dls2(start_arg, vdso);
}
