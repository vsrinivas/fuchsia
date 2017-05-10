#include "dynlink.h"
#include <magenta/compiler.h>
#include <stdatomic.h>
#include <stddef.h>

#define SHARED

__NO_SAFESTACK __attribute__((__visibility__("hidden")))
dl_start_return_t _dl_start(
    void* start_arg, void* vdso) {
    size_t base = (size_t)__ehdr_start;
    ElfW(Dyn)* dynv = _DYNAMIC;

    size_t i, dyn[DYN_CNT];
    size_t *rel, rel_size;

    for (i = 0; i < DYN_CNT; i++)
        dyn[i] = 0;
    for (i = 0; dynv[i].d_tag; i++)
        if (dynv[i].d_tag < DYN_CNT)
            dyn[dynv[i].d_tag] = dynv[i].d_un.d_val;

    /* MIPS uses an ugly packed form for GOT relocations. Since we
     * can't make function calls yet and the code is tiny anyway,
     * it's simply inlined here. */
    if (NEED_MIPS_GOT_RELOCS) {
        size_t local_cnt = 0;
        size_t* got = (void*)(base + dyn[DT_PLTGOT]);
        for (i = 0; dynv[i].d_tag; i++)
            if (dynv[i].d_tag == DT_MIPS_LOCAL_GOTNO)
                local_cnt = dynv[i].d_un.d_val;
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

    // Make sure all the relocations have landed before calling __dls2,
    // which relies on them.
    atomic_signal_fence(memory_order_seq_cst);

    return __dls2(start_arg, vdso);
}
