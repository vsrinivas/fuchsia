#define _GNU_SOURCE
#include "dynlink.h"
#include "libc.h"
#include "malloc_impl.h"
#include "pthread_impl.h"
#include "stdio_impl.h"
#include "tls_impl.h"
#include <ctype.h>
#include <dlfcn.h>
#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <link.h>
#include <magenta/dlfcn.h>
#include <pthread.h>
#include <setjmp.h>
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
#include <runtime/status.h>
#include <runtime/thread.h>

static void error(const char*, ...);
static void debugmsg(const char*, ...);
static mx_handle_t get_library_vmo(const char* name);

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

struct td_index {
    size_t args[2];
    struct td_index* next;
};

struct dso {
    // These five fields match struct link_map in <link.h>.
    // TODO(mcgrathr): Use the type here.
    unsigned char* base;
    char* name;
    size_t* dynv;
    struct dso *next, *prev;

    const char* soname;
    Phdr* phdr;
    int phnum;
    size_t phentsize;
    int refcnt;
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
    volatile int new_dtv_idx, new_tls_idx;
    struct td_index* td_index;
    struct dso* fini_next;
    struct funcdesc {
        void* addr;
        size_t* got;
    } * funcdescs;
    size_t* got;
    char buf[];
};

struct symdef {
    Sym* sym;
    struct dso* dso;
};

void __init_tp(pthread_t);
pthread_t __copy_tls(unsigned char*);

static struct builtin_tls {
    char c;
    struct pthread pt;
    void* space[16];
} builtin_tls[1];
#define MIN_TLS_ALIGN offsetof(struct builtin_tls, pt)

#define ADDEND_LIMIT 4096
static size_t *saved_addends, *apply_addends_to;

static struct dso ldso, vdso;
static struct dso *head, *tail, *fini_head;
static unsigned long long gencnt;
static int runtime;
static int ldd_mode;
static int ldso_fail;
static int noload;
static jmp_buf* rtld_fail;
static pthread_rwlock_t lock;
static struct debug debug;
static struct tls_module* tls_tail;
static size_t tls_cnt, tls_offset, tls_align = MIN_TLS_ALIGN;
static size_t static_tls_cnt;
static pthread_mutex_t init_fini_lock = {._m_type = PTHREAD_MUTEX_RECURSIVE};

static mx_handle_t loader_svc = MX_HANDLE_INVALID;
static mx_handle_t logger = MX_HANDLE_INVALID;

struct debug* _dl_debug_addr = &debug;

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

/* Compute load address for a virtual address in a given dso. */
#define laddr(p, v) (void*)((p)->base + (v))
#define fpaddr(p, v) ((void (*)(void))laddr(p, v))

static void decode_vec(size_t* v, size_t* a, size_t cnt) {
    size_t i;
    for (i = 0; i < cnt; i++)
        a[i] = 0;
    for (; v[0]; v += 2)
        if (v[0] - 1 < cnt - 1) {
            a[0] |= 1UL << v[0];
            a[v[0]] = v[1];
        }
}

static int search_vec(size_t* v, size_t* r, size_t key) {
    for (; v[0] != key; v += 2)
        if (!v[0])
            return 0;
    *r = v[1];
    return 1;
}

static uint32_t sysv_hash(const char* s0) {
    const unsigned char* s = (void*)s0;
    uint_fast32_t h = 0;
    while (*s) {
        h = 16 * h + *s++;
        h ^= h >> 24 & 0xf0;
    }
    return h & 0xfffffff;
}

static uint32_t gnu_hash(const char* s0) {
    const unsigned char* s = (void*)s0;
    uint_fast32_t h = 5381;
    for (; *s; s++)
        h += h * 32 + *s;
    return h;
}

static Sym* sysv_lookup(const char* s, uint32_t h, struct dso* dso) {
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

static Sym* gnu_lookup(uint32_t h1, uint32_t* hashtab, struct dso* dso, const char* s) {
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

static Sym* gnu_lookup_filtered(uint32_t h1, uint32_t* hashtab, struct dso* dso, const char* s,
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

static struct symdef find_sym(struct dso* dso, const char* s, int need_def) {
    uint32_t h = 0, gh, gho, *ght;
    size_t ghm = 0;
    struct symdef def = {0};
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

static void do_relocs(struct dso* dso, size_t* rel, size_t rel_size, size_t stride) {
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
                struct td_index* new = malloc(sizeof *new);
                if (!new) {
                    error("Error relocating %s: cannot allocate TLSDESC for %s", dso->name,
                          sym ? name : "(local)");
                    longjmp(*rtld_fail, 1);
                }
                new->next = dso->td_index;
                dso->td_index = new;
                new->args[0] = def.dso->tls_id;
                new->args[1] = tls_val + addend;
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

/* A huge hack: to make up for the wastefulness of shared libraries
 * needing at least a page of dirty memory even if they have no global
 * data, we reclaim the gaps at the beginning and end of writable maps
 * and "donate" them to the heap. */

static void reclaim(struct dso* dso, size_t start, size_t end) {
    if (start >= dso->relro_start && start < dso->relro_end)
        start = dso->relro_end;
    if (end >= dso->relro_start && end < dso->relro_end)
        end = dso->relro_start;
    __donate_heap(laddr(dso, start), laddr(dso, end));
}

static void reclaim_gaps(struct dso* dso) {
    Phdr* ph = dso->phdr;
    size_t phcnt = dso->phnum;

    for (; phcnt--; ph = (void*)((char*)ph + dso->phentsize)) {
        if (ph->p_type != PT_LOAD)
            continue;
        if ((ph->p_flags & (PF_R | PF_W)) != (PF_R | PF_W))
            continue;
        reclaim(dso, ph->p_vaddr & -PAGE_SIZE, ph->p_vaddr);
        reclaim(dso, ph->p_vaddr + ph->p_memsz,
                ph->p_vaddr + ph->p_memsz + PAGE_SIZE - 1 & -PAGE_SIZE);
    }
}

static void unmap_library(struct dso* dso) {
    if (dso->map && dso->map_len) {
        munmap(dso->map, dso->map_len);
    }
}

static void* choose_load_address(size_t span) {
    // vm_map requires some vm_object handle, so create a dummy one.
    mx_handle_t vmo = _mx_vmo_create(0);

    // Do a mapping to let the kernel choose an address range.
    // TODO(MG-161): This really ought to be a no-access mapping (PROT_NONE
    // in POSIX terms).  But the kernel currently doesn't allow that, so do
    // a read-only mapping.
    uintptr_t base;
    mx_status_t status = _mx_process_map_vm(libc.proc, vmo, 0, span, &base,
                                            MX_VM_FLAG_PERM_READ);
    _mx_handle_close(vmo);
    if (status < 0) {
        error("failed to reserve %zu bytes of address space: %d\n",
              span, status);
        errno = ENOMEM;
        return MAP_FAILED;
    }

    // TODO(MG-133): Really we should just leave the no-access mapping in
    // place and let each PT_LOAD mapping overwrite it.  But the kernel
    // currently doesn't allow splitting an existing mapping to overwrite
    // part of it.  So we remove the address-reserving mapping before
    // starting on the actual PT_LOAD mappings.  NOTE! THIS IS RACY!
    // That is, in the general case of dlopen when there are multiple
    // threads, it's racy.  For the startup case (or any time when there
    // is only one thread), it's fine.
    status = _mx_process_unmap_vm(libc.proc, base, 0);
    if (status < 0) {
        error("vm_unmap failed on reservation %#" PRIxPTR "+%zu: %d\n",
              base, span, status);
        errno = ENOMEM;
        return MAP_FAILED;
    }

    return (void*)base;
}

// TODO(mcgrathr): Temporary hack to avoid modifying the file VMO.
// This will go away when we have copy-on-write.
static mx_handle_t get_writable_vmo(mx_handle_t vmo, size_t data_size,
                                    off_t* off_start, size_t* map_size) {
    mx_handle_t copy_vmo = _mx_vmo_create(data_size);
    if (copy_vmo < 0)
        return copy_vmo;
    uintptr_t window = 0;
    mx_status_t status = _mx_process_map_vm(libc.proc, vmo,
                                            *off_start, data_size, &window,
                                            MX_VM_FLAG_PERM_READ);
    if (status < 0) {
        _mx_handle_close(copy_vmo);
        return status;
    }
    mx_ssize_t n = _mx_vmo_write(copy_vmo, (void*)window, 0, data_size);
    _mx_process_unmap_vm(libc.proc, window, 0);
    if (n >= 0 && n != (mx_ssize_t)data_size)
        n = ERR_IO;
    if (n < 0) {
        mx_handle_close(copy_vmo);
        return n;
    }
    *off_start = 0;
    *map_size = data_size;
    return copy_vmo;
}

static void* map_library(mx_handle_t vmo, struct dso* dso) {
    Ehdr buf[(896 + sizeof(Ehdr)) / sizeof(Ehdr)];
    void* allocated_buf = 0;
    size_t phsize;
    size_t addr_min = SIZE_MAX, addr_max = 0, map_len;
    size_t this_min, this_max;
    size_t nsegs = 0;
    off_t off_start = 0;
    Ehdr* eh;
    Phdr *ph, *ph0;
    unsigned char *map = MAP_FAILED, *base;
    size_t dyn = 0;
    size_t tls_image = 0;
    size_t i;

    ssize_t l = _mx_vmo_read(vmo, buf, 0, sizeof buf);
    eh = buf;
    if (l < 0)
        return 0;
    if (l < sizeof *eh || (eh->e_type != ET_DYN && eh->e_type != ET_EXEC))
        goto noexec;
    phsize = eh->e_phentsize * eh->e_phnum;
    if (phsize > sizeof buf - sizeof *eh) {
        allocated_buf = malloc(phsize);
        if (!allocated_buf)
            return 0;
        l = _mx_vmo_read(vmo, allocated_buf, eh->e_phoff, phsize);
        if (l < 0)
            goto error;
        if (l != phsize)
            goto noexec;
        ph = ph0 = allocated_buf;
    } else if (eh->e_phoff + phsize > l) {
        l = _mx_vmo_read(vmo, buf + 1, eh->e_phoff, phsize);
        if (l < 0)
            goto error;
        if (l != phsize)
            goto noexec;
        ph = ph0 = (void*)(buf + 1);
    } else {
        ph = ph0 = (void*)((char*)buf + eh->e_phoff);
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
            off_start = ph->p_offset;
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
    off_start &= -PAGE_SIZE;
    map_len = addr_max - addr_min + off_start;
    map = choose_load_address(map_len);
    if (map == MAP_FAILED)
        goto error;
    dso->map = map;
    dso->map_len = map_len;
    /* If the loaded file is not relocatable and the requested address is
     * not available, then the load operation must fail. */
    if (eh->e_type != ET_DYN && addr_min && map != (void*)addr_min) {
        errno = EBUSY;
        goto error;
    }
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
        off_start = ph->p_offset & -PAGE_SIZE;
        uint32_t mx_flags = MX_VM_FLAG_FIXED;
        mx_flags |= (ph->p_flags & PF_R) ? MX_VM_FLAG_PERM_READ : 0;
        mx_flags |= (ph->p_flags & PF_W) ? MX_VM_FLAG_PERM_WRITE : 0;
        mx_flags |= (ph->p_flags & PF_X) ? MX_VM_FLAG_PERM_EXECUTE : 0;
        uintptr_t mapaddr = (uintptr_t)base + this_min;
        mx_handle_t map_vmo = vmo;
        size_t map_size = this_max - this_min;
        if (map_size == 0)
            continue;

        mx_status_t status;
        if (ph->p_flags & PF_W) {
            size_t data_size =
                ((ph->p_vaddr + ph->p_filesz + PAGE_SIZE - 1) & -PAGE_SIZE) -
                this_min;
            if (data_size > 0) {
                map_vmo = get_writable_vmo(vmo, data_size,
                                           &off_start, &map_size);
                if (map_vmo < 0) {
                    status = map_vmo;
                mx_error:
                    // TODO(mcgrathr): Perhaps this should translate the
                    // kernel error in 'status' into an errno value.  Or
                    // perhaps it should just assert that the kernel error
                    // was among an expected set.  Probably all failures of
                    // these kernel calls should either be totally fatal or
                    // should translate into ENOMEM.
                    errno = ENOMEM;
                    goto error;
                }
            }
        }

        status = _mx_process_map_vm(libc.proc, map_vmo, off_start,
                                    map_size, &mapaddr, mx_flags);
        if (map_vmo != vmo)
            _mx_handle_close(map_vmo);
        if (status != NO_ERROR)
            goto mx_error;

        if (ph->p_memsz > ph->p_filesz) {
            size_t brk = (size_t)base + ph->p_vaddr + ph->p_filesz;
            size_t pgbrk = brk + PAGE_SIZE - 1 & -PAGE_SIZE;
            memset((void*)brk, 0, pgbrk - brk & PAGE_SIZE - 1);
            if (pgbrk - (size_t)base < this_max) {
                size_t bss_len = (size_t)base + this_max - pgbrk;
                mx_handle_t bss_vmo = _mx_vmo_create(bss_len);
                if (bss_vmo < 0) {
                    status = bss_vmo;
                    goto mx_error;
                }
                uintptr_t bss_mapaddr = pgbrk;
                status = _mx_process_map_vm(libc.proc, bss_vmo, 0, bss_len,
                                            &bss_mapaddr, mx_flags);
                _mx_handle_close(bss_vmo);
                if (status < 0)
                    goto mx_error;
            }
        }
    }

    dso->base = base;
    dso->dynv = laddr(dso, dyn);
    if (dso->tls.size)
        dso->tls.image = laddr(dso, tls_image);
    if (!runtime)
        reclaim_gaps(dso);
    free(allocated_buf);
    return map;
noexec:
    errno = ENOEXEC;
error:
    if (map != MAP_FAILED)
        unmap_library(dso);
    free(allocated_buf);
    return 0;
}

static void decode_dyn(struct dso* p) {
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

static struct dso* find_library_in(struct dso* p, const char* name) {
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

static struct dso* find_library(const char* name) {
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

static struct dso* load_library_vmo(mx_handle_t vmo, const char* name,
                                    struct dso* needed_by) {
    unsigned char* map;
    struct dso *p, temp_dso = {0};
    size_t alloc_size;
    int n_th = 0;

    map = noload ? 0 : map_library(vmo, &temp_dso);
    if (!map)
        return 0;

    decode_dyn(&temp_dso);
    if (temp_dso.soname != NULL) {
        // Now check again if we opened the same file a second time.
        // That is, a file with the same DT_SONAME string.
        p = find_library(temp_dso.soname);
        if (p != NULL) {
            unmap_library(&temp_dso);
            return p;
        }
    }

    if (name == NULL) {
        // If this was loaded by VMO rather than by name, then insist that
        // it have a SONAME.
        name = temp_dso.soname;
        if (name == NULL) {
            unmap_library(&temp_dso);
            errno = ENOEXEC;
            return NULL;
        }
    }

    /* Allocate storage for the new DSO. When there is TLS, this
     * storage must include a reservation for all pre-existing
     * threads to obtain copies of both the new TLS, and an
     * extended DTV capable of storing an additional slot for
     * the newly-loaded DSO. */
    alloc_size = sizeof *p + strlen(name) + 1;
    if (runtime && temp_dso.tls.image) {
        size_t per_th = temp_dso.tls.size + temp_dso.tls.align + sizeof(void*) * (tls_cnt + 3);
        n_th = atomic_load(&libc.thread_count);
        if (n_th > SSIZE_MAX / per_th)
            alloc_size = SIZE_MAX;
        else
            alloc_size += n_th * per_th;
    }
    p = calloc(1, alloc_size);
    if (!p) {
        unmap_library(&temp_dso);
        return 0;
    }
    memcpy(p, &temp_dso, sizeof temp_dso);
    p->refcnt = 1;
    p->needed_by = needed_by;
    p->name = p->buf;
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

    return p;
}

static struct dso* load_library(const char* name, struct dso* needed_by) {
    if (!*name) {
        errno = EINVAL;
        return 0;
    }

    struct dso* p = find_library(name);
    if (p == NULL) {
        mx_handle_t vmo = get_library_vmo(name);
        if (vmo >= 0) {
            p = load_library_vmo(vmo, name, needed_by);
            _mx_handle_close(vmo);
        }
    }

    return p;
}

static void load_deps(struct dso* p) {
    size_t i, ndeps = 0;
    struct dso ***deps = &p->deps, **tmp, *dep;
    for (; p; p = p->next) {
        for (i = 0; p->dynv[i]; i += 2) {
            if (p->dynv[i] != DT_NEEDED)
                continue;
            dep = load_library(p->strings + p->dynv[i + 1], p);
            if (!dep) {
                error("Error loading shared library %s: %m (needed by %s)",
                      p->strings + p->dynv[i + 1], p->name);
                if (runtime)
                    longjmp(*rtld_fail, 1);
                continue;
            }
            if (runtime) {
                tmp = realloc(*deps, sizeof(*tmp) * (ndeps + 2));
                if (!tmp)
                    longjmp(*rtld_fail, 1);
                tmp[ndeps++] = dep;
                tmp[ndeps] = 0;
                *deps = tmp;
            }
        }
    }
}

static void load_preload(char* s) {
    int tmp;
    char* z;
    for (z = s; *z; s = z) {
        for (; *s && (isspace(*s) || *s == ':'); s++)
            ;
        for (z = s; *z && !isspace(*z) && *z != ':'; z++)
            ;
        tmp = *z;
        *z = 0;
        load_library(s, 0);
        *z = tmp;
    }
}

static void make_global(struct dso* p) {
    for (; p; p = p->next)
        p->global = 1;
}

static void do_mips_relocs(struct dso* p, size_t* got) {
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

static void reloc_all(struct dso* p) {
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

#if 0
        // TODO(mcgrathr): No mprotect yet, and ENOSYS stub not safe because
        // TLS errno here might crash.
        if (head != &ldso && p->relro_start != p->relro_end &&
            mprotect(laddr(p, p->relro_start), p->relro_end - p->relro_start, PROT_READ) &&
            errno != ENOSYS) {
            error("Error relocating %s: RELRO protection failed: %m", p->name);
            if (runtime)
                longjmp(*rtld_fail, 1);
        }
#endif

        p->relocated = 1;
    }
}

static void kernel_mapped_dso(struct dso* p) {
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

void __init_tls(mxr_thread_t* mxr_thread) {}

__attribute__((__visibility__("hidden"))) void* __tls_get_new(size_t* v) {
    pthread_t self = __pthread_self();

    /* Block signals to make accessing new TLS async-signal-safe */
    sigset_t set;
    __block_all_sigs(&set);
    if (v[0] <= (size_t)self->dtv[0]) {
        __restore_sigs(&set);
        return (char*)self->dtv[v[0]] + v[1] + DTP_OFFSET;
    }

    /* This is safe without any locks held because, if the caller
     * is able to request the Nth entry of the DTV, the DSO list
     * must be valid at least that far out and it was synchronized
     * at program startup or by an already-completed call to dlopen. */
    struct dso* p;
    for (p = head; p->tls_id != v[0]; p = p->next)
        ;

    /* Get new DTV space from new DSO if needed */
    if (v[0] > (size_t)self->dtv[0]) {
        void** newdtv = p->new_dtv + (v[0] + 1) * a_fetch_add(&p->new_dtv_idx, 1);
        memcpy(newdtv, self->dtv, ((size_t)self->dtv[0] + 1) * sizeof(void*));
        newdtv[0] = (void*)v[0];
        self->dtv = self->dtv_copy = newdtv;
    }

    /* Get new TLS memory from all new DSOs up to the requested one */
    unsigned char* mem;
    for (p = head;; p = p->next) {
        if (!p->tls_id || self->dtv[p->tls_id])
            continue;
        mem = p->new_tls + (p->tls.size + p->tls.align) * a_fetch_add(&p->new_tls_idx, 1);
        mem += ((uintptr_t)p->tls.image - (uintptr_t)mem) & (p->tls.align - 1);
        self->dtv[p->tls_id] = mem;
        memcpy(mem, p->tls.image, p->tls.len);
        if (p->tls_id == v[0])
            break;
    }
    __restore_sigs(&set);
    return mem + v[1] + DTP_OFFSET;
}

static void update_tls_size(void) {
    libc.tls_cnt = tls_cnt;
    libc.tls_align = tls_align;
    libc.tls_size =
        ALIGN((1 + tls_cnt) * sizeof(void*) + tls_offset + sizeof(struct pthread) + tls_align * 2,
              tls_align);
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

__attribute__((__visibility__("hidden"))) dl_start_return_t __dls2(
    void* start_arg, void* vdso_map) {
    ldso.base = _BASE;

    Ehdr* ehdr = (void*)ldso.base;
    ldso.name = "libc.so";
    ldso.global = 1;
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
        vdso.name = "<vDSO>";
        vdso.global = 1;

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
        a_crash();
    size_t addends[symbolic_rel_cnt + 1];
    saved_addends = addends;

    head = &ldso;
    reloc_all(&ldso);

    ldso.relocated = 0;

    /* Call dynamic linker stage-3, __dls3, looking it up
     * symbolically as a barrier against moving the address
     * load across the above relocation processing. */
    struct symdef dls3_def = find_sym(&ldso, "__dls3", 0);
    return (*(stage3_func*)laddr(&ldso, dls3_def.sym->st_value))(start_arg);
}

/* Stage 3 of the dynamic linker is called with the dynamic linker/libc
 * fully functional. Its job is to load (if not already loaded) and
 * process dependencies and relocations for the main application and
 * transfer control to its entry point. */

static void* dls3(mx_handle_t exec_vmo, int argc, char** argv) {
    static struct dso app;
    size_t i;
    char* env_preload = 0;
    char** argv_orig = argv;

    if (argc < 1 || argv[0] == NULL) {
        static const char* dummy_argv0 = "";
        argv = (char**)&dummy_argv0;
    }

    libc.page_size = PAGE_SIZE;

    /* Setup early thread pointer in builtin_tls for ldso/libc itself to
     * use during dynamic linking. If possible it will also serve as the
     * thread pointer at runtime. */
    libc.tls_size = sizeof builtin_tls;
    libc.tls_align = tls_align;
    __init_tp(__copy_tls((void*)builtin_tls));

    /* Only trust user/env if kernel says we're not suid/sgid */
    bool log_libs = false;
    if (!libc.secure) {
        env_preload = getenv("LD_PRELOAD");

        const char* debug = getenv("LD_DEBUG");
        if (debug != NULL && debug[0] != '\0')
            log_libs = true;
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
                    env_preload = opt + 8;
                else if (opt[7])
                    *argv = 0;
                else if (*argv)
                    env_preload = *argv++;
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

        exec_vmo = get_library_vmo(argv[0]);
        if (exec_vmo < 0) {
            debugmsg("%s: cannot load %s: %d\n", ldname, argv[0], exec_vmo);
            _exit(1);
        }
    }

    Ehdr* ehdr = map_library(exec_vmo, &app);
    _mx_handle_close(exec_vmo);
    if (!ehdr) {
        debugmsg("%s: %s: Not a valid dynamic program\n", ldso.name, argv[0]);
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

    // Donate unused parts of ldso mapping to malloc.
    // map_library already did this for app.
    reclaim_gaps(&ldso);

    /* Load preload/needed libraries, add their symbols to the global
     * namespace, and perform all remaining relocations. */
    if (env_preload)
        load_preload(env_preload);
    load_deps(&app);
    make_global(&app);

    for (i = 0; app.dynv[i]; i += 2) {
        if (!DT_DEBUG_INDIRECT && app.dynv[i] == DT_DEBUG)
            app.dynv[i + 1] = (size_t)&debug;
        if (DT_DEBUG_INDIRECT && app.dynv[i] == DT_DEBUG_INDIRECT) {
            size_t* ptr = (size_t*)app.dynv[i + 1];
            *ptr = (size_t)&debug;
        }
    }

    /* The main program must be relocated LAST since it may contin
     * copy relocations which depend on libraries' relocations. */
    reloc_all(app.next);
    reloc_all(&app);

    update_tls_size();

    if (libc.tls_size > sizeof builtin_tls || tls_align > MIN_TLS_ALIGN) {
        void* initial_tls = calloc(libc.tls_size, 1);
        if (!initial_tls) {
            debugmsg("%s: Error getting %zu bytes thread-local storage: %m\n", argv[0],
                     libc.tls_size);
            _exit(127);
        }
        __init_tp(__copy_tls(initial_tls));
    } else {
        size_t tmp_tls_size = libc.tls_size;
        pthread_t self = __pthread_self();
        /* Temporarily set the tls size to the full size of
         * builtin_tls so that __copy_tls will use the same layout
         * as it did for before. Then check, just to be safe. */
        libc.tls_size = sizeof builtin_tls;
        if (__copy_tls((void*)builtin_tls) != self)
            a_crash();
        libc.tls_size = tmp_tls_size;
    }
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

    // Reset from the argv[0] value so we don't save a dangling pointer
    // into the caller's stack frame.
    app.name = "";

    errno = 0;

    return laddr(&app, ehdr->e_entry);
}

dl_start_return_t __dls3(void* start_arg) {
    mx_handle_t bootstrap = (uintptr_t)start_arg;

    uint32_t nbytes, nhandles;
    mx_status_t status = mxr_message_size(bootstrap, &nbytes, &nhandles);
    if (status != NO_ERROR) {
        error("mxr_message_size bootstrap handle %#x failed: %d (%s)",
              bootstrap, status, mx_strstatus(status));
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
              nbytes, nhandles, bootstrap, status, mx_strstatus(status));
        nbytes = nhandles = 0;
    }

    mx_handle_t exec_vmo = MX_HANDLE_INVALID;
    for (int i = 0; i < nhandles; ++i) {
        switch (MX_HND_INFO_TYPE(handle_info[i])) {
        case MX_HND_TYPE_LOADER_SVC:
            if (loader_svc != MX_HANDLE_INVALID ||
                handles[i] == MX_HANDLE_INVALID) {
                error("bootstrap message bad LOADER_SVC %#x vs %#x",
                      handles[i], loader_svc);
            }
            loader_svc = handles[i];
            break;
        case MX_HND_TYPE_EXEC_VMO:
            if (exec_vmo != MX_HANDLE_INVALID ||
                handles[i] == MX_HANDLE_INVALID) {
                error("bootstrap message bad EXEC_VMO %#x vs %#x",
                      handles[i], exec_vmo);
            }
            exec_vmo = handles[i];
            break;
        case MX_HND_TYPE_MXIO_LOGGER:
            if (logger != MX_HANDLE_INVALID ||
                handles[i] == MX_HANDLE_INVALID) {
                error("bootstrap message bad MXIO_LOGGER %#x vs %#x",
                      handles[i], logger);
            }
            logger = handles[i];
            break;
        case MX_HND_TYPE_PROC_SELF:
            libc.proc = handles[i];
            break;
        default:
            _mx_handle_close(handles[i]);
            break;
        }
    }

    // Unpack the environment strings so dls3 can use getenv.
    char* argv[procargs->args_num + 1];
    char* envp[procargs->environ_num + 1];
    status = mxr_processargs_strings(buffer, nbytes, argv, envp);
    if (status == NO_ERROR)
        __environ = envp;

    void* entry = dls3(exec_vmo, procargs->args_num, argv);

    // Reset it so there's no dangling pointer to this stack frame.
    // Presumably the parent will send the same strings in the main
    // bootstrap message, but that's for __libc_start_main to see.
    __environ = NULL;

    return DL_START_RETURN(entry, start_arg);
}

static void* dlopen_internal(mx_handle_t vmo, const char* file, int mode) {
    struct dso* volatile p, *orig_tail, *next;
    struct tls_module* orig_tls_tail;
    size_t orig_tls_cnt, orig_tls_offset, orig_tls_align;
    size_t i;
    int cs;
    jmp_buf jb;

    pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &cs);
    pthread_rwlock_wrlock(&lock);
    __inhibit_ptc();

    p = 0;
    orig_tls_tail = tls_tail;
    orig_tls_cnt = tls_cnt;
    orig_tls_offset = tls_offset;
    orig_tls_align = tls_align;
    orig_tail = tail;
    noload = mode & RTLD_NOLOAD;

    rtld_fail = &jb;
    if (setjmp(*rtld_fail)) {
        /* Clean up anything new that was (partially) loaded */
        if (p && p->deps)
            for (i = 0; p->deps[i]; i++)
                if (p->deps[i]->global < 0)
                    p->deps[i]->global = 0;
        for (p = orig_tail->next; p; p = next) {
            next = p->next;
            while (p->td_index) {
                void* tmp = p->td_index->next;
                free(p->td_index);
                p->td_index = tmp;
            }
            free(p->deps);
            unmap_library(p);
            free(p);
        }
        if (!orig_tls_tail)
            libc.tls_head = 0;
        tls_tail = orig_tls_tail;
        tls_cnt = orig_tls_cnt;
        tls_offset = orig_tls_offset;
        tls_align = orig_tls_align;
        tail = orig_tail;
        tail->next = 0;
        p = 0;
        goto end;
    } else {
        if (vmo != MX_HANDLE_INVALID)
            p = load_library_vmo(vmo, file, head);
        else
            p = load_library(file, head);
    }

    if (!p) {
        error(noload ? "Library %s is not already loaded" : "Error loading shared library %s: %m",
              file);
        goto end;
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
    orig_tail = tail;
end:
    __release_ptc();
    if (p)
        gencnt++;
    pthread_rwlock_unlock(&lock);
    if (p)
        do_init_fini(orig_tail);
    pthread_setcancelstate(cs, 0);
    return p;
}

void* dlopen(const char* file, int mode) {
    if (!file)
        return head;
    return dlopen_internal(MX_HANDLE_INVALID, file, mode);
}

void* dlopen_vmo(mx_handle_t vmo, int mode) {
    if (vmo < 0 || vmo == MX_HANDLE_INVALID) {
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

static mx_handle_t loader_svc_rpc(uint32_t opcode,
                                  const void* data, size_t len) {
    mx_handle_t handle = MX_HANDLE_INVALID;
    struct {
        mx_loader_svc_msg_t header;
        uint8_t data[LOADER_SVC_MSG_MAX - sizeof(mx_loader_svc_msg_t)];
    } msg;

    loader_svc_rpc_in_progress = true;

    if (len >= sizeof msg.data) {
        error("message of %zu bytes too large for loader service protocol",
              len);
        handle = ERR_OUT_OF_RANGE;
        goto out;
    }

    memset(&msg.header, 0, sizeof msg.header);
    msg.header.opcode = opcode;
    memcpy(msg.data, data, len);
    msg.data[len] = 0;

    uint32_t nbytes = sizeof msg.header + len + 1;
    mx_status_t status = _mx_msgpipe_write(loader_svc, &msg, nbytes,
                                           NULL, 0, 0);
    if (status != NO_ERROR) {
        error("mx_msgpipe_write of %u bytes to loader service: %d (%s)",
              nbytes, status, mx_strstatus(status));
        handle = status;
        goto out;
    }

    status = _mx_handle_wait_one(loader_svc, MX_SIGNAL_READABLE,
                                 MX_TIME_INFINITE, NULL);
    if (status != NO_ERROR) {
        error("mx_handle_wait_one for loader service reply: %d (%s)",
              status, mx_strstatus(status));
        handle = status;
        goto out;
    }

    uint32_t reply_size = sizeof(msg.header);
    uint32_t handle_count = 1;
    status = _mx_msgpipe_read(loader_svc, &msg, &reply_size,
                              &handle, &handle_count, 0);
    if (status != NO_ERROR) {
        error("mx_msgpipe_read of %u bytes for loader service reply: %d (%s)",
              sizeof(msg.header), status, mx_strstatus(status));
        handle = status;
        goto out;
    }
    if (reply_size != sizeof(msg.header)) {
        error("loader service reply %u bytes != %u",
              reply_size, sizeof(msg.header));
        handle = ERR_INVALID_ARGS;
        goto out;
    }
    if (msg.header.opcode != LOADER_SVC_OP_STATUS) {
        error("loader service reply opcode %u != %u",
              msg.header.opcode, LOADER_SVC_OP_STATUS);
        handle = ERR_INVALID_ARGS;
        goto out;
    }
    if (msg.header.arg != NO_ERROR) {
        if (handle != MX_HANDLE_INVALID) {
            error("loader service error %d reply contains handle %#x",
                  msg.header.arg, handle);
            handle = ERR_INVALID_ARGS;
            goto out;
        }
        if (msg.header.arg > 0) {
            error("loader service error reply %d > 0", msg.header.arg);
            handle = ERR_INVALID_ARGS;
            goto out;
        }
        handle = msg.header.arg;
    }

out:
    loader_svc_rpc_in_progress = false;
    return handle;
}

static mx_handle_t get_library_vmo(const char* name) {
    if (loader_svc == MX_HANDLE_INVALID) {
        error("cannot look up \"%s\" with no loader service", name);
        return MX_HANDLE_INVALID;
    }
    return loader_svc_rpc(LOADER_SVC_OP_LOAD_OBJECT, name, strlen(name));
}

static void log_write(const void* buf, size_t len) {
    // The loader service prints "header: %s\n" when we send %s,
    // so strip a trailing newline.
    if (((const char*)buf)[len - 1] == '\n')
        --len;

    mx_status_t status;
    if (logger != MX_HANDLE_INVALID)
        status = _mx_log_write(logger, len, buf, 0);
    else if (!loader_svc_rpc_in_progress && loader_svc != MX_HANDLE_INVALID)
        status = loader_svc_rpc(LOADER_SVC_OP_DEBUG_PRINT, buf, len);
    else {
        int n = _mx_debug_write(buf, len);
        status = n < 0 ? n : NO_ERROR;
    }
    if (status != NO_ERROR)
        __builtin_trap();
}

static size_t errormsg_write(FILE* f, const unsigned char* buf, size_t len) {
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

static int errormsg_vprintf(const char* restrict fmt, va_list ap) {
    FILE f = {
        .lbf = EOF,
        .write = errormsg_write,
        .buf = (void*)fmt,
        .buf_size = 0,
        .lock = -1,
    };
    return vfprintf(&f, fmt, ap);
}

static void debugmsg(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    errormsg_vprintf(fmt, ap);
    va_end(ap);
}

static void error(const char* fmt, ...) {
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
