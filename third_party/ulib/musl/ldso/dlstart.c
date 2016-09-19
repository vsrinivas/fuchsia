#include "dynlink.h"
#include <stddef.h>

#define SHARED

#ifndef GETFUNCSYM
#define GETFUNCSYM(fp, sym, got)                                          \
    do {                                                                  \
        extern stage2_func sym __attribute__((__visibility__("hidden"))); \
        static stage2_func* static_func_ptr = sym;                        \
        __asm__ __volatile__(""                                           \
                             : "+m"(static_func_ptr)                      \
                             :                                            \
                             : "memory");                                 \
        *(fp) = static_func_ptr;                                          \
    } while (0)
#endif

__attribute__((__visibility__("hidden"))) dl_start_return_t _dl_start(
    void* start_arg, void* vdso) {
    size_t base = (size_t)_BASE;
    size_t* dynv = _DYNAMIC;

    size_t i, dyn[DYN_CNT];
    size_t *rel, rel_size;

    for (i = 0; i < DYN_CNT; i++)
        dyn[i] = 0;
    for (i = 0; dynv[i]; i += 2)
        if (dynv[i] < DYN_CNT)
            dyn[dynv[i]] = dynv[i + 1];

    /* MIPS uses an ugly packed form for GOT relocations. Since we
     * can't make function calls yet and the code is tiny anyway,
     * it's simply inlined here. */
    if (NEED_MIPS_GOT_RELOCS) {
        size_t local_cnt = 0;
        size_t* got = (void*)(base + dyn[DT_PLTGOT]);
        for (i = 0; dynv[i]; i += 2)
            if (dynv[i] == DT_MIPS_LOCAL_GOTNO)
                local_cnt = dynv[i + 1];
        for (i = 0; i < local_cnt; i++)
            got[i] += base;
    }

    rel = (void*)(base + dyn[DT_REL]);
    rel_size = dyn[DT_RELSZ];
    for (; rel_size; rel += 2, rel_size -= 2 * sizeof(size_t)) {
        if (!IS_RELATIVE(rel[1], 0))
            continue;
        size_t* rel_addr = (void*)(base + rel[0]);
        *rel_addr += base;
    }

    rel = (void*)(base + dyn[DT_RELA]);
    rel_size = dyn[DT_RELASZ];
    for (; rel_size; rel += 3, rel_size -= 3 * sizeof(size_t)) {
        if (!IS_RELATIVE(rel[1], 0))
            continue;
        size_t* rel_addr = (void*)(base + rel[0]);
        *rel_addr = base + rel[2];
    }

    stage2_func* dls2;
    GETFUNCSYM(&dls2, __dls2, base + dyn[DT_PLTGOT]);
    return dls2(start_arg, vdso);
}

// This defines _start to call _dl_start and then jump to the entry point.
DL_START_ASM
