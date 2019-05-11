#include "dynlink.h"
#include "relr.h"
#include "libc.h"
#include <zircon/compiler.h>
#include <stdatomic.h>
#include <stddef.h>

__LOCAL dl_start_return_t _dl_start(void* start_arg, void* vdso) {
    ElfW(Addr) base = (uintptr_t)__ehdr_start;
    const ElfW(Rel)* rel = NULL;
    const ElfW(Rela)* rela = NULL;
    const ElfW(Addr)* relr = NULL;
    size_t relcount = 0, relacount = 0, relrsz = 0;

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
        case DT_RELR:
            relr = (const void*)(base + d->d_un.d_ptr);
            break;
        case DT_RELCOUNT:
            relcount = d->d_un.d_val;
            break;
        case DT_RELACOUNT:
            relacount = d->d_un.d_val;
            break;
        case DT_RELRSZ:
            relrsz = d->d_un.d_val;
            break;
        case DT_RELRENT:
            if (d->d_un.d_val != sizeof(relr[0])) {
                __builtin_trap();
            }
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

    apply_relr(base, relr, relrsz);

    // Make sure all the relocations have landed before calling __dls2,
    // which relies on them.
    atomic_signal_fence(memory_order_seq_cst);

    return __dls2(start_arg, vdso);
}
