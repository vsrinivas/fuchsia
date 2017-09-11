#define _GNU_SOURCE
#include "dynlink.h"
#include "libc.h"
#include "asan_impl.h"
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
#include <sys/uio.h>
#include <unistd.h>

#include <inttypes.h>

#include <runtime/message.h>
#include <runtime/processargs.h>
#include <runtime/thread.h>

static void early_init(void);
static void error(const char*, ...);
static void debugmsg(const char*, ...);
static void log_write(const void* buf, size_t len);
static mx_status_t get_library_vmo(const char* name, mx_handle_t* vmo);
static void loader_svc_config(const char* config);

#define MAXP2(a, b) (-(-(a) & -(b)))
#define ALIGN(x, y) (((x) + (y)-1) & -(y))

#define VMO_NAME_DL_ALLOC "ld.so.1-internal-heap"
#define VMO_NAME_UNKNOWN "<unknown ELF file>"
#define VMO_NAME_PREFIX_BSS "bss:"
#define VMO_NAME_PREFIX_DATA "data:"

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

    union {
        const struct gnu_note* build_id_note; // Written by map_library.
        struct iovec build_id_log;      // Written by format_build_id_log.
    };
    atomic_flag logged;

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

union gnu_note_name {
    char name[sizeof("GNU")];
    uint32_t word;
};
#define GNU_NOTE_NAME ((union gnu_note_name){.name = "GNU"})
_Static_assert(sizeof(GNU_NOTE_NAME.name) == sizeof(GNU_NOTE_NAME.word), "");

struct gnu_note {
    Elf64_Nhdr nhdr;
    union gnu_note_name name;
    alignas(4) uint8_t desc[];
};

#define MIN_TLS_ALIGN alignof(struct pthread)

#define ADDEND_LIMIT 4096
static size_t *saved_addends, *apply_addends_to;

static struct dso ldso, vdso;
static struct dso *head, *tail, *fini_head;
static struct dso *detached_head;
static unsigned long long gencnt;
static int runtime __asm__("_dynlink_runtime") __USED;
static int ldso_fail;
static jmp_buf* rtld_fail;
static pthread_rwlock_t lock;
static struct debug debug;
static struct tls_module* tls_tail;
static size_t tls_cnt, tls_offset, tls_align = MIN_TLS_ALIGN;
static size_t static_tls_cnt;
static pthread_mutex_t init_fini_lock = {._m_type = PTHREAD_MUTEX_RECURSIVE};

static bool log_libs = false;
static atomic_uintptr_t unlogged_tail;

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

NO_ASAN static int dl_strcmp(const char* l, const char* r) {
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

__NO_SAFESTACK NO_ASAN __attribute__((malloc))
static void* dl_alloc(size_t size) {
    // Round the size up so the allocation pointer always stays aligned.
    size = (size + DL_ALLOC_ALIGN - 1) & -DL_ALLOC_ALIGN;

    // Get more pages if needed.  The remaining partial page, if any,
    // is wasted unless the system happens to give us the adjacent page.
    if (alloc_limit - alloc_ptr < size) {
        size_t chunk_size = (size + PAGE_SIZE - 1) & -PAGE_SIZE;
        mx_handle_t vmo;
        mx_status_t status = _mx_vmo_create(chunk_size, 0, &vmo);
        if (status != MX_OK)
            return NULL;
        _mx_object_set_property(vmo, MX_PROP_NAME,
                                VMO_NAME_DL_ALLOC, sizeof(VMO_NAME_DL_ALLOC));
        uintptr_t chunk;
        status = _mx_vmar_map(_mx_vmar_root_self(), 0, vmo, 0, chunk_size,
                              MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE,
                              &chunk);
        _mx_handle_close(vmo);
        if (status != MX_OK)
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

__NO_SAFESTACK NO_ASAN
 static void decode_vec(ElfW(Dyn)* v, size_t* a, size_t cnt) {
    size_t i;
    for (i = 0; i < cnt; i++)
        a[i] = 0;
    for (; v->d_tag; v++)
        if (v->d_tag - 1 < cnt - 1) {
            a[0] |= 1UL << v->d_tag;
            a[v->d_tag] = v->d_un.d_val;
        }
}

__NO_SAFESTACK NO_ASAN
 static int search_vec(ElfW(Dyn)* v, size_t* r, size_t key) {
    for (; v->d_tag != key; v++)
        if (!v->d_tag)
            return 0;
    *r = v->d_un.d_val;
    return 1;
}

__NO_SAFESTACK NO_ASAN static uint32_t sysv_hash(const char* s0) {
    const unsigned char* s = (void*)s0;
    uint_fast32_t h = 0;
    while (*s) {
        h = 16 * h + *s++;
        h ^= h >> 24 & 0xf0;
    }
    return h & 0xfffffff;
}

__NO_SAFESTACK NO_ASAN static uint32_t gnu_hash(const char* s0) {
    const unsigned char* s = (void*)s0;
    uint_fast32_t h = 5381;
    for (; *s; s++)
        h += h * 32 + *s;
    return h;
}

__NO_SAFESTACK NO_ASAN static Sym* sysv_lookup(const char* s, uint32_t h,
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

__NO_SAFESTACK NO_ASAN static Sym* gnu_lookup(uint32_t h1, uint32_t* hashtab,
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

__NO_SAFESTACK NO_ASAN
static Sym* gnu_lookup_filtered(uint32_t h1, uint32_t* hashtab,
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

__NO_SAFESTACK NO_ASAN
static struct symdef find_sym(struct dso* dso, const char* s, int need_def) {
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

__NO_SAFESTACK NO_ASAN static void do_relocs(struct dso* dso, size_t* rel,
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
        if (skip_relative && R_TYPE(rel[1]) == REL_RELATIVE)
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

// Locate the build ID note just after mapping the segments in.
// This is called from dls2, so it cannot use any non-static functions.
__NO_SAFESTACK NO_ASAN static bool find_buildid_note(struct dso* dso,
                                                     const Phdr* seg) {
    const char* end = laddr(dso, seg->p_vaddr + seg->p_filesz);
    for (const struct gnu_note* n = laddr(dso, seg->p_vaddr);
         (const char*)n < end;
         n = (const void*)(n->name.name +
                           ((n->nhdr.n_namesz + 3) & -4) +
                           ((n->nhdr.n_descsz + 3) & -4))) {
        if (n->nhdr.n_type == NT_GNU_BUILD_ID &&
            n->nhdr.n_namesz == sizeof(GNU_NOTE_NAME) &&
            n->name.word == GNU_NOTE_NAME.word) {
            dso->build_id_note = n;
            return true;
        }
    }
    return false;
}

// We pre-format the log line for each DSO early so that we can log it
// without running any nontrivial code.  We use hand-rolled formatting
// code to avoid using large and complex code like the printf engine.
// Each line looks like "dso: id=... base=0x... name=...\n".
#define BUILD_ID_LOG_1 "dso: id="
#define BUILD_ID_LOG_NONE "none"
#define BUILD_ID_LOG_2 " base=0x"
#define BUILD_ID_LOG_3 " name="

__NO_SAFESTACK static size_t build_id_log_size(struct dso* dso,
                                               size_t namelen) {
    size_t id_size = (dso->build_id_note == NULL ?
                      sizeof(BUILD_ID_LOG_NONE) - 1 :
                      dso->build_id_note->nhdr.n_descsz * 2);
    return (sizeof(BUILD_ID_LOG_1) - 1 + id_size +
            sizeof(BUILD_ID_LOG_2) - 1 + (sizeof(size_t) * 2) +
            sizeof(BUILD_ID_LOG_3) - 1 + namelen + 1);
}

__NO_SAFESTACK static void format_build_id_log(
    struct dso* dso, char *buffer, const char *name, size_t namelen) {
#define HEXDIGITS "0123456789abcdef"
    const struct gnu_note* note = dso->build_id_note;
    dso->build_id_log.iov_base = buffer;
    memcpy(buffer, BUILD_ID_LOG_1, sizeof(BUILD_ID_LOG_1) - 1);
    char *p = buffer + sizeof(BUILD_ID_LOG_1) - 1;
    if (note == NULL) {
        memcpy(p, BUILD_ID_LOG_NONE, sizeof(BUILD_ID_LOG_NONE) - 1);
        p += sizeof(BUILD_ID_LOG_NONE) - 1;
    } else {
        for (Elf64_Word i = 0; i < note->nhdr.n_descsz; ++i) {
            uint8_t byte = note->desc[i];
            *p++ = HEXDIGITS[byte >> 4];
            *p++ = HEXDIGITS[byte & 0xf];
        }
    }
    memcpy(p, BUILD_ID_LOG_2, sizeof(BUILD_ID_LOG_2) - 1);
    p += sizeof(BUILD_ID_LOG_2) - 1;
    uintptr_t base = (uintptr_t)dso->base;
    unsigned int shift = sizeof(uintptr_t) * 8;
    do {
        shift -= 4;
        *p++ = HEXDIGITS[(base >> shift) & 0xf];
    } while (shift > 0);
    memcpy(p, BUILD_ID_LOG_3, sizeof(BUILD_ID_LOG_3) - 1);
    p += sizeof(BUILD_ID_LOG_3) - 1;
    memcpy(p, name, namelen);
    p += namelen;
    *p++ = '\n';
    dso->build_id_log.iov_len = p - buffer;
#undef HEXDIGITS
}

__NO_SAFESTACK static void allocate_and_format_build_id_log(struct dso* dso) {
    const char* name = dso->name;
    if (name[0] == '\0')
        name = dso->soname == NULL ? "<application>" : dso->soname;
    size_t namelen = strlen(name);
    char *buffer = dl_alloc(build_id_log_size(dso, namelen));
    format_build_id_log(dso, buffer, name, namelen);
}

__NO_SAFESTACK void _dl_log_unlogged(void) {
    // The first thread to successfully swap in 0 and get an old value
    // for unlogged_tail is responsible for logging all the unlogged
    // DSOs up through that pointer.  If dlopen calls move the tail
    // and another thread then calls into here, we can race with that
    // thread.  So we use a separate atomic_flag on each 'struct dso'
    // to ensure only one thread prints each one.
    uintptr_t last_unlogged =
        atomic_load_explicit(&unlogged_tail, memory_order_acquire);
    do {
        if (last_unlogged == 0)
            return;
    } while (!atomic_compare_exchange_weak_explicit(&unlogged_tail,
                                                    &last_unlogged, 0,
                                                    memory_order_acq_rel,
                                                    memory_order_relaxed));
    for (struct dso* p = head; true; p = p->next) {
        if (!atomic_flag_test_and_set_explicit(
                &p->logged, memory_order_relaxed))
            log_write(p->build_id_log.iov_base, p->build_id_log.iov_len);
        if ((struct dso*)last_unlogged == p)
            break;
    }
}

__NO_SAFESTACK NO_ASAN static mx_status_t map_library(mx_handle_t vmo,
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
    if (status != MX_OK)
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
        if (status != MX_OK)
            goto error;
        if (l != phsize)
            goto noexec;
        ph = ph0 = buf.phdrs;
    } else {
        ph = ph0 = (void*)((char*)&buf + eh->e_phoff);
    }
    const Phdr* first_note = NULL;
    const Phdr* last_note = NULL;
    for (i = eh->e_phnum; i; i--, ph = (void*)((char*)ph + eh->e_phentsize)) {
        switch (ph->p_type) {
        case PT_LOAD:
            nsegs++;
            if (ph->p_vaddr < addr_min) {
                addr_min = ph->p_vaddr;
            }
            if (ph->p_vaddr + ph->p_memsz > addr_max) {
                addr_max = ph->p_vaddr + ph->p_memsz;
            }
            break;
        case PT_DYNAMIC:
            dyn = ph->p_vaddr;
            break;
        case PT_TLS:
            tls_image = ph->p_vaddr;
            dso->tls.align = ph->p_align;
            dso->tls.len = ph->p_filesz;
            dso->tls.size = ph->p_memsz;
            break;
        case PT_GNU_RELRO:
            dso->relro_start = ph->p_vaddr & -PAGE_SIZE;
            dso->relro_end = (ph->p_vaddr + ph->p_memsz) & -PAGE_SIZE;
            break;
        case PT_NOTE:
            if (first_note == NULL)
                first_note = ph;
            last_note = ph;
            break;
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
    if (status != MX_OK) {
        error("failed to reserve %zu bytes of address space: %d\n",
              map_len, status);
        goto error;
    }

    char vmo_name[MX_MAX_NAME_LEN];
    if (_mx_object_get_property(vmo, MX_PROP_NAME,
                                vmo_name, sizeof(vmo_name)) != MX_OK ||
        vmo_name[0] == '\0')
        memcpy(vmo_name, VMO_NAME_UNKNOWN, sizeof(VMO_NAME_UNKNOWN));

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
        this_max = (ph->p_vaddr + ph->p_memsz + PAGE_SIZE - 1) & -PAGE_SIZE;
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
                if (status == MX_OK) {
                    char name[MX_MAX_NAME_LEN] = VMO_NAME_PREFIX_BSS;
                    memcpy(&name[sizeof(VMO_NAME_PREFIX_BSS) - 1], vmo_name,
                           MX_MAX_NAME_LEN - sizeof(VMO_NAME_PREFIX_BSS));
                    _mx_object_set_property(map_vmo, MX_PROP_NAME,
                                            name, strlen(name));
                }
            } else {
                // Get a writable (lazy) copy of the portion of the file VMO.
                status = _mx_vmo_clone(vmo, MX_VMO_CLONE_COPY_ON_WRITE,
                                       off_start, data_size, &map_vmo);
                if (status == MX_OK && map_size > data_size) {
                    // Extend the writable VMO to cover the .bss pages too.
                    // These pages will be zero-filled, not copied from the
                    // file VMO.
                    status = _mx_vmo_set_size(map_vmo, map_size);
                    if (status != MX_OK) {
                        _mx_handle_close(map_vmo);
                        goto error;
                    }
                }
                if (status == MX_OK) {
                    char name[MX_MAX_NAME_LEN] = VMO_NAME_PREFIX_DATA;
                    memcpy(&name[sizeof(VMO_NAME_PREFIX_DATA) - 1], vmo_name,
                           MX_MAX_NAME_LEN - sizeof(VMO_NAME_PREFIX_DATA));
                    _mx_object_set_property(map_vmo, MX_PROP_NAME,
                                            name, strlen(name));
                }
            }
            if (status != MX_OK)
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
        if (status != MX_OK)
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

    for (const Phdr* seg = first_note; seg <= last_note; ++seg) {
        if (seg->p_type == PT_NOTE && find_buildid_note(dso, seg))
            break;
    }

    return MX_OK;
noexec:
    // We overload this to translate into ENOEXEC later.
    status = MX_ERR_WRONG_TYPE;
error:
    if (map != MAP_FAILED)
        unmap_library(dso);
    if (dso->vmar != MX_HANDLE_INVALID)
        _mx_handle_close(dso->vmar);
    return status;
}

__NO_SAFESTACK NO_ASAN static void decode_dyn(struct dso* p) {
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
    // First see if it's in the general list.
    struct dso* p = find_library_in(head, name);
    if (p == NULL && detached_head != NULL) {
        // ldso is not in the list yet, so the first search didn't notice
        // anything that is only a dependency of ldso, i.e. the vDSO.
        // See if the lookup by name matches ldso or its dependencies.
        p = find_library_in(detached_head, name);
        if (p == &ldso) {
            // If something depends on libc (&ldso), we actually want
            // to pull in the entire detached list in its existing
            // order (&ldso is always last), so that libc stays after
            // its own dependencies.
            detached_head->prev = tail;
            tail->next = detached_head;
            tail = p;
            detached_head = NULL;
        } else if (p != NULL) {
            // Take it out of its place in the list rooted at detached_head.
            if (p->prev != NULL)
                p->prev->next = p->next;
            else
                detached_head = p->next;
            if (p->next != NULL) {
                p->next->prev = p->prev;
                p->next = NULL;
            }
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
                                NULL, NULL) == MX_OK) {
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
    // TODO(MG-519): Switch to official tracing mechanism when ready.
    static int seqno;
    debugmsg("@trace_load: %" PRIu64 ":%da %p %p %p",
             pid, seqno, p->base, p->map, p->map + p->map_len);
    debugmsg("@trace_load: %" PRIu64 ":%db %s",
             pid, seqno, buildid);
    debugmsg("@trace_load: %" PRIu64 ":%dc %s %s",
             pid, seqno, soname, name);
    ++seqno;
}

__NO_SAFESTACK static void do_tls_layout(struct dso* p,
                                         char* tls_buffer, int n_th) {
    if (p->tls.size == 0)
        return;

    p->tls_id = ++tls_cnt;
    tls_align = MAXP2(tls_align, p->tls.align);
#ifdef TLS_ABOVE_TP
    p->tls.offset =
        tls_offset +
        ((tls_align - 1) & -(tls_offset + (uintptr_t)p->tls.image));
    tls_offset += p->tls.size;
#else
    tls_offset += p->tls.size + p->tls.align - 1;
    tls_offset -= (tls_offset + (uintptr_t)p->tls.image) & (p->tls.align - 1);
    p->tls.offset = tls_offset;
#endif

    if (tls_buffer != NULL) {
        p->new_dtv = (void*)(-sizeof(size_t) &
                             (uintptr_t)(tls_buffer + sizeof(size_t)));
        p->new_tls = (void*)(p->new_dtv + n_th * (tls_cnt + 1));
    }

    if (tls_tail)
        tls_tail->next = &p->tls;
    else
        libc.tls_head = &p->tls;
    tls_tail = &p->tls;
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
        return MX_OK;
    }

    mx_status_t status = map_library(vmo, &temp_dso);
    if (status != MX_OK)
        return status;

    decode_dyn(&temp_dso);
    if (temp_dso.soname != NULL) {
        // Now check again if we opened the same file a second time.
        // That is, a file with the same DT_SONAME string.
        p = find_library(temp_dso.soname);
        if (p != NULL) {
            unmap_library(&temp_dso);
            *loaded = p;
            return MX_OK;
        }
    }

    if (name == NULL) {
        // If this was loaded by VMO rather than by name, then insist that
        // it have a SONAME.
        name = temp_dso.soname;
        if (name == NULL) {
            unmap_library(&temp_dso);
            return MX_ERR_WRONG_TYPE;
        }
    }

    // Calculate how many slots are needed for dependencies.
    size_t ndeps = 1;  // Account for a NULL terminator.
    for (size_t i = 0; temp_dso.dynv[i].d_tag; i++) {
        if (temp_dso.dynv[i].d_tag == DT_NEEDED)
            ++ndeps;
    }

    /* Allocate storage for the new DSO. When there is TLS, this
     * storage must include a reservation for all pre-existing
     * threads to obtain copies of both the new TLS, and an
     * extended DTV capable of storing an additional slot for
     * the newly-loaded DSO. */
    size_t namelen = strlen(name) + 1;
    size_t build_id_log_len = build_id_log_size(&temp_dso, namelen - 1);
    alloc_size = (sizeof *p + ndeps * sizeof(p->deps[0]) +
                  namelen + build_id_log_len);
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
        return MX_ERR_NO_MEMORY;
    }
    *p = temp_dso;
    p->refcnt = 1;
    p->needed_by = needed_by;
    p->name = (void*)&p->buf[ndeps];
    memcpy(p->name, name, namelen);
    format_build_id_log(p, p->name + namelen, p->name, namelen);
    if (runtime)
        do_tls_layout(p, p->name + namelen + build_id_log_len, n_th);

    tail->next = p;
    p->prev = tail;
    tail = p;

    *loaded = p;
    return MX_OK;
}

__NO_SAFESTACK static mx_status_t load_library(const char* name, int rtld_mode,
                                               struct dso* needed_by,
                                               struct dso** loaded) {
    if (!*name)
        return MX_ERR_INVALID_ARGS;

    *loaded = find_library(name);
    if (*loaded != NULL)
        return MX_OK;

    mx_handle_t vmo;
    mx_status_t status = get_library_vmo(name, &vmo);
    if (status == MX_OK) {
        status = load_library_vmo(vmo, name, rtld_mode, needed_by, loaded);
        _mx_handle_close(vmo);
    }

    return status;
}

__NO_SAFESTACK static void load_deps(struct dso* p) {
    for (; p; p = p->next) {
        struct dso** deps = NULL;
        // The two preallocated DSOs don't get space allocated for ->deps.
        if (runtime && p->deps == NULL && p != &ldso && p != &vdso)
            deps = p->deps = p->buf;
        for (size_t i = 0; p->dynv[i].d_tag; i++) {
            if (p->dynv[i].d_tag != DT_NEEDED)
                continue;
            const char* name = p->strings + p->dynv[i].d_un.d_val;
            struct dso* dep;
            mx_status_t status = load_library(name, 0, p, &dep);
            if (status != MX_OK) {
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

__NO_SAFESTACK NO_ASAN static void reloc_all(struct dso* p) {
    size_t dyn[DYN_CNT];
    for (; p; p = p->next) {
        if (p->relocated)
            continue;
        decode_vec(p->dynv, dyn, DYN_CNT);
        do_relocs(p, laddr(p, dyn[DT_JMPREL]), dyn[DT_PLTRELSZ], 2 + (dyn[DT_PLTREL] == DT_RELA));
        do_relocs(p, laddr(p, dyn[DT_REL]), dyn[DT_RELSZ], 2);
        do_relocs(p, laddr(p, dyn[DT_RELA]), dyn[DT_RELASZ], 3);

        if (head != &ldso && p->relro_start != p->relro_end) {
            mx_status_t status =
                _mx_vmar_protect(p->vmar,
                                 (uintptr_t)laddr(p, p->relro_start),
                                 p->relro_end - p->relro_start,
                                 MX_VM_FLAG_PERM_READ);
            if (status == MX_ERR_BAD_HANDLE &&
                p == &ldso && p->vmar == MX_HANDLE_INVALID) {
                debugmsg("No VMAR_LOADED handle received;"
                         " cannot protect RELRO for %s\n",
                         p->name);
            } else if (status != MX_OK) {
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
        if (p->vmar != MX_HANDLE_INVALID) {
            _mx_handle_close(p->vmar);
            p->vmar = MX_HANDLE_INVALID;
        }

        p->relocated = 1;
    }
}

__NO_SAFESTACK NO_ASAN static void kernel_mapped_dso(struct dso* p) {
    size_t min_addr = -1, max_addr = 0, cnt;
    const Phdr* ph = p->phdr;
    for (cnt = p->phnum; cnt--; ph = (void*)((char*)ph + p->phentsize)) {
        switch (ph->p_type) {
        case PT_LOAD:
            if (ph->p_vaddr < min_addr)
                min_addr = ph->p_vaddr;
            if (ph->p_vaddr + ph->p_memsz > max_addr)
                max_addr = ph->p_vaddr + ph->p_memsz;
            break;
        case PT_DYNAMIC:
            p->dynv = laddr(p, ph->p_vaddr);
            break;
        case PT_GNU_RELRO:
            p->relro_start = ph->p_vaddr & -PAGE_SIZE;
            p->relro_end = (ph->p_vaddr + ph->p_memsz) & -PAGE_SIZE;
            break;
        case PT_NOTE:
            if (p->build_id_note == NULL)
                find_buildid_note(p, ph);
            break;
        }
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

    if (v[0] <= (size_t)self->head.dtv[0]) {
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
    return mem + v[1] + DTP_OFFSET;
}

__NO_SAFESTACK struct pthread* __init_main_thread(mx_handle_t thread_self) {
    pthread_attr_t attr = DEFAULT_PTHREAD_ATTR;
    attr._a_stacksize = libc.stack_size;

    char thread_self_name[MX_MAX_NAME_LEN];
    if (_mx_object_get_property(thread_self, MX_PROP_NAME, thread_self_name,
                                sizeof(thread_self_name)) != MX_OK)
        strcpy(thread_self_name, "(initial-thread)");
    pthread_t td = __allocate_thread(&attr, thread_self_name, NULL);
    if (td == NULL) {
        debugmsg("No memory for %zu bytes thread-local storage.\n",
                 libc.tls_size);
        _exit(127);
    }

    mx_status_t status = mxr_thread_adopt(thread_self, &td->mxr_thread);
    if (status != MX_OK)
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

__NO_SAFESTACK NO_ASAN __attribute__((__visibility__("hidden")))
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
        tail = ldso.next = &vdso;
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
        if (R_TYPE(rel[1]) != REL_RELATIVE)
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
    // First load our own dependencies.  Usually this will be just the
    // vDSO, which is already loaded, so there will be nothing to do.
    // In a sanitized build, we'll depend on the sanitizer runtime DSO
    // and load that now (and its dependencies, such as the unwinder).
    load_deps(&ldso);

    // Now reorder the list so that we appear last, after all our
    // dependencies.  This ensures that e.g. the sanitizer runtime's
    // malloc will be chosen over ours, even if the application
    // doesn't itself depend on the sanitizer runtime SONAME.
    ldso.next->prev = NULL;
    detached_head = ldso.next;
    ldso.prev = tail;
    ldso.next = NULL;
    tail->next = &ldso;

    static struct dso app;

    if (argc < 1 || argv[0] == NULL) {
        static const char* dummy_argv0 = "";
        argv = (char**)&dummy_argv0;
    }

    libc.page_size = PAGE_SIZE;

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

    mx_status_t status = map_library(exec_vmo, &app);
    _mx_handle_close(exec_vmo);
    if (status != MX_OK) {
        debugmsg("%s: %s: Not a valid dynamic program (%s)\n",
                 ldso.name, argv[0], _mx_status_get_string(status));
        _exit(1);
    }

    app.name = argv[0];

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

    // Format the build ID log lines for the three special cases.
    allocate_and_format_build_id_log(&ldso);
    allocate_and_format_build_id_log(&vdso);
    allocate_and_format_build_id_log(&app);

    /* Initial dso chain consists only of the app. */
    head = tail = &app;

    // Load preload/needed libraries, add their symbols to the global
    // namespace, and perform all remaining relocations.
    //
    // Do TLS layout for DSOs after loading, but before relocation.
    // This needs to be after the main program's TLS setup (just
    // above), which has to be the first since it can use static TLS
    // offsets (local-exec TLS model) that are presumed to start at
    // the beginning of the static TLS block.  But we may have loaded
    // some libraries (sanitizer runtime) before that, so we don't do
    // each library's TLS setup directly in load_library_vmo.

    if (ld_preload)
        load_preload(ld_preload);
    load_deps(&app);

    app.global = 1;
    for (struct dso* p = app.next; p != NULL; p = p->next) {
        p->global = 1;
        do_tls_layout(p, NULL, 0);
    }

    for (size_t i = 0; app.dynv[i].d_tag; i++) {
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

    /* Switch to runtime mode: any further failures in the dynamic
     * linker are a reportable failure rather than a fatal startup
     * error. */
    runtime = 1;

    atomic_init(&unlogged_tail, (uintptr_t)tail);

    debug.ver = 1;
    debug.bp = dl_debug_state;
    debug.head = head;
    debug.base = ldso.base;
    debug.state = 0;

    status = _mx_object_set_property(__magenta_process_self,
                                     MX_PROP_PROCESS_DEBUG_ADDR,
                                     &_dl_debug_addr, sizeof(_dl_debug_addr));
    if (status != MX_OK) {
        // Bummer. Crashlogger backtraces, debugger sessions, etc. will be
        // problematic, but this isn't fatal.
        // TODO(dje): Is there a way to detect we're here because of being
        // an injected process (launchpad_start_injected)? IWBN to print a
        // warning here but launchpad_start_injected can trigger this.
    }

    _dl_debug_state();

    if (log_libs)
        _dl_log_unlogged();

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
    for (size_t i = 0; i < app.phnum; i++) {
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

__NO_SAFESTACK NO_ASAN static dl_start_return_t __dls3(void* start_arg) {
    mx_handle_t bootstrap = (uintptr_t)start_arg;

    uint32_t nbytes, nhandles;
    mx_status_t status = mxr_message_size(bootstrap, &nbytes, &nhandles);
    if (status != MX_OK) {
        error("mxr_message_size bootstrap handle %#x failed: %d (%s)",
              bootstrap, status, _mx_status_get_string(status));
        nbytes = nhandles = 0;
    }

    MXR_PROCESSARGS_BUFFER(buffer, nbytes);
    mx_handle_t handles[nhandles];
    mx_proc_args_t* procargs;
    uint32_t* handle_info;
    if (status == MX_OK)
        status = mxr_processargs_read(bootstrap, buffer, nbytes,
                                      handles, nhandles,
                                      &procargs, &handle_info);
    if (status != MX_OK) {
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
    if (status == MX_OK)
        __environ = envp;

    // At this point we can make system calls and have our essential
    // handles, so things are somewhat normal.
    early_init();

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

// Do sanitizer setup and whatever else must be done before dls3.
__NO_SAFESTACK NO_ASAN static void early_init(void) {
#if __has_feature(address_sanitizer)
    __asan_early_init();
    // Inform the loader service that we prefer ASan-supporting libraries.
    loader_svc_config("asan");
#endif
}

static void set_global(struct dso* p, int global) {
    if (p->global > 0)
        // Short-circuit if it's already fully global.  Its deps will be too.
        return;
    p->global = global;
    if (p->deps != NULL) {
        for (struct dso **dep = p->deps; *dep != NULL; ++dep) {
            set_global(*dep, global);
        }
    }
}

static void* dlopen_internal(mx_handle_t vmo, const char* file, int mode) {
    pthread_rwlock_wrlock(&lock);
    __thread_allocation_inhibit();

    struct dso* orig_tail = tail;

    struct dso* p;
    mx_status_t status = (vmo != MX_HANDLE_INVALID ?
                          load_library_vmo(vmo, file, mode, head, &p) :
                          load_library(file, mode, head, &p));

    if (status != MX_OK) {
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

    jmp_buf jb;
    rtld_fail = &jb;
    if (setjmp(*rtld_fail)) {
        /* Clean up anything new that was (partially) loaded */
        if (p && p->deps)
            set_global(p, 0);
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
        set_global(p, -1);
        reloc_all(p);
        set_global(p, 0);
    }

    if (mode & RTLD_GLOBAL) {
        set_global(p, 1);
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

    // The next _dl_log_unlogged can safely read the 'struct dso' list from
    // head up through new_tail.  Most fields will never change again.
    atomic_store_explicit(&unlogged_tail, (uintptr_t)new_tail,
                          memory_order_release);

    pthread_rwlock_unlock(&lock);

    if (log_libs)
        _dl_log_unlogged();

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

static bool find_sym_for_dlsym(struct dso* p,
                               const char* name,
                               uint32_t* name_gnu_hash,
                               uint32_t* name_sysv_hash,
                               void** result) {
    const Sym* sym;
    if (p->ghashtab != NULL) {
        if (*name_gnu_hash == 0)
            *name_gnu_hash = gnu_hash(name);
        sym = gnu_lookup(*name_gnu_hash, p->ghashtab, p, name);
    } else {
        if (*name_sysv_hash == 0)
            *name_sysv_hash = sysv_hash(name);
        sym = sysv_lookup(name, *name_sysv_hash, p);
    }
    if (sym && (sym->st_info & 0xf) == STT_TLS) {
        *result = __tls_get_addr((size_t[]){p->tls_id, sym->st_value});
        return true;
    }
    if (sym && sym->st_value && (1 << (sym->st_info & 0xf) & OK_TYPES)) {
        *result = laddr(p, sym->st_value);
        return true;
    }
    if (p->deps) {
        for (struct dso** dep = p->deps; *dep != NULL; ++dep) {
            if (find_sym_for_dlsym(*dep, name, name_gnu_hash, name_sysv_hash,
                                   result))
                return true;
        }
    }
    return false;
}

static void* do_dlsym(struct dso* p, const char* s, void* ra) {
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
    uint32_t gnu_hash = 0, sysv_hash = 0;
    void* result;
    if (find_sym_for_dlsym(p, s, &gnu_hash, &sysv_hash, &result))
        return result;
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
                                                 mx_handle_t request_handle,
                                                 mx_handle_t* result) {
    // Use a static buffer rather than one on the stack to avoid growing
    // the stack size too much.  Calls to this function are always
    // serialized anyway, so there is no danger of collision.
    static struct {
        mx_loader_svc_msg_t header;
        uint8_t data[LOADER_SVC_MSG_MAX - sizeof(mx_loader_svc_msg_t)];
    } msg;

    loader_svc_rpc_in_progress = true;

    mx_status_t status;
    if (len >= sizeof msg.data) {
        _mx_handle_close(request_handle);
        error("message of %zu bytes too large for loader service protocol",
              len);
        status = MX_ERR_OUT_OF_RANGE;
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
        .wr_handles = &request_handle,
        .wr_num_handles = request_handle == MX_HANDLE_INVALID ? 0 : 1,
        .rd_bytes = &msg,
        .rd_num_bytes = sizeof(msg),
        .rd_handles = result,
        .rd_num_handles = result == NULL ? 0 : 1,
    };

    uint32_t reply_size;
    uint32_t handle_count;
    mx_status_t read_status = MX_OK;
    status = _mx_channel_call(loader_svc, 0, MX_TIME_INFINITE,
                              &call, &reply_size, &handle_count,
                              &read_status);
    if (status != MX_OK) {
        error("_mx_channel_call of %u bytes to loader service: "
              "%d (%s), read %d (%s)",
              call.wr_num_bytes, status, _mx_status_get_string(status),
              read_status, _mx_status_get_string(read_status));
        if (status != MX_ERR_CALL_FAILED)
            _mx_handle_close(request_handle);
        else if (read_status != MX_OK)
            status = read_status;
        goto out;
    }

    if (reply_size != sizeof(msg.header)) {
        error("loader service reply %u bytes != %u",
              reply_size, sizeof(msg.header));
        status = MX_ERR_INVALID_ARGS;
        goto out;
    }
    if (msg.header.opcode != LOADER_SVC_OP_STATUS) {
        if (handle_count > 0) {
            _mx_handle_close(*result);
            *result = MX_HANDLE_INVALID;
        }
        error("loader service reply opcode %u != %u",
              msg.header.opcode, LOADER_SVC_OP_STATUS);
        status = MX_ERR_INVALID_ARGS;
        goto out;
    }
    if (msg.header.arg != MX_OK) {
        // |result| is non-null if |handle_count| > 0, because
        // |handle_count| <= |rd_num_handles|.
        if (handle_count > 0 && *result != MX_HANDLE_INVALID) {
            error("loader service error %d reply contains handle %#x",
                  msg.header.arg, *result);
            status = MX_ERR_INVALID_ARGS;
            goto out;
        }
        status = msg.header.arg;
    }

out:
    loader_svc_rpc_in_progress = false;
    return status;
}

__NO_SAFESTACK static void loader_svc_config(const char* config) {
    mx_status_t status = loader_svc_rpc(LOADER_SVC_OP_CONFIG,
                                        config, strlen(config),
                                        MX_HANDLE_INVALID, NULL);
    if (status != MX_OK)
        debugmsg("LOADER_SVC_OP_CONFIG(%s): %s\n",
                 config, _mx_status_get_string(status));
}

__NO_SAFESTACK static mx_status_t get_library_vmo(const char* name,
                                                  mx_handle_t* result) {
    if (loader_svc == MX_HANDLE_INVALID) {
        error("cannot look up \"%s\" with no loader service", name);
        return MX_ERR_UNAVAILABLE;
    }
    return loader_svc_rpc(LOADER_SVC_OP_LOAD_OBJECT, name, strlen(name),
                          MX_HANDLE_INVALID, result);
}

__NO_SAFESTACK mx_status_t dl_clone_loader_sevice(mx_handle_t* out) {
    if (loader_svc == MX_HANDLE_INVALID) {
        return MX_ERR_UNAVAILABLE;
    }
    mx_handle_t h0, h1;
    mx_status_t status;
    if ((status = _mx_channel_create(0, &h0, &h1)) != MX_OK) {
        return status;
    }
    if ((status = loader_svc_rpc(LOADER_SVC_OP_CLONE, NULL, 0, h1, NULL)) != MX_OK) {
        _mx_handle_close(h0);
    } else {
        *out = h0;
    }
    return status;
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
        status = loader_svc_rpc(LOADER_SVC_OP_DEBUG_PRINT, buf, len,
                                MX_HANDLE_INVALID, NULL);
    else {
        int n = _mx_debug_write(buf, len);
        status = n < 0 ? n : MX_OK;
    }
    if (status != MX_OK)
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

// We piggy-back on the loader service to publish data from sanitizers.
void __sanitizer_publish_data(const char* sink_name, mx_handle_t vmo) {
    pthread_rwlock_rdlock(&lock);
    mx_status_t status = loader_svc_rpc(LOADER_SVC_OP_PUBLISH_DATA_SINK,
                                        sink_name, strlen(sink_name),
                                        vmo, NULL);
    if (status != MX_OK) {
        // TODO(mcgrathr): Send this whereever sanitizer logging goes.
        debugmsg("Failed to publish data sink \"%s\" (%s): %s\n",
                 sink_name, _mx_status_get_string(status), dlerror());
    }
    pthread_rwlock_unlock(&lock);
}

// ... and to get configuration files for them.
mx_status_t __sanitizer_get_configuration(const char* name,
                                          mx_handle_t *out_vmo) {
    pthread_rwlock_rdlock(&lock);
    mx_status_t status = loader_svc_rpc(LOADER_SVC_OP_LOAD_DEBUG_CONFIG,
                                        name, strlen(name),
                                        MX_HANDLE_INVALID, out_vmo);
    if (status != MX_OK) {
        // TODO(mcgrathr): Send this whereever sanitizer logging goes.
        debugmsg("Failed to get configuration file \"%s\" (%s): %s\n",
                 name, _mx_status_get_string(status), dlerror());
    }
    pthread_rwlock_unlock(&lock);
    return status;
}

#ifdef __clang__
// Under -fsanitize-coverage, the startup code path before __dls3 cannot
// use PLT calls, so its calls to the sancov hook are a problem.  We use
// some assembler chicanery to redirect those calls to the local symbol
// _dynlink_sancov_trampoline.  Since the target of the PLT relocs is
// local, the linker will elide the PLT entry and resolve the calls
// directly to our definition.  The trampoline checks the 'runtime' flag to
// distinguish calls before final relocation is complete, and only calls
// into the sanitizer runtime once it's actually up.  Because of the
// .weakref chicanery, _dynlink_sancov_trace_pc_guard must be in a separate
// assembly file.
__asm__(".weakref __sanitizer_cov_trace_pc_guard, _dynlink_sancov_trampoline");
__asm__(".hidden _dynlink_sancov_trace_pc_guard");
__asm__(".pushsection .text._dynlink_sancov_trampoline,\"ax\",%progbits\n"
        ".local _dynlink_sancov_trampoline\n"
        ".type _dynlink_sancov_trampoline,%function\n"
        "_dynlink_sancov_trampoline:\n"
# ifdef __x86_64__
        "cmpl $0, _dynlink_runtime(%rip)\n"
        "jne _dynlink_sancov_trace_pc_guard\n"
        "ret\n"
# elif defined(__aarch64__)
        "adrp x16, _dynlink_runtime\n"
        "ldr w16, [x16, #:lo12:_dynlink_runtime]\n"
        "cbnz w16, _dynlink_sancov_trace_pc_guard\n"
        "ret\n"
# else
#  error unsupported architecture
# endif
        ".size _dynlink_sancov_trampoline, . - _dynlink_sancov_trampoline\n"
        ".popsection");
#endif
