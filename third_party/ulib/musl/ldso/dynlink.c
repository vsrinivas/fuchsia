#define _GNU_SOURCE
#include "dynlink.h"
#include "libc.h"
#include "magenta_impl.h"
#include "pthread_impl.h"
#include "stdio_impl.h"
#include <ctype.h>
#include <dlfcn.h>
#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <link.h>
#include <magenta/dlfcn.h>
#include <magenta/process.h>
#include <magenta/status.h>
#include <pthread.h>
#include <setjmp.h>
#include <stdalign.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <inttypes.h>

#include <runtime/message.h>
#include <runtime/processargs.h>
#include <runtime/thread.h>

static void error(const char*, ...);
static void debugmsg(const char*, ...);
static mx_status_t get_library_vmo(const char* name, mx_handle_t* vmo);

#define MAXP2(a, b) (-(-(a) & -(b)))
#define ALIGN(x, y) ((x) + (y)-1 & -(y))

// This matches struct r_debug in <link.h>.
// TODO(mcgrathr): Use the type here.
struct debug {
    int ver;
    void* head;
    void (*bp)(void);
    int state;
    void* base;
};

struct dso {
    // These five fields match struct link_map in <link.h>.
    // TODO(mcgrathr): Use the type here.
    unsigned char* base;
    char* name;
    ElfW(Dyn)* dynv;
    struct dso *next, *prev;

    const char* soname;
    Phdr* phdr;
    int phnum;
    size_t phentsize;
    int refcnt;
    mx_handle_t vmar; // Closed after relocation.
    Sym* syms;
    uint32_t* hashtab;
    uint32_t* ghashtab;
    int16_t* versym;
    char* strings;
    unsigned char* map;
    size_t map_len;
    signed char global;
    char relocated;
    char constructed;
    struct dso **deps, *needed_by;
    struct tls_module tls;
    size_t tls_id;
    size_t relro_start, relro_end;
    void** new_dtv;
    unsigned char* new_tls;
    atomic_int new_dtv_idx, new_tls_idx;
    struct dso* fini_next;
    struct funcdesc {
        void* addr;
        size_t* got;
    } * funcdescs;
    size_t* got;
    struct dso* buf[];
};

struct symdef {
    Sym* sym;
    struct dso* dso;
};

#define MIN_TLS_ALIGN alignof(struct pthread)

#define ADDEND_LIMIT 4096
static size_t *saved_addends, *apply_addends_to;

static struct dso ldso, vdso;
static struct dso *head, *tail, *fini_head;
static unsigned long long gencnt;
static int runtime;
static int ldd_mode;
static int ldso_fail;
static jmp_buf* rtld_fail;
static pthread_rwlock_t lock;
static struct debug debug;
static struct tls_module* tls_tail;
static size_t tls_cnt, tls_offset, tls_align = MIN_TLS_ALIGN;
static size_t static_tls_cnt;
static pthread_mutex_t init_fini_lock = {._m_type = PTHREAD_MUTEX_RECURSIVE};

static mx_handle_t loader_svc = MX_HANDLE_INVALID;
static mx_handle_t logger = MX_HANDLE_INVALID;

// Various tools use this value to bootstrap their knowledge of the process.
// E.g., the list of loaded shared libraries is obtained from here.
// The value is stored in the process's MX_PROPERTY_PROCESS_DEBUG_ADDR so that
// tools can obtain the value when aslr is enabled.
struct debug* _dl_debug_addr = &debug;

// If true then dump load map data in a specific format for tracing.
// This is used by Intel PT (Processor Trace) support for example when
// post-processing the h/w trace.
static bool trace_maps = false;

__attribute__((__visibility__("hidden"))) void (*const __init_array_start)(void) = 0,
                                                       (*const __fini_array_start)(void) = 0;

__attribute__((__visibility__("hidden"))) extern void (*const __init_array_end)(void),
    (*const __fini_array_end)(void);

weak_alias(__init_array_start, __init_array_end);
weak_alias(__fini_array_start, __fini_array_end);

static int dl_strcmp(const char* l, const char* r) {
    for (; *l == *r && *l; l++, r++)
        ;
    return *(unsigned char*)l - *(unsigned char*)r;
}
#define strcmp(l, r) dl_strcmp(l, r)


// Simple bump allocator for dynamic linker internal data structures.
// This allocator is single-threaded: it can be used only at startup or
// while holding the big lock.  These allocations can never be freed
// once in use.  But it does support a simple checkpoint and rollback
// mechanism to undo all allocations since the checkpoint, used for the
// abortive dlopen case.

union allocated_types {
    struct dso dso;
    size_t tlsdesc[2];
};
#define DL_ALLOC_ALIGN alignof(union allocated_types)

static uintptr_t alloc_base, alloc_limit, alloc_ptr;

__NO_SAFESTACK __attribute__((malloc)) static void* dl_alloc(size_t size) {
    // Round the size up so the allocation pointer always stays aligned.
    size = (size + DL_ALLOC_ALIGN - 1) & -DL_ALLOC_ALIGN;

    // Get more pages if needed.  The remaining partial page, if any,
    // is wasted unless the system happens to give us the adjacent page.
    if (alloc_limit - alloc_ptr < size) {
        size_t chunk_size = (size + PAGE_SIZE - 1) & -PAGE_SIZE;
        mx_handle_t vmo;
        mx_status_t status = _mx_vmo_create(chunk_size, 0, &vmo);
        if (status != NO_ERROR)
            return NULL;
        uintptr_t chunk;
        status = _mx_vmar_map(_mx_vmar_root_self(), 0, vmo, 0, chunk_size,
                              MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE,
                              &chunk);
        _mx_handle_close(vmo);
        if (status != NO_ERROR)
            return NULL;
        if (chunk != alloc_limit)
            alloc_ptr = alloc_base = chunk;
        alloc_limit = chunk + chunk_size;
    }

    void* block = (void*)alloc_ptr;
    alloc_ptr += size;

    return block;
}

struct dl_alloc_checkpoint {
    uintptr_t ptr, base;
};

__NO_SAFESTACK
static void dl_alloc_checkpoint(struct dl_alloc_checkpoint *state) {
    state->ptr = alloc_ptr;
    state->base = alloc_base;
}

__NO_SAFESTACK
static void dl_alloc_rollback(const struct dl_alloc_checkpoint *state) {
    uintptr_t frontier = alloc_ptr;
    // If we're still using the same contiguous chunk as the checkpoint
    // state, we can just restore the old state directly and waste nothing.
    // If we've allocated new chunks since then, the best we can do is
    // reset to the beginning of the current chunk, since we haven't kept
    // track of the past chunks.
    alloc_ptr = alloc_base == state->base ? state->ptr : alloc_base;
    memset((void*)alloc_ptr, 0, frontier - alloc_ptr);
}


/* Compute load address for a virtual address in a given dso. */
#define laddr(p, v) (void*)((p)->base + (v))
#define fpaddr(p, v) ((void (*)(void))laddr(p, v))

__NO_SAFESTACK static void decode_vec(ElfW(Dyn)* v, size_t* a, size_t cnt) {
    size_t i;
    for (i = 0; i < cnt; i++)
        a[i] = 0;
    for (; v->d_tag; v++)
        if (v->d_tag - 1 < cnt - 1) {
            a[0] |= 1UL << v->d_tag;
            a[v->d_tag] = v->d_un.d_val;
        }
}

__NO_SAFESTACK static int search_vec(ElfW(Dyn)* v, size_t* r, size_t key) {
    for (; v->d_tag != key; v++)
        if (!v->d_tag)
            return 0;
    *r = v->d_un.d_val;
    return 1;
}

__NO_SAFESTACK static uint32_t sysv_hash(const char* s0) {
    const unsigned char* s = (void*)s0;
    uint_fast32_t h = 0;
    while (*s) {
        h = 16 * h + *s++;
        h ^= h >> 24 & 0xf0;
    }
    return h & 0xfffffff;
}

__NO_SAFESTACK static uint32_t gnu_hash(const char* s0) {
    const unsigned char* s = (void*)s0;
    uint_fast32_t h = 5381;
    for (; *s; s++)
        h += h * 32 + *s;
    return h;
}

__NO_SAFESTACK static Sym* sysv_lookup(const char* s, uint32_t h,
                                       struct dso* dso) {
    size_t i;
    Sym* syms = dso->syms;
    uint32_t* hashtab = dso->hashtab;
    char* strings = dso->strings;
    for (i = hashtab[2 + h % hashtab[0]]; i; i = hashtab[2 + hashtab[0] + i]) {
        if ((!dso->versym || dso->versym[i] >= 0) && (!strcmp(s, strings + syms[i].st_name)))
            return syms + i;
    }
    return 0;
}

__NO_SAFESTACK static Sym* gnu_lookup(uint32_t h1, uint32_t* hashtab,
                                      struct dso* dso, const char* s) {
    uint32_t nbuckets = hashtab[0];
    uint32_t* buckets = hashtab + 4 + hashtab[2] * (sizeof(size_t) / 4);
    uint32_t i = buckets[h1 % nbuckets];

    if (!i)
        return 0;

    uint32_t* hashval = buckets + nbuckets + (i - hashtab[1]);

    for (h1 |= 1;; i++) {
        uint32_t h2 = *hashval++;
        if ((h1 == (h2 | 1)) && (!dso->versym || dso->versym[i] >= 0) &&
            !strcmp(s, dso->strings + dso->syms[i].st_name))
            return dso->syms + i;
        if (h2 & 1)
            break;
    }

    return 0;
}

__NO_SAFESTACK static Sym* gnu_lookup_filtered(uint32_t h1, uint32_t* hashtab,
                                               struct dso* dso, const char* s,
                                               uint32_t fofs, size_t fmask) {
    const size_t* bloomwords = (const void*)(hashtab + 4);
    size_t f = bloomwords[fofs & (hashtab[2] - 1)];
    if (!(f & fmask))
        return 0;

    f >>= (h1 >> hashtab[3]) % (8 * sizeof f);
    if (!(f & 1))
        return 0;

    return gnu_lookup(h1, hashtab, dso, s);
}

#define OK_TYPES \
    (1 << STT_NOTYPE | 1 << STT_OBJECT | 1 << STT_FUNC | 1 << STT_COMMON | 1 << STT_TLS)
#define OK_BINDS (1 << STB_GLOBAL | 1 << STB_WEAK | 1 << STB_GNU_UNIQUE)

#ifndef ARCH_SYM_REJECT_UND
#define ARCH_SYM_REJECT_UND(s) 0
#endif

__NO_SAFESTACK static struct symdef find_sym(struct dso* dso,
                                             const char* s, int need_def) {
    uint32_t h = 0, gh, gho, *ght;
    size_t ghm = 0;
    struct symdef def = {};
    for (; dso; dso = dso->next) {
        Sym* sym;
        if (!dso->global)
            continue;
        if ((ght = dso->ghashtab)) {
            if (!ghm) {
                gh = gnu_hash(s);
                int maskbits = 8 * sizeof ghm;
                gho = gh / maskbits;
                ghm = 1ul << gh % maskbits;
            }
            sym = gnu_lookup_filtered(gh, ght, dso, s, gho, ghm);
        } else {
            if (!h)
                h = sysv_hash(s);
            sym = sysv_lookup(s, h, dso);
        }
        if (!sym)
            continue;
        if (!sym->st_shndx)
            if (need_def || (sym->st_info & 0xf) == STT_TLS || ARCH_SYM_REJECT_UND(sym))
                continue;
        if (!sym->st_value)
            if ((sym->st_info & 0xf) != STT_TLS)
                continue;
        if (!(1 << (sym->st_info & 0xf) & OK_TYPES))
            continue;
        if (!(1 << (sym->st_info >> 4) & OK_BINDS))
            continue;

        if (def.sym && sym->st_info >> 4 == STB_WEAK)
            continue;
        def.sym = sym;
        def.dso = dso;
        if (sym->st_info >> 4 == STB_GLOBAL)
            break;
    }
    return def;
}

__attribute__((__visibility__("hidden"))) ptrdiff_t __tlsdesc_static(void), __tlsdesc_dynamic(void);

__NO_SAFESTACK static void do_relocs(struct dso* dso, size_t* rel,
                                     size_t rel_size, size_t stride) {
    unsigned char* base = dso->base;
    Sym* syms = dso->syms;
    char* strings = dso->strings;
    Sym* sym;
    const char* name;
    void* ctx;
    int type;
    int sym_index;
    struct symdef def;
    size_t* reloc_addr;
    size_t sym_val;
    size_t tls_val;
    size_t addend;
    int skip_relative = 0, reuse_addends = 0, save_slot = 0;

    if (dso == &ldso) {
        /* Only ldso's REL table needs addend saving/reuse. */
        if (rel == apply_addends_to)
            reuse_addends = 1;
        skip_relative = 1;
    }

    for (; rel_size; rel += stride, rel_size -= stride * sizeof(size_t)) {
        if (skip_relative && IS_RELATIVE(rel[1], dso->syms))
            continue;
        type = R_TYPE(rel[1]);
        if (type == REL_NONE)
            continue;
        sym_index = R_SYM(rel[1]);
        reloc_addr = laddr(dso, rel[0]);
        if (sym_index) {
            sym = syms + sym_index;
            name = strings + sym->st_name;
            ctx = type == REL_COPY ? head->next : head;
            def = (sym->st_info & 0xf) == STT_SECTION ? (struct symdef){.dso = dso, .sym = sym}
                                                      : find_sym(ctx, name, type == REL_PLT);
            if (!def.sym && (sym->st_shndx != SHN_UNDEF || sym->st_info >> 4 != STB_WEAK)) {
                error("Error relocating %s: %s: symbol not found", dso->name, name);
                if (runtime)
                    longjmp(*rtld_fail, 1);
                continue;
            }
        } else {
            sym = 0;
            def.sym = 0;
            def.dso = dso;
        }

        if (stride > 2) {
            addend = rel[2];
        } else if (type == REL_GOT || type == REL_PLT || type == REL_COPY) {
            addend = 0;
        } else if (reuse_addends) {
            /* Save original addend in stage 2 where the dso
             * chain consists of just ldso; otherwise read back
             * saved addend since the inline one was clobbered. */
            if (head == &ldso)
                saved_addends[save_slot] = *reloc_addr;
            addend = saved_addends[save_slot++];
        } else {
            addend = *reloc_addr;
        }

        sym_val = def.sym ? (size_t)laddr(def.dso, def.sym->st_value) : 0;
        tls_val = def.sym ? def.sym->st_value : 0;

        switch (type) {
        case REL_NONE:
            break;
        case REL_OFFSET:
            addend -= (size_t)reloc_addr;
        case REL_SYMBOLIC:
        case REL_GOT:
        case REL_PLT:
            *reloc_addr = sym_val + addend;
            break;
        case REL_RELATIVE:
            *reloc_addr = (size_t)base + addend;
            break;
        case REL_SYM_OR_REL:
            if (sym)
                *reloc_addr = sym_val + addend;
            else
                *reloc_addr = (size_t)base + addend;
            break;
        case REL_COPY:
            memcpy(reloc_addr, (void*)sym_val, sym->st_size);
            break;
        case REL_OFFSET32:
            *(uint32_t*)reloc_addr = sym_val + addend - (size_t)reloc_addr;
            break;
        case REL_FUNCDESC:
            *reloc_addr = def.sym ? (size_t)(def.dso->funcdescs + (def.sym - def.dso->syms)) : 0;
            break;
        case REL_FUNCDESC_VAL:
            if ((sym->st_info & 0xf) == STT_SECTION)
                *reloc_addr += sym_val;
            else
                *reloc_addr = sym_val;
            reloc_addr[1] = def.sym ? (size_t)def.dso->got : 0;
            break;
        case REL_DTPMOD:
            *reloc_addr = def.dso->tls_id;
            break;
        case REL_DTPOFF:
            *reloc_addr = tls_val + addend - DTP_OFFSET;
            break;
#ifdef TLS_ABOVE_TP
        case REL_TPOFF:
            *reloc_addr = tls_val + def.dso->tls.offset + TPOFF_K + addend;
            break;
#else
        case REL_TPOFF:
            *reloc_addr = tls_val - def.dso->tls.offset + addend;
            break;
        case REL_TPOFF_NEG:
            *reloc_addr = def.dso->tls.offset - tls_val + addend;
            break;
#endif
        case REL_TLSDESC:
            if (stride < 3)
                addend = reloc_addr[1];
            if (runtime && def.dso->tls_id >= static_tls_cnt) {
                size_t* new = dl_alloc(2 * sizeof(size_t));
                if (!new) {
                    error("Error relocating %s: cannot allocate TLSDESC for %s", dso->name,
                          sym ? name : "(local)");
                    longjmp(*rtld_fail, 1);
                }
                new[0] = def.dso->tls_id;
                new[1] = tls_val + addend;
                reloc_addr[0] = (size_t)__tlsdesc_dynamic;
                reloc_addr[1] = (size_t) new;
            } else {
                reloc_addr[0] = (size_t)__tlsdesc_static;
#ifdef TLS_ABOVE_TP
                reloc_addr[1] = tls_val + def.dso->tls.offset + TPOFF_K + addend;
#else
                reloc_addr[1] = tls_val - def.dso->tls.offset + addend;
#endif
            }
            break;
        default:
            error("Error relocating %s: unsupported relocation type %d", dso->name, type);
            if (runtime)
                longjmp(*rtld_fail, 1);
            continue;
        }
    }
}

__NO_SAFESTACK static void unmap_library(struct dso* dso) {
    if (dso->map && dso->map_len) {
        munmap(dso->map, dso->map_len);
    }
    if (dso->vmar != MX_HANDLE_INVALID) {
        _mx_vmar_destroy(dso->vmar);
        _mx_handle_close(dso->vmar);
        dso->vmar = MX_HANDLE_INVALID;
    }
}

__NO_SAFESTACK static mx_status_t map_library(mx_handle_t vmo,
                                              struct dso* dso) {
    struct {
        Ehdr ehdr;
        // A typical ELF file has 7 or 8 phdrs, so in practice
        // this is always enough.  Life is simpler if there is no
        // need for dynamic allocation here.
        Phdr phdrs[16];
    } buf;
    size_t phsize;
    size_t addr_min = SIZE_MAX, addr_max = 0, map_len;
    size_t this_min, this_max;
    size_t nsegs = 0;
    const Ehdr* const eh = &buf.ehdr;
    Phdr *ph, *ph0;
    unsigned char *map = MAP_FAILED, *base;
    size_t dyn = 0;
    size_t tls_image = 0;
    size_t i;

    size_t l;
    mx_status_t status = _mx_vmo_read(vmo, &buf, 0, sizeof(buf), &l);
    if (status != NO_ERROR)
        return status;
    // We cannot support ET_EXEC in the general case, because its fixed
    // addresses might conflict with where the dynamic linker has already
    // been loaded.  It's also policy in Fuchsia that all executables are
    // PIEs to maximize ASLR security benefits.  So don't even try to
    // handle loading ET_EXEC.
    if (l < sizeof *eh || eh->e_type != ET_DYN)
        goto noexec;
    phsize = eh->e_phentsize * eh->e_phnum;
    if (phsize > sizeof(buf.phdrs))
        goto noexec;
    if (eh->e_phoff + phsize > l) {
        status = _mx_vmo_read(vmo, buf.phdrs, eh->e_phoff, phsize, &l);
        if (status != NO_ERROR)
            goto error;
        if (l != phsize)
            goto noexec;
        ph = ph0 = buf.phdrs;
    } else {
        ph = ph0 = (void*)((char*)&buf + eh->e_phoff);
    }
    for (i = eh->e_phnum; i; i--, ph = (void*)((char*)ph + eh->e_phentsize)) {
        if (ph->p_type == PT_DYNAMIC) {
            dyn = ph->p_vaddr;
        } else if (ph->p_type == PT_TLS) {
            tls_image = ph->p_vaddr;
            dso->tls.align = ph->p_align;
            dso->tls.len = ph->p_filesz;
            dso->tls.size = ph->p_memsz;
        } else if (ph->p_type == PT_GNU_RELRO) {
            dso->relro_start = ph->p_vaddr & -PAGE_SIZE;
            dso->relro_end = (ph->p_vaddr + ph->p_memsz) & -PAGE_SIZE;
        }
        if (ph->p_type != PT_LOAD)
            continue;
        nsegs++;
        if (ph->p_vaddr < addr_min) {
            addr_min = ph->p_vaddr;
        }
        if (ph->p_vaddr + ph->p_memsz > addr_max) {
            addr_max = ph->p_vaddr + ph->p_memsz;
        }
    }
    if (!dyn)
        goto noexec;
    addr_max += PAGE_SIZE - 1;
    addr_max &= -PAGE_SIZE;
    addr_min &= -PAGE_SIZE;
    map_len = addr_max - addr_min;

    // Allocate a VMAR to reserve the whole address range.  Stash
    // the new VMAR's handle until relocation has finished, because
    // we need it to adjust page protections for RELRO.
    uintptr_t vmar_base;
    status = _mx_vmar_allocate(__magenta_vmar_root_self, 0, map_len,
                               MX_VM_FLAG_CAN_MAP_READ |
                                   MX_VM_FLAG_CAN_MAP_WRITE |
                                   MX_VM_FLAG_CAN_MAP_EXECUTE |
                                   MX_VM_FLAG_CAN_MAP_SPECIFIC,
                               &dso->vmar, &vmar_base);
    if (status != NO_ERROR) {
        error("failed to reserve %zu bytes of address space: %d\n",
              map_len, status);
        goto error;
    }

    dso->map = map = (void*)vmar_base;
    dso->map_len = map_len;
    base = map - addr_min;
    dso->phdr = 0;
    dso->phnum = 0;
    for (ph = ph0, i = eh->e_phnum; i; i--, ph = (void*)((char*)ph + eh->e_phentsize)) {
        if (ph->p_type != PT_LOAD)
            continue;
        /* Check if the programs headers are in this load segment, and
         * if so, record the address for use by dl_iterate_phdr. */
        if (!dso->phdr && eh->e_phoff >= ph->p_offset &&
            eh->e_phoff + phsize <= ph->p_offset + ph->p_filesz) {
            dso->phdr = (void*)(base + ph->p_vaddr + (eh->e_phoff - ph->p_offset));
            dso->phnum = eh->e_phnum;
            dso->phentsize = eh->e_phentsize;
        }
        this_min = ph->p_vaddr & -PAGE_SIZE;
        this_max = ph->p_vaddr + ph->p_memsz + PAGE_SIZE - 1 & -PAGE_SIZE;
        size_t off_start = ph->p_offset & -PAGE_SIZE;
        uint32_t mx_flags = MX_VM_FLAG_SPECIFIC;
        mx_flags |= (ph->p_flags & PF_R) ? MX_VM_FLAG_PERM_READ : 0;
        mx_flags |= (ph->p_flags & PF_W) ? MX_VM_FLAG_PERM_WRITE : 0;
        mx_flags |= (ph->p_flags & PF_X) ? MX_VM_FLAG_PERM_EXECUTE : 0;
        uintptr_t mapaddr = (uintptr_t)base + this_min;
        mx_handle_t map_vmo = vmo;
        size_t map_size = this_max - this_min;
        if (map_size == 0)
            continue;

        if (ph->p_flags & PF_W) {
            // TODO(mcgrathr,MG-698): When MG-698 is fixed, we can clone to
            // a size that's not whole pages, and then extending it with
            // set_size will do the partial-page zeroing for us implicitly.
            size_t data_size =
                ((ph->p_vaddr + ph->p_filesz + PAGE_SIZE - 1) & -PAGE_SIZE) -
                this_min;
            if (data_size == 0) {
                // This segment is purely zero-fill.
                status = _mx_vmo_create(map_size, 0, &map_vmo);
            } else {
                // Get a writable (lazy) copy of the portion of the file VMO.
                status = _mx_vmo_clone(vmo, MX_VMO_CLONE_COPY_ON_WRITE,
                                       off_start, data_size, &map_vmo);
                if (status == NO_ERROR && map_size > data_size) {
                    // Extend the writable VMO to cover the .bss pages too.
                    // These pages will be zero-filled, not copied from the
                    // file VMO.
                    status = _mx_vmo_set_size(map_vmo, map_size);
                    if (status != NO_ERROR) {
                        _mx_handle_close(map_vmo);
                        goto error;
                    }
                }
            }
            if (status != NO_ERROR)
                goto error;
            off_start = 0;
        } else if (ph->p_memsz > ph->p_filesz) {
            // Read-only .bss is not a thing.
            goto noexec;
        }

        status = _mx_vmar_map(dso->vmar, mapaddr - vmar_base, map_vmo,
                              off_start, map_size, mx_flags, &mapaddr);
        if (map_vmo != vmo)
            _mx_handle_close(map_vmo);
        if (status != NO_ERROR)
            goto error;

        if (ph->p_memsz > ph->p_filesz) {
            // The final partial page of data from the file is followed by
            // whatever the file's contents there are, but in the memory
            // image that partial page should be all zero.
            uintptr_t file_end = (uintptr_t)base + ph->p_vaddr + ph->p_filesz;
            uintptr_t map_end = mapaddr + map_size;
            if (map_end > file_end)
                memset((void*)file_end, 0, map_end - file_end);
        }
    }

    dso->base = base;
    dso->dynv = laddr(dso, dyn);
    if (dso->tls.size)
        dso->tls.image = laddr(dso, tls_image);
    return NO_ERROR;
noexec:
    // We overload this to translate into ENOEXEC later.
    status = ERR_WRONG_TYPE;
error:
    if (map != MAP_FAILED)
        unmap_library(dso);
    if (dso->vmar != MX_HANDLE_INVALID)
        _mx_handle_close(dso->vmar);
    return status;
}

__NO_SAFESTACK static void decode_dyn(struct dso* p) {
    size_t dyn[DYN_CNT];
    decode_vec(p->dynv, dyn, DYN_CNT);
    p->syms = laddr(p, dyn[DT_SYMTAB]);
    p->strings = laddr(p, dyn[DT_STRTAB]);
    if (dyn[0] & (1 << DT_SONAME))
        p->soname = p->strings + dyn[DT_SONAME];
    if (dyn[0] & (1 << DT_HASH))
        p->hashtab = laddr(p, dyn[DT_HASH]);
    if (dyn[0] & (1 << DT_PLTGOT))
        p->got = laddr(p, dyn[DT_PLTGOT]);
    if (search_vec(p->dynv, dyn, DT_GNU_HASH))
        p->ghashtab = laddr(p, *dyn);
    if (search_vec(p->dynv, dyn, DT_VERSYM))
        p->versym = laddr(p, *dyn);
}

static size_t count_syms(struct dso* p) {
    if (p->hashtab)
        return p->hashtab[1];

    size_t nsym, i;
    uint32_t* buckets = p->ghashtab + 4 + (p->ghashtab[2] * sizeof(size_t) / 4);
    uint32_t* hashval;
    for (i = nsym = 0; i < p->ghashtab[0]; i++) {
        if (buckets[i] > nsym)
            nsym = buckets[i];
    }
    if (nsym) {
        hashval = buckets + p->ghashtab[0] + (nsym - p->ghashtab[1]);
        do
            nsym++;
        while (!(*hashval++ & 1));
    }
    return nsym;
}

__NO_SAFESTACK static struct dso* find_library_in(struct dso* p,
                                                  const char* name) {
    while (p != NULL) {
        if (!strcmp(p->name, name) ||
            (p->soname != NULL && !strcmp(p->soname, name))) {
            ++p->refcnt;
            break;
        }
        p = p->next;
    }
    return p;
}

__NO_SAFESTACK static struct dso* find_library(const char* name) {
    int is_self = 0;

    /* Catch and block attempts to reload the implementation itself */
    if (name[0] == 'l' && name[1] == 'i' && name[2] == 'b') {
        static const char *rp, reserved[] = "c\0pthread\0rt\0m\0dl\0util\0xnet\0";
        char* z = strchr(name, '.');
        if (z) {
            size_t l = z - name;
            for (rp = reserved; *rp && strncmp(name + 3, rp, l - 3); rp += strlen(rp) + 1)
                ;
            if (*rp) {
                if (ldd_mode) {
                    /* Track which names have been resolved
                     * and only report each one once. */
                    static unsigned reported;
                    unsigned mask = 1U << (rp - reserved);
                    if (!(reported & mask)) {
                        reported |= mask;
                        debugmsg("\t%s => %s (%p)\n",
                                 name, ldso.name, ldso.base);
                    }
                }
                is_self = 1;
            }
        }
    }
    if (!strcmp(name, ldso.name))
        is_self = 1;
    if (is_self) {
        if (!ldso.prev) {
            tail->next = &ldso;
            ldso.prev = tail;
            tail = ldso.next ? ldso.next : &ldso;
        }
        return &ldso;
    }

    // First see if it's in the general list.
    struct dso* p = find_library_in(head, name);
    if (p == NULL && ldso.prev == NULL) {
        // ldso is not in the list yet, so the first search didn't notice
        // anything that is only a dependency of ldso, i.e. the vDSO.
        // See if the lookup by name matches ldso or its dependencies.
        p = find_library_in(&ldso, name);
        if (p != NULL) {
            // Take it out of its place in the list rooted at ldso.
            if (p->prev != NULL)
                p->prev->next = p->next;
            if (p->next != NULL)
                p->next->prev = p->prev;
            // Stick it on the main list.
            tail->next = p;
            p->prev = tail;
            tail = p;
        }
    }

    return p;
}

#define MAX_BUILDID_SIZE 64

__NO_SAFESTACK static void read_buildid(struct dso* p,
                                        char* buf, size_t buf_size) {
    Phdr* ph = p->phdr;
    size_t cnt;

    for (cnt = p->phnum; cnt--; ph = (void*)((char*)ph + p->phentsize)) {
        if (ph->p_type != PT_NOTE)
            continue;

        // Find the PT_LOAD segment we live in.
        Phdr* ph2 = p->phdr;
        Phdr* ph_load = NULL;
        size_t cnt2;
        for (cnt2 = p->phnum; cnt2--; ph2 = (void*)((char*)ph2 + p->phentsize)) {
            if (ph2->p_type != PT_LOAD)
                continue;
            if (ph->p_vaddr >= ph2->p_vaddr &&
                ph->p_vaddr < ph2->p_vaddr + ph2->p_filesz) {
                ph_load = ph2;
                break;
            }
        }
        if (ph_load == NULL)
            continue;

        size_t off = ph_load->p_vaddr + (ph->p_offset - ph_load->p_offset);
        size_t size = ph->p_filesz;

        struct {
            Elf32_Nhdr hdr;
            char name[sizeof("GNU")];
        } hdr;

        while (size > sizeof(hdr)) {
            memcpy(&hdr, (char*)p->base + off, sizeof(hdr));
            size_t header_size = sizeof(Elf32_Nhdr) + ((hdr.hdr.n_namesz + 3) & -4);
            size_t payload_size = (hdr.hdr.n_descsz + 3) & -4;
            off += header_size;
            size -= header_size;
            uint8_t* payload = (uint8_t*)p->base + off;
            off += payload_size;
            size -= payload_size;
            if (hdr.hdr.n_type != NT_GNU_BUILD_ID ||
                hdr.hdr.n_namesz != sizeof("GNU") ||
                memcmp(hdr.name, "GNU", sizeof("GNU")) != 0) {
                continue;
            }
            if (hdr.hdr.n_descsz > MAX_BUILDID_SIZE) {
                // TODO(dje): Revisit.
                snprintf(buf, buf_size, "build_id_too_large_%u", hdr.hdr.n_descsz);
            } else {
                for (size_t i = 0; i < hdr.hdr.n_descsz; ++i) {
                    snprintf(&buf[i * 2], 3, "%02x", payload[i]);
                }
            }
            return;
        }
    }

    strcpy(buf, "<none>");
}

__NO_SAFESTACK static void trace_load(struct dso* p) {
    static mx_koid_t pid = MX_KOID_INVALID;
    if (pid == MX_KOID_INVALID) {
        mx_info_handle_basic_t process_info;
        if (_mx_object_get_info(__magenta_process_self,
                                MX_INFO_HANDLE_BASIC,
                                &process_info, sizeof(process_info),
                                NULL, NULL) == NO_ERROR) {
            pid = process_info.koid;
        } else {
            // No point in continually calling mx_object_get_info.
            // The first 100 are reserved.
            pid = 1;
        }
    }

    // Compute extra values useful to tools.
    // This is done here so that it's only done when necessary.
    char buildid[MAX_BUILDID_SIZE * 2 + 1];
    read_buildid(p, buildid, sizeof(buildid));

    const char* name = p->soname == NULL ? "<application>" : p->name;
    const char* soname = p->soname == NULL ? "<application>" : p->soname;

    // The output is in multiple lines to cope with damn line wrapping.
    // N.B. Programs like the Intel Processor Trace decoder parse this output.
    // Do not change without coordination with consumers.
    // TODO(dje): Switch to official tracing mechanism when ready. MG-519
    static int seqno;
    debugmsg("@trace_load: %" PRIu64 ":%da %p %p %p",
             pid, seqno, p->base, p->map, p->map + p->map_len);
    debugmsg("@trace_load: %" PRIu64 ":%db %s",
             pid, seqno, buildid);
    debugmsg("@trace_load: %" PRIu64 ":%dc %s %s",
             pid, seqno, soname, name);
    ++seqno;
}

__NO_SAFESTACK static mx_status_t load_library_vmo(mx_handle_t vmo,
                                                   const char* name,
                                                   int rtld_mode,
                                                   struct dso* needed_by,
                                                   struct dso** loaded) {
    struct dso *p, temp_dso = {};
    size_t alloc_size;
    int n_th = 0;

    if (rtld_mode & RTLD_NOLOAD) {
        *loaded = NULL;
        return NO_ERROR;
    }

    mx_status_t status = map_library(vmo, &temp_dso);
    if (status != NO_ERROR)
        return status;

    decode_dyn(&temp_dso);
    if (temp_dso.soname != NULL) {
        // Now check again if we opened the same file a second time.
        // That is, a file with the same DT_SONAME string.
        p = find_library(temp_dso.soname);
        if (p != NULL) {
            unmap_library(&temp_dso);
            *loaded = p;
            return NO_ERROR;
        }
    }

    if (name == NULL) {
        // If this was loaded by VMO rather than by name, then insist that
        // it have a SONAME.
        name = temp_dso.soname;
        if (name == NULL) {
            unmap_library(&temp_dso);
            return ERR_WRONG_TYPE;
        }
    }

    // Calculate how many slots are needed for dependencies.
    size_t ndeps = 0;
    for (size_t i = 0; temp_dso.dynv[i].d_tag; i++) {
        if (temp_dso.dynv[i].d_tag == DT_NEEDED)
            ++ndeps;
    }
    if (ndeps > 0)
        // Account for a NULL terminator.
        ++ndeps;

    /* Allocate storage for the new DSO. When there is TLS, this
     * storage must include a reservation for all pre-existing
     * threads to obtain copies of both the new TLS, and an
     * extended DTV capable of storing an additional slot for
     * the newly-loaded DSO. */
    alloc_size = sizeof *p + ndeps * sizeof(p->deps[0]) + strlen(name) + 1;
    if (runtime && temp_dso.tls.image) {
        size_t per_th = temp_dso.tls.size + temp_dso.tls.align + sizeof(void*) * (tls_cnt + 3);
        n_th = atomic_load(&libc.thread_count);
        if (n_th > SSIZE_MAX / per_th)
            alloc_size = SIZE_MAX;
        else
            alloc_size += n_th * per_th;
    }
    p = dl_alloc(alloc_size);
    if (!p) {
        unmap_library(&temp_dso);
        return ERR_NO_MEMORY;
    }
    *p = temp_dso;
    p->refcnt = 1;
    p->needed_by = needed_by;
    p->name = (void*)&p->buf[ndeps];
    strcpy(p->name, name);
    if (p->tls.image) {
        p->tls_id = ++tls_cnt;
        tls_align = MAXP2(tls_align, p->tls.align);
#ifdef TLS_ABOVE_TP
        p->tls.offset = tls_offset + ((tls_align - 1) & -(tls_offset + (uintptr_t)p->tls.image));
        tls_offset += p->tls.size;
#else
        tls_offset += p->tls.size + p->tls.align - 1;
        tls_offset -= (tls_offset + (uintptr_t)p->tls.image) & (p->tls.align - 1);
        p->tls.offset = tls_offset;
#endif
        p->new_dtv =
            (void*)(-sizeof(size_t) & (uintptr_t)(p->name + strlen(p->name) + sizeof(size_t)));
        p->new_tls = (void*)(p->new_dtv + n_th * (tls_cnt + 1));
        if (tls_tail)
            tls_tail->next = &p->tls;
        else
            libc.tls_head = &p->tls;
        tls_tail = &p->tls;
    }

    tail->next = p;
    p->prev = tail;
    tail = p;

    if (ldd_mode)
        debugmsg("\t%s => %s (%p)\n", p->soname, name, p->base);

    *loaded = p;
    return NO_ERROR;
}

__NO_SAFESTACK static mx_status_t load_library(const char* name, int rtld_mode,
                                               struct dso* needed_by,
                                               struct dso** loaded) {
    if (!*name)
        return ERR_INVALID_ARGS;

    *loaded = find_library(name);
    if (*loaded != NULL)
        return NO_ERROR;

    mx_handle_t vmo;
    mx_status_t status = get_library_vmo(name, &vmo);
    if (status == NO_ERROR) {
        status = load_library_vmo(vmo, name, rtld_mode, needed_by, loaded);
        _mx_handle_close(vmo);
    }

    return status;
}

__NO_SAFESTACK static void load_deps(struct dso* p) {
    for (; p; p = p->next) {
        // These don't get space allocated for ->deps.
        if (p == &ldso || p == &vdso)
            continue;
        struct dso** deps = NULL;
        if (runtime && p->deps == NULL)
            deps = p->deps = p->buf;
        for (size_t i = 0; p->dynv[i].d_tag; i++) {
            if (p->dynv[i].d_tag != DT_NEEDED)
                continue;
            const char* name = p->strings + p->dynv[i].d_un.d_val;
            struct dso* dep;
            mx_status_t status = load_library(name, 0, p, &dep);
            if (status != NO_ERROR) {
                error("Error loading shared library %s: %s (needed by %s)",
                      name, _mx_status_get_string(status), p->name);
                if (runtime)
                    longjmp(*rtld_fail, 1);
            } else if (deps != NULL) {
                *deps++ = dep;
            }
        }
    }
}

__NO_SAFESTACK static void load_preload(char* s) {
    int tmp;
    char* z;
    for (z = s; *z; s = z) {
        for (; *s && (isspace(*s) || *s == ':'); s++)
            ;
        for (z = s; *z && !isspace(*z) && *z != ':'; z++)
            ;
        tmp = *z;
        *z = 0;
        struct dso *p;
        load_library(s, 0, NULL, &p);
        *z = tmp;
    }
}

__NO_SAFESTACK static void make_global(struct dso* p) {
    for (; p; p = p->next)
        p->global = 1;
}

__NO_SAFESTACK static void do_mips_relocs(struct dso* p, size_t* got) {
    size_t i, j, rel[2];
    unsigned char* base = p->base;
    i = 0;
    search_vec(p->dynv, &i, DT_MIPS_LOCAL_GOTNO);
    if (p == &ldso) {
        got += i;
    } else {
        while (i--)
            *got++ += (size_t)base;
    }
    j = 0;
    search_vec(p->dynv, &j, DT_MIPS_GOTSYM);
    i = 0;
    search_vec(p->dynv, &i, DT_MIPS_SYMTABNO);
    Sym* sym = p->syms + j;
    rel[0] = (unsigned char*)got - base;
    for (i -= j; i; i--, sym++, rel[0] += sizeof(size_t)) {
        rel[1] = R_INFO(sym - p->syms, R_MIPS_JUMP_SLOT);
        do_relocs(p, rel, sizeof rel, 2);
    }
}

__NO_SAFESTACK static void reloc_all(struct dso* p) {
    size_t dyn[DYN_CNT];
    for (; p; p = p->next) {
        if (p->relocated)
            continue;
        decode_vec(p->dynv, dyn, DYN_CNT);
        if (NEED_MIPS_GOT_RELOCS)
            do_mips_relocs(p, laddr(p, dyn[DT_PLTGOT]));
        do_relocs(p, laddr(p, dyn[DT_JMPREL]), dyn[DT_PLTRELSZ], 2 + (dyn[DT_PLTREL] == DT_RELA));
        do_relocs(p, laddr(p, dyn[DT_REL]), dyn[DT_RELSZ], 2);
        do_relocs(p, laddr(p, dyn[DT_RELA]), dyn[DT_RELASZ], 3);

        if (head != &ldso && p->relro_start != p->relro_end) {
            mx_status_t status =
                _mx_vmar_protect(p->vmar,
                                 (uintptr_t)laddr(p, p->relro_start),
                                 p->relro_end - p->relro_start,
                                 MX_VM_FLAG_PERM_READ);
            if (status == ERR_BAD_HANDLE &&
                p == &ldso && p->vmar == MX_HANDLE_INVALID) {
                debugmsg("No VMAR_LOADED handle received;"
                         " cannot protect RELRO for %s\n",
                         p->name);
            } else if (status != NO_ERROR) {
                error("Error relocating %s: RELRO protection"
                      " %p+%#zx failed: %s",
                      p->name,
                      laddr(p, p->relro_start), p->relro_end - p->relro_start,
                      _mx_status_get_string(status));
                if (runtime)
                    longjmp(*rtld_fail, 1);
            }
        }

        // Hold the VMAR handle only long enough to apply RELRO.
        // Now it's no longer needed and the mappings cannot be
        // changed any more (only unmapped).
        _mx_handle_close(p->vmar);
        p->vmar = MX_HANDLE_INVALID;

        p->relocated = 1;
    }
}

__NO_SAFESTACK static void kernel_mapped_dso(struct dso* p) {
    size_t min_addr = -1, max_addr = 0, cnt;
    Phdr* ph = p->phdr;
    for (cnt = p->phnum; cnt--; ph = (void*)((char*)ph + p->phentsize)) {
        if (ph->p_type == PT_DYNAMIC) {
            p->dynv = laddr(p, ph->p_vaddr);
        } else if (ph->p_type == PT_GNU_RELRO) {
            p->relro_start = ph->p_vaddr & -PAGE_SIZE;
            p->relro_end = (ph->p_vaddr + ph->p_memsz) & -PAGE_SIZE;
        }
        if (ph->p_type != PT_LOAD)
            continue;
        if (ph->p_vaddr < min_addr)
            min_addr = ph->p_vaddr;
        if (ph->p_vaddr + ph->p_memsz > max_addr)
            max_addr = ph->p_vaddr + ph->p_memsz;
    }
    min_addr &= -PAGE_SIZE;
    max_addr = (max_addr + PAGE_SIZE - 1) & -PAGE_SIZE;
    p->map = p->base + min_addr;
    p->map_len = max_addr - min_addr;
}

void __libc_exit_fini(void) {
    struct dso* p;
    size_t dyn[DYN_CNT];
    for (p = fini_head; p; p = p->fini_next) {
        if (!p->constructed)
            continue;
        decode_vec(p->dynv, dyn, DYN_CNT);
        if (dyn[0] & (1 << DT_FINI_ARRAY)) {
            size_t n = dyn[DT_FINI_ARRAYSZ] / sizeof(size_t);
            size_t* fn = (size_t*)laddr(p, dyn[DT_FINI_ARRAY]) + n;
            while (n--)
                ((void (*)(void)) * --fn)();
        }
#ifndef NO_LEGACY_INITFINI
        if ((dyn[0] & (1 << DT_FINI)) && dyn[DT_FINI])
            fpaddr(p, dyn[DT_FINI])();
#endif
    }
}

static void do_init_fini(struct dso* p) {
    size_t dyn[DYN_CNT];
    /* Allow recursive calls that arise when a library calls
     * dlopen from one of its constructors, but block any
     * other threads until all ctors have finished. */
    pthread_mutex_lock(&init_fini_lock);
    for (; p; p = p->prev) {
        if (p->constructed)
            continue;
        p->constructed = 1;
        decode_vec(p->dynv, dyn, DYN_CNT);
        if (dyn[0] & ((1 << DT_FINI) | (1 << DT_FINI_ARRAY))) {
            p->fini_next = fini_head;
            fini_head = p;
        }
#ifndef NO_LEGACY_INITFINI
        if ((dyn[0] & (1 << DT_INIT)) && dyn[DT_INIT])
            fpaddr(p, dyn[DT_INIT])();
#endif
        if (dyn[0] & (1 << DT_INIT_ARRAY)) {
            size_t n = dyn[DT_INIT_ARRAYSZ] / sizeof(size_t);
            size_t* fn = laddr(p, dyn[DT_INIT_ARRAY]);
            while (n--)
                ((void (*)(void)) * fn++)();
        }
    }
    pthread_mutex_unlock(&init_fini_lock);
}

void __libc_start_init(void) {
    do_init_fini(tail);
}

static void dl_debug_state(void) {}

weak_alias(dl_debug_state, _dl_debug_state);

__attribute__((__visibility__("hidden"))) void* __tls_get_new(size_t* v) {
    pthread_t self = __pthread_self();

    /* Block signals to make accessing new TLS async-signal-safe */
    sigset_t set;
    __block_all_sigs(&set);
    if (v[0] <= (size_t)self->head.dtv[0]) {
        __restore_sigs(&set);
        return (char*)self->head.dtv[v[0]] + v[1] + DTP_OFFSET;
    }

    /* This is safe without any locks held because, if the caller
     * is able to request the Nth entry of the DTV, the DSO list
     * must be valid at least that far out and it was synchronized
     * at program startup or by an already-completed call to dlopen. */
    struct dso* p;
    for (p = head; p->tls_id != v[0]; p = p->next)
        ;

    /* Get new DTV space from new DSO if needed */
    if (v[0] > (size_t)self->head.dtv[0]) {
        void** newdtv = p->new_dtv + (v[0] + 1) * atomic_fetch_add(&p->new_dtv_idx, 1);
        memcpy(newdtv, self->head.dtv, ((size_t)self->head.dtv[0] + 1) * sizeof(void*));
        newdtv[0] = (void*)v[0];
        self->head.dtv = newdtv;
    }

    /* Get new TLS memory from all new DSOs up to the requested one */
    unsigned char* mem;
    for (p = head;; p = p->next) {
        if (!p->tls_id || self->head.dtv[p->tls_id])
            continue;
        mem = p->new_tls + (p->tls.size + p->tls.align) * atomic_fetch_add(&p->new_tls_idx, 1);
        mem += ((uintptr_t)p->tls.image - (uintptr_t)mem) & (p->tls.align - 1);
        self->head.dtv[p->tls_id] = mem;
        memcpy(mem, p->tls.image, p->tls.len);
        if (p->tls_id == v[0])
            break;
    }
    __restore_sigs(&set);
    return mem + v[1] + DTP_OFFSET;
}

__NO_SAFESTACK struct pthread* __init_main_thread(mx_handle_t thread_self) {
    pthread_attr_t attr = DEFAULT_PTHREAD_ATTR;
    attr._a_stacksize = libc.stack_size;

    pthread_t td = __allocate_thread(&attr);
    if (td == NULL) {
        debugmsg("No memory for %zu bytes thread-local storage.\n",
                 libc.tls_size);
        _exit(127);
    }

    mx_status_t status = mxr_thread_adopt(thread_self, &td->mxr_thread);
    if (status != NO_ERROR)
        __builtin_trap();

    mxr_tp_set(thread_self, pthread_to_tp(td));
    return td;
}

__NO_SAFESTACK static void update_tls_size(void) {
    libc.tls_cnt = tls_cnt;
    libc.tls_align = tls_align;
    libc.tls_size =
        ALIGN((1 + tls_cnt) * sizeof(void*) + tls_offset + sizeof(struct pthread) + tls_align * 2,
              tls_align);
    // TODO(mcgrathr): The TLS block is always allocated in whole pages.
    // We should keep track of the available slop to the end of the page
    // and make dlopen use that for new dtv/TLS space when it fits.
}

/* Stage 1 of the dynamic linker is defined in dlstart.c. It calls the
 * following stage 2 and stage 3 functions via primitive symbolic lookup
 * since it does not have access to their addresses to begin with. */

/* Stage 2 of the dynamic linker is called after relative relocations
 * have been processed. It can make function calls to static functions
 * and access string literals and static data, but cannot use extern
 * symbols. Its job is to perform symbolic relocations on the dynamic
 * linker itself, but some of the relocations performed may need to be
 * replaced later due to copy relocations in the main program. */

static dl_start_return_t __dls3(void* start_arg);

__NO_SAFESTACK __attribute__((__visibility__("hidden")))
dl_start_return_t __dls2(
    void* start_arg, void* vdso_map) {
    ldso.base = (unsigned char*)__ehdr_start;

    Ehdr* ehdr = (void*)ldso.base;
    ldso.name = (char*)"libc.so";
    ldso.global = -1;
    ldso.phnum = ehdr->e_phnum;
    ldso.phdr = laddr(&ldso, ehdr->e_phoff);
    ldso.phentsize = ehdr->e_phentsize;
    kernel_mapped_dso(&ldso);
    decode_dyn(&ldso);

    if (vdso_map != NULL) {
        // The vDSO was mapped in by our creator.  Stitch it in as
        // a preloaded shared object right away, so ld.so itself
        // can depend on it and require its symbols.

        vdso.base = vdso_map;
        vdso.name = (char*)"<vDSO>";
        vdso.global = -1;

        Ehdr* ehdr = vdso_map;
        vdso.phnum = ehdr->e_phnum;
        vdso.phdr = laddr(&vdso, ehdr->e_phoff);
        vdso.phentsize = ehdr->e_phentsize;
        kernel_mapped_dso(&vdso);
        decode_dyn(&vdso);

        vdso.prev = &ldso;
        ldso.next = &vdso;
    }

    /* Prepare storage for to save clobbered REL addends so they
     * can be reused in stage 3. There should be very few. If
     * something goes wrong and there are a huge number, abort
     * instead of risking stack overflow. */
    size_t dyn[DYN_CNT];
    decode_vec(ldso.dynv, dyn, DYN_CNT);
    size_t* rel = laddr(&ldso, dyn[DT_REL]);
    size_t rel_size = dyn[DT_RELSZ];
    size_t symbolic_rel_cnt = 0;
    apply_addends_to = rel;
    for (; rel_size; rel += 2, rel_size -= 2 * sizeof(size_t))
        if (!IS_RELATIVE(rel[1], ldso.syms))
            symbolic_rel_cnt++;
    if (symbolic_rel_cnt >= ADDEND_LIMIT)
        __builtin_trap();
    size_t addends[symbolic_rel_cnt + 1];
    saved_addends = addends;

    head = &ldso;
    reloc_all(&ldso);

    ldso.relocated = 0;

    // Make sure all the relocations have landed before calling __dls3,
    // which relies on them.
    atomic_signal_fence(memory_order_seq_cst);

    return __dls3(start_arg);
}

/* Stage 3 of the dynamic linker is called with the dynamic linker/libc
 * fully functional. Its job is to load (if not already loaded) and
 * process dependencies and relocations for the main application and
 * transfer control to its entry point. */

__NO_SAFESTACK static void* dls3(mx_handle_t exec_vmo, int argc, char** argv) {
    static struct dso app;
    size_t i;
    char** argv_orig = argv;

    if (argc < 1 || argv[0] == NULL) {
        static const char* dummy_argv0 = "";
        argv = (char**)&dummy_argv0;
    }

    libc.page_size = PAGE_SIZE;

    bool log_libs = false;
    char* ld_preload = getenv("LD_PRELOAD");
    const char* ld_debug = getenv("LD_DEBUG");
    if (ld_debug != NULL && ld_debug[0] != '\0')
        log_libs = true;

    {
        // Features like Intel Processor Trace require specific output in a
        // specific format. Thus this output has its own env var.
        const char* ld_trace = getenv("LD_TRACE");
        if (ld_trace != NULL && ld_trace[0] != '\0')
            trace_maps = true;
    }

    if (exec_vmo == MX_HANDLE_INVALID) {
        char* ldname = argv[0];
        size_t l = strlen(ldname);
        if (l >= 3 && !strcmp(ldname + l - 3, "ldd"))
            ldd_mode = 1;
        argv++;
        while (argv[0] && argv[0][0] == '-' && argv[0][1] == '-') {
            char* opt = argv[0] + 2;
            *argv++ = (void*)-1;
            if (!*opt) {
                break;
            } else if (!memcmp(opt, "list", 5)) {
                ldd_mode = 1;
            } else if (!memcmp(opt, "preload", 7)) {
                if (opt[7] == '=')
                    ld_preload = opt + 8;
                else if (opt[7])
                    *argv = 0;
                else if (*argv)
                    ld_preload = *argv++;
            } else {
                argv[0] = 0;
            }
        }
        argc -= argv - argv_orig;
        if (!argv[0]) {
            debugmsg("musl libc (" LDSO_ARCH ")\n"
                     "Dynamic Program Loader\n"
                     "Usage: %s [options] [--] pathname%s\n",
                     ldname, ldd_mode ? "" : " [args]");
            _exit(1);
        }

        ldso.name = ldname;

        mx_status_t status = get_library_vmo(argv[0], &exec_vmo);
        if (status != NO_ERROR) {
            debugmsg("%s: cannot load %s: %d\n", ldname, argv[0], status);
            _exit(1);
        }
    }

    mx_status_t status = map_library(exec_vmo, &app);
    _mx_handle_close(exec_vmo);
    if (status != NO_ERROR) {
        debugmsg("%s: %s: Not a valid dynamic program (%s)\n",
                 ldso.name, argv[0], _mx_status_get_string(status));
        _exit(1);
    }

    app.name = argv[0];

    /* Find the name that would have been used for the dynamic
     * linker had ldd not taken its place. */
    if (ldd_mode) {
        for (i = 0; i < app.phnum; i++) {
            if (app.phdr[i].p_type == PT_INTERP)
                ldso.name = laddr(&app, app.phdr[i].p_vaddr);
        }
        debugmsg("\t%s (%p)\n", ldso.name, ldso.base);
    }

    if (app.tls.size) {
        libc.tls_head = tls_tail = &app.tls;
        app.tls_id = tls_cnt = 1;
#ifdef TLS_ABOVE_TP
        app.tls.offset = 0;
        tls_offset =
            app.tls.size + (-((uintptr_t)app.tls.image + app.tls.size) & (app.tls.align - 1));
#else
        tls_offset = app.tls.offset =
            app.tls.size + (-((uintptr_t)app.tls.image + app.tls.size) & (app.tls.align - 1));
#endif
        tls_align = MAXP2(tls_align, app.tls.align);
    }

    app.global = 1;
    decode_dyn(&app);

    /* Initial dso chain consists only of the app. */
    head = tail = &app;

    /* Load preload/needed libraries, add their symbols to the global
     * namespace, and perform all remaining relocations. */
    if (ld_preload)
        load_preload(ld_preload);
    load_deps(&app);
    make_global(&app);

    for (i = 0; app.dynv[i].d_tag; i++) {
        if (!DT_DEBUG_INDIRECT && app.dynv[i].d_tag == DT_DEBUG)
            app.dynv[i].d_un.d_ptr = (size_t)&debug;
        if (DT_DEBUG_INDIRECT && app.dynv[i].d_tag == DT_DEBUG_INDIRECT) {
            size_t* ptr = (size_t*)app.dynv[i].d_un.d_ptr;
            *ptr = (size_t)&debug;
        }
    }

    /* The main program must be relocated LAST since it may contin
     * copy relocations which depend on libraries' relocations. */
    reloc_all(app.next);
    reloc_all(&app);

    update_tls_size();
    static_tls_cnt = tls_cnt;

    if (ldso_fail)
        _exit(127);
    if (ldd_mode)
        _exit(0);

    /* Switch to runtime mode: any further failures in the dynamic
     * linker are a reportable failure rather than a fatal startup
     * error. */
    runtime = 1;

    debug.ver = 1;
    debug.bp = dl_debug_state;
    debug.head = head;
    debug.base = ldso.base;
    debug.state = 0;

    status = _mx_object_set_property(__magenta_process_self,
                                     MX_PROP_PROCESS_DEBUG_ADDR,
                                     &_dl_debug_addr, sizeof(_dl_debug_addr));
    if (status != NO_ERROR) {
        // Bummer. Crashlogger backtraces, debugger sessions, etc. will be
        // problematic, but this isn't fatal.
        // TODO(dje): Is there a way to detect we're here because of being
        // an injected process (launchpad_start_injected)? IWBN to print a
        // warning here but launchpad_start_injected can trigger this.
    }

    _dl_debug_state();

    if (log_libs) {
        for (struct dso* p = &app; p != NULL; p = p->next) {
            const char* name = p->name[0] == '\0' ? "<application>" : p->name;
            const char* a = "";
            const char* b = "";
            const char* c = "";
            if (p->soname != NULL && strcmp(name, p->soname)) {
                a = " (";
                b = p->soname;
                c = ")";
            }
            if (p->base == p->map)
                debugmsg("Loaded at [%p,%p): %s%s%s%s\n",
                         p->map, p->map + p->map_len, name, a, b, c);
            else
                debugmsg("Loaded at [%p,%p) bias %p: %s%s%s%s\n",
                         p->map, p->map + p->map_len, p->base, name, a, b, c);
        }
    }

    if (trace_maps) {
        for (struct dso* p = &app; p != NULL; p = p->next) {
            trace_load(p);
        }
    }

    // Reset from the argv[0] value so we don't save a dangling pointer
    // into the caller's stack frame.
    app.name = (char*)"";

    // Check for a PT_GNU_STACK header requesting a main thread stack size.
    libc.stack_size = DEFAULT_PTHREAD_ATTR._a_stacksize;
    for (i = 0; i < app.phnum; i++) {
        if (app.phdr[i].p_type == PT_GNU_STACK) {
            size_t size = app.phdr[i].p_memsz;
            if (size > 0)
                libc.stack_size = size;
            break;
        }
    }

    const Ehdr* ehdr = (void*)app.map;
    return laddr(&app, ehdr->e_entry);
}

__NO_SAFESTACK static dl_start_return_t __dls3(void* start_arg) {
    mx_handle_t bootstrap = (uintptr_t)start_arg;

    uint32_t nbytes, nhandles;
    mx_status_t status = mxr_message_size(bootstrap, &nbytes, &nhandles);
    if (status != NO_ERROR) {
        error("mxr_message_size bootstrap handle %#x failed: %d (%s)",
              bootstrap, status, _mx_status_get_string(status));
        nbytes = nhandles = 0;
    }

    MXR_PROCESSARGS_BUFFER(buffer, nbytes);
    mx_handle_t handles[nhandles];
    mx_proc_args_t* procargs;
    uint32_t* handle_info;
    if (status == NO_ERROR)
        status = mxr_processargs_read(bootstrap, buffer, nbytes,
                                      handles, nhandles,
                                      &procargs, &handle_info);
    if (status != NO_ERROR) {
        error("bad message of %u bytes, %u handles"
              " from bootstrap handle %#x: %d (%s)",
              nbytes, nhandles, bootstrap, status,
              _mx_status_get_string(status));
        nbytes = nhandles = 0;
    }

    mx_handle_t exec_vmo = MX_HANDLE_INVALID;
    for (int i = 0; i < nhandles; ++i) {
        switch (PA_HND_TYPE(handle_info[i])) {
        case PA_SVC_LOADER:
            if (loader_svc != MX_HANDLE_INVALID ||
                handles[i] == MX_HANDLE_INVALID) {
                error("bootstrap message bad LOADER_SVC %#x vs %#x",
                      handles[i], loader_svc);
            }
            loader_svc = handles[i];
            break;
        case PA_VMO_EXECUTABLE:
            if (exec_vmo != MX_HANDLE_INVALID ||
                handles[i] == MX_HANDLE_INVALID) {
                error("bootstrap message bad EXEC_VMO %#x vs %#x",
                      handles[i], exec_vmo);
            }
            exec_vmo = handles[i];
            break;
        case PA_MXIO_LOGGER:
            if (logger != MX_HANDLE_INVALID ||
                handles[i] == MX_HANDLE_INVALID) {
                error("bootstrap message bad MXIO_LOGGER %#x vs %#x",
                      handles[i], logger);
            }
            logger = handles[i];
            break;
        case PA_VMAR_LOADED:
            if (ldso.vmar != MX_HANDLE_INVALID ||
                handles[i] == MX_HANDLE_INVALID) {
                error("bootstrap message bad VMAR_LOADED %#x vs %#x",
                      handles[i], ldso.vmar);
            }
            ldso.vmar = handles[i];
            break;
        case PA_PROC_SELF:
            __magenta_process_self = handles[i];
            break;
        case PA_VMAR_ROOT:
            __magenta_vmar_root_self = handles[i];
            break;
        default:
            _mx_handle_close(handles[i]);
            break;
        }
    }

    if (__magenta_process_self == MX_HANDLE_INVALID)
        error("bootstrap message bad no proc self");
    if (__magenta_vmar_root_self == MX_HANDLE_INVALID)
        error("bootstrap message bad no root vmar");

    // Unpack the environment strings so dls3 can use getenv.
    char* argv[procargs->args_num + 1];
    char* envp[procargs->environ_num + 1];
    status = mxr_processargs_strings(buffer, nbytes, argv, envp, NULL);
    if (status == NO_ERROR)
        __environ = envp;

    void* entry = dls3(exec_vmo, procargs->args_num, argv);

    // Reset it so there's no dangling pointer to this stack frame.
    // Presumably the parent will send the same strings in the main
    // bootstrap message, but that's for __libc_start_main to see.
    __environ = NULL;

    if (vdso.global <= 0) {
        // Nothing linked against the vDSO.  Ideally we would unmap the
        // vDSO, but there is no way to do it because the unmap system call
        // would try to return to the vDSO code and crash.
        if (ldso.global < 0) {
            // TODO(mcgrathr): We could free all heap data structures, and
            // with some vDSO assistance unmap ourselves and unwind back to
            // the user entry point.  Thus a program could link against the
            // vDSO alone and not use this libc/ldso at all after startup.
            // We'd need to be sure there are no TLSDESC entries pointing
            // back to our code, but other than that there should no longer
            // be a way to enter our code.
        } else {
            debugmsg("Dynamic linker %s doesn't link in vDSO %s???\n",
                     ldso.name, vdso.name);
            _exit(127);
        }
    } else if (ldso.global <= 0) {
        // This should be impossible.
        __builtin_trap();
    }

   return DL_START_RETURN(entry, start_arg);
}

static void* dlopen_internal(mx_handle_t vmo, const char* file, int mode) {
    pthread_rwlock_wrlock(&lock);
    __thread_allocation_inhibit();

    struct dso* orig_tail = tail;

    struct dso* p;
    mx_status_t status = (vmo != MX_HANDLE_INVALID ?
                          load_library_vmo(vmo, file, mode, head, &p) :
                          load_library(file, mode, head, &p));

    if (status != NO_ERROR) {
        error("Error loading shared library %s: %s",
              file, _mx_status_get_string(status));
    fail:
        __thread_allocation_release();
        pthread_rwlock_unlock(&lock);
        return NULL;
    }

    if (p == NULL) {
        if (!(mode & RTLD_NOLOAD))
            __builtin_trap();
        error("Library %s is not already loaded", file);
        goto fail;
    }

    struct tls_module* orig_tls_tail = tls_tail;
    size_t orig_tls_cnt = tls_cnt;
    size_t orig_tls_offset = tls_offset;
    size_t orig_tls_align = tls_align;

    struct dl_alloc_checkpoint checkpoint;
    dl_alloc_checkpoint(&checkpoint);

    size_t i;
    jmp_buf jb;
    rtld_fail = &jb;
    if (setjmp(*rtld_fail)) {
        /* Clean up anything new that was (partially) loaded */
        if (p && p->deps)
            for (i = 0; p->deps[i]; i++)
                if (p->deps[i]->global < 0)
                    p->deps[i]->global = 0;
        for (p = orig_tail->next; p; p = p->next)
            unmap_library(p);
        if (!orig_tls_tail)
            libc.tls_head = 0;
        tls_tail = orig_tls_tail;
        tls_cnt = orig_tls_cnt;
        tls_offset = orig_tls_offset;
        tls_align = orig_tls_align;
        tail = orig_tail;
        tail->next = 0;
        dl_alloc_rollback(&checkpoint);
        goto fail;
    }

    /* First load handling */
    if (!p->deps) {
        load_deps(p);
        if (p->deps)
            for (i = 0; p->deps[i]; i++)
                if (!p->deps[i]->global)
                    p->deps[i]->global = -1;
        if (!p->global)
            p->global = -1;
        reloc_all(p);
        if (p->deps)
            for (i = 0; p->deps[i]; i++)
                if (p->deps[i]->global < 0)
                    p->deps[i]->global = 0;
        if (p->global < 0)
            p->global = 0;
    }

    if (mode & RTLD_GLOBAL) {
        if (p->deps)
            for (i = 0; p->deps[i]; i++)
                p->deps[i]->global = 1;
        p->global = 1;
    }

    update_tls_size();
    _dl_debug_state();
    if (trace_maps) {
        trace_load(p);
    }

    // Allow thread creation, now that the TLS bookkeeping is consistent.
    __thread_allocation_release();

    // Bump the dl_iterate_phdr dlpi_adds counter.
    gencnt++;

    // Collect the current new tail before we release the lock.
    // Another dlopen can come in and advance the tail, but we
    // alone are responsible for making sure that do_init_fini
    // starts with the first object we just added.
    struct dso* new_tail = tail;

    pthread_rwlock_unlock(&lock);

    do_init_fini(new_tail);

    return p;
}

void* dlopen(const char* file, int mode) {
    if (!file)
        return head;
    return dlopen_internal(MX_HANDLE_INVALID, file, mode);
}

void* dlopen_vmo(mx_handle_t vmo, int mode) {
    if (vmo == MX_HANDLE_INVALID) {
        errno = EINVAL;
        return NULL;
    }
    return dlopen_internal(vmo, NULL, mode);
}

mx_handle_t dl_set_loader_service(mx_handle_t new_svc) {
    mx_handle_t old_svc;
    pthread_rwlock_wrlock(&lock);
    old_svc = loader_svc;
    loader_svc = new_svc;
    pthread_rwlock_unlock(&lock);
    return old_svc;
}

__attribute__((__visibility__("hidden"))) int __dl_invalid_handle(void* h) {
    struct dso* p;
    for (p = head; p; p = p->next)
        if (h == p)
            return 0;
    error("Invalid library handle %p", (void*)h);
    return 1;
}

static void* addr2dso(size_t a) {
    struct dso* p;
    for (p = head; p; p = p->next) {
        if (a - (size_t)p->map < p->map_len)
            return p;
    }
    return 0;
}

void* __tls_get_addr(size_t*);

static void* do_dlsym(struct dso* p, const char* s, void* ra) {
    size_t i;
    uint32_t h = 0, gh = 0, *ght;
    Sym* sym;
    if (p == head || p == RTLD_DEFAULT || p == RTLD_NEXT) {
        if (p == RTLD_DEFAULT) {
            p = head;
        } else if (p == RTLD_NEXT) {
            p = addr2dso((size_t)ra);
            if (!p)
                p = head;
            p = p->next;
        }
        struct symdef def = find_sym(p, s, 0);
        if (!def.sym)
            goto failed;
        if ((def.sym->st_info & 0xf) == STT_TLS)
            return __tls_get_addr((size_t[]){def.dso->tls_id, def.sym->st_value});
        return laddr(def.dso, def.sym->st_value);
    }
    if (__dl_invalid_handle(p))
        return 0;
    if ((ght = p->ghashtab)) {
        gh = gnu_hash(s);
        sym = gnu_lookup(gh, ght, p, s);
    } else {
        h = sysv_hash(s);
        sym = sysv_lookup(s, h, p);
    }
    if (sym && (sym->st_info & 0xf) == STT_TLS)
        return __tls_get_addr((size_t[]){p->tls_id, sym->st_value});
    if (sym && sym->st_value && (1 << (sym->st_info & 0xf) & OK_TYPES))
        return laddr(p, sym->st_value);
    if (p->deps)
        for (i = 0; p->deps[i]; i++) {
            if ((ght = p->deps[i]->ghashtab)) {
                if (!gh)
                    gh = gnu_hash(s);
                sym = gnu_lookup(gh, ght, p->deps[i], s);
            } else {
                if (!h)
                    h = sysv_hash(s);
                sym = sysv_lookup(s, h, p->deps[i]);
            }
            if (sym && (sym->st_info & 0xf) == STT_TLS)
                return __tls_get_addr((size_t[]){p->deps[i]->tls_id, sym->st_value});
            if (sym && sym->st_value && (1 << (sym->st_info & 0xf) & OK_TYPES))
                return laddr(p->deps[i], sym->st_value);
        }
failed:
    error("Symbol not found: %s", s);
    return 0;
}

int dladdr(const void* addr, Dl_info* info) {
    struct dso* p;
    Sym *sym, *bestsym;
    uint32_t nsym;
    char* strings;
    void* best = 0;

    pthread_rwlock_rdlock(&lock);
    p = addr2dso((size_t)addr);
    pthread_rwlock_unlock(&lock);

    if (!p)
        return 0;

    sym = p->syms;
    strings = p->strings;
    nsym = count_syms(p);

    if (!best)
        for (; nsym; nsym--, sym++) {
            if (sym->st_value && (1 << (sym->st_info & 0xf) & OK_TYPES) &&
                (1 << (sym->st_info >> 4) & OK_BINDS)) {
                void* symaddr = laddr(p, sym->st_value);
                if (symaddr > addr || symaddr < best)
                    continue;
                best = symaddr;
                bestsym = sym;
                if (addr == symaddr)
                    break;
            }
        }

    if (!best)
        return 0;

    info->dli_fname = p->name;
    info->dli_fbase = p->base;
    info->dli_sname = strings + bestsym->st_name;
    info->dli_saddr = best;

    return 1;
}

void* dlsym(void* restrict p, const char* restrict s) {
    void* res;
    pthread_rwlock_rdlock(&lock);
    res = do_dlsym(p, s, __builtin_return_address(0));
    pthread_rwlock_unlock(&lock);
    return res;
}

int dl_iterate_phdr(int (*callback)(struct dl_phdr_info* info, size_t size, void* data),
                    void* data) {
    struct dso* current;
    struct dl_phdr_info info;
    int ret = 0;
    for (current = head; current;) {
        info.dlpi_addr = (uintptr_t)current->base;
        info.dlpi_name = current->name;
        info.dlpi_phdr = current->phdr;
        info.dlpi_phnum = current->phnum;
        info.dlpi_adds = gencnt;
        info.dlpi_subs = 0;
        info.dlpi_tls_modid = current->tls_id;
        info.dlpi_tls_data = current->tls.image;

        ret = (callback)(&info, sizeof(info), data);

        if (ret != 0)
            break;

        pthread_rwlock_rdlock(&lock);
        current = current->next;
        pthread_rwlock_unlock(&lock);
    }
    return ret;
}

__attribute__((__visibility__("hidden"))) void __dl_vseterr(const char*, va_list);

#define LOADER_SVC_MSG_MAX 1024

// This detects recursion via the error function.
static bool loader_svc_rpc_in_progress;
static mx_txid_t loader_svc_txid;

__NO_SAFESTACK static mx_status_t loader_svc_rpc(uint32_t opcode,
                                                 const void* data, size_t len,
                                                 mx_handle_t* result) {
    mx_status_t status;
    struct {
        mx_loader_svc_msg_t header;
        uint8_t data[LOADER_SVC_MSG_MAX - sizeof(mx_loader_svc_msg_t)];
    } msg;

    loader_svc_rpc_in_progress = true;

    if (len >= sizeof msg.data) {
        error("message of %zu bytes too large for loader service protocol",
              len);
        status = ERR_OUT_OF_RANGE;
        goto out;
    }

    memset(&msg.header, 0, sizeof msg.header);
    msg.header.txid = ++loader_svc_txid;
    msg.header.opcode = opcode;
    memcpy(msg.data, data, len);
    msg.data[len] = 0;
    if (result != NULL) {
      // Don't return an uninitialized value if the channel call
      // succeeds but doesn't provide any handles.
      *result = MX_HANDLE_INVALID;
    }

    mx_channel_call_args_t call = {
        .wr_bytes = &msg,
        .wr_num_bytes = sizeof(msg.header) + len + 1,
        .rd_bytes = &msg,
        .rd_num_bytes = sizeof(msg),
        .rd_handles = result,
        .rd_num_handles = result == NULL ? 0 : 1,
    };

    uint32_t reply_size;
    uint32_t handle_count;
    mx_status_t read_status = NO_ERROR;
    status = _mx_channel_call(loader_svc, 0, MX_TIME_INFINITE,
                              &call, &reply_size, &handle_count,
                              &read_status);
    if (status != NO_ERROR) {
        error("_mx_channel_call of %u bytes to loader service: "
              "%d (%s), read %d (%s)",
              call.wr_num_bytes, status, _mx_status_get_string(status),
              read_status, _mx_status_get_string(read_status));
        if (status == ERR_CALL_FAILED && read_status != NO_ERROR)
            status = read_status;
        goto out;
    }

    if (reply_size != sizeof(msg.header)) {
        error("loader service reply %u bytes != %u",
              reply_size, sizeof(msg.header));
        status = ERR_INVALID_ARGS;
        goto out;
    }
    if (msg.header.opcode != LOADER_SVC_OP_STATUS) {
        error("loader service reply opcode %u != %u",
              msg.header.opcode, LOADER_SVC_OP_STATUS);
        status = ERR_INVALID_ARGS;
        goto out;
    }
    if (msg.header.arg != NO_ERROR) {
        // |result| is non-null if |handle_count| > 0, because
        // |handle_count| <= |rd_num_handles|.
        if (handle_count > 0 && *result != MX_HANDLE_INVALID) {
            error("loader service error %d reply contains handle %#x",
                  msg.header.arg, *result);
            status = ERR_INVALID_ARGS;
            goto out;
        }
        status = msg.header.arg;
    }

out:
    loader_svc_rpc_in_progress = false;
    return status;
}

__NO_SAFESTACK static mx_status_t get_library_vmo(const char* name,
                                                  mx_handle_t* result) {
    if (loader_svc == MX_HANDLE_INVALID) {
        error("cannot look up \"%s\" with no loader service", name);
        return ERR_UNAVAILABLE;
    }
    return loader_svc_rpc(LOADER_SVC_OP_LOAD_OBJECT, name, strlen(name),
                          result);
}

__NO_SAFESTACK static void log_write(const void* buf, size_t len) {
    // The loader service prints "header: %s\n" when we send %s,
    // so strip a trailing newline.
    if (((const char*)buf)[len - 1] == '\n')
        --len;

    mx_status_t status;
    if (logger != MX_HANDLE_INVALID)
        status = _mx_log_write(logger, len, buf, 0);
    else if (!loader_svc_rpc_in_progress && loader_svc != MX_HANDLE_INVALID)
        status = loader_svc_rpc(LOADER_SVC_OP_DEBUG_PRINT, buf, len, NULL);
    else {
        int n = _mx_debug_write(buf, len);
        status = n < 0 ? n : NO_ERROR;
    }
    if (status != NO_ERROR)
        __builtin_trap();
}

__NO_SAFESTACK static size_t errormsg_write(FILE* f, const unsigned char* buf,
                                            size_t len) {
    if (f != NULL && f->wpos > f->wbase)
        log_write(f->wbase, f->wpos - f->wbase);

    if (len > 0)
        log_write(buf, len);

    if (f != NULL) {
        f->wend = f->buf + f->buf_size;
        f->wpos = f->wbase = f->buf;
    }

    return len;
}

__NO_SAFESTACK static int errormsg_vprintf(const char* restrict fmt,
                                           va_list ap) {
    FILE f = {
        .lbf = EOF,
        .write = errormsg_write,
        .buf = (void*)fmt,
        .buf_size = 0,
        .lock = -1,
    };
    return vfprintf(&f, fmt, ap);
}

__NO_SAFESTACK static void debugmsg(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    errormsg_vprintf(fmt, ap);
    va_end(ap);
}

__NO_SAFESTACK static void error(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    if (!runtime) {
        errormsg_vprintf(fmt, ap);
        ldso_fail = 1;
        va_end(ap);
        return;
    }
    __dl_vseterr(fmt, ap);
    va_end(ap);
}
