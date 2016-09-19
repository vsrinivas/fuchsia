#include "tls_impl.h"

#include "atomic.h"
#include "libc.h"
#include "pthread_impl.h"
#include <elf.h>
#include <limits.h>
#include <runtime/tls.h>
#include <stddef.h>
#include <string.h>
#include <sys/mman.h>

#define ROUND(x) (((x) + PAGE_SIZE - 1) & -PAGE_SIZE)

/* pthread_key_create.c overrides this */
static void* dummy_tsd[1] = {0};
weak_alias(dummy_tsd, __pthread_tsd_main);

void __init_tp(pthread_t thread) {
    thread->self = thread;
    // TODO(kulakowski): Get and set thread ID
    mxr_tp_set(pthread_to_tp(thread));
    thread->locale = &libc.global_locale;
}

static struct builtin_tls {
    char c;
    struct pthread pt;
    void* space[16];
} builtin_tls[1];
#define MIN_TLS_ALIGN offsetof(struct builtin_tls, pt)

static struct tls_module main_tls;

void* __copy_tls(unsigned char* mem) {
    pthread_t td;
    struct tls_module* p;
    size_t i;
    void** dtv;

#ifdef TLS_ABOVE_TP
    dtv = (void**)(mem + libc.tls_size) - (libc.tls_cnt + 1);

    mem += -((uintptr_t)mem + sizeof(struct pthread)) & (libc.tls_align - 1);
    td = (pthread_t)mem;
    mem += sizeof(struct pthread);

    for (i = 1, p = libc.tls_head; p; i++, p = p->next) {
        dtv[i] = mem + p->offset;
        memcpy(dtv[i], p->image, p->len);
    }
#else
    dtv = (void**)mem;

    mem += libc.tls_size - sizeof(struct pthread);
    mem -= (uintptr_t)mem & (libc.tls_align - 1);
    td = (pthread_t)mem;

    for (i = 1, p = libc.tls_head; p; i++, p = p->next) {
        dtv[i] = mem - p->offset;
        memcpy(dtv[i], p->image, p->len);
    }
#endif
    dtv[0] = (void*)libc.tls_cnt;
    td->dtv = td->dtv_copy = dtv;
    return td;
}

#if ULONG_MAX == 0xffffffff
typedef Elf32_Phdr Phdr;
#else
typedef Elf64_Phdr Phdr;
#endif

static void static_init_tls(mxr_thread_t* mxr_thread) {
    void* mem;

    // TODO(kulakowski) Get base and the tls phdr in the static case.
    // unsigned char *p;
    // size_t n;
    // Phdr *phdr, *tls_phdr=0;
    // size_t base = 0;
    // for (p=(void *)aux[AT_PHDR],n=aux[AT_PHNUM]; n; n--,p+=aux[AT_PHENT]) {
    //     phdr = (void *)p;
    //     if (phdr->p_type == PT_PHDR)
    //         base = aux[AT_PHDR] - phdr->p_vaddr;
    //     if (phdr->p_type == PT_TLS)
    //         tls_phdr = phdr;
    // }

    // if (tls_phdr) {
    //     main_tls.image = (void *)(base + tls_phdr->p_vaddr);
    //     main_tls.len = tls_phdr->p_filesz;
    //     main_tls.size = tls_phdr->p_memsz;
    //     main_tls.align = tls_phdr->p_align;
    //     libc.tls_cnt = 1;
    //     libc.tls_head = &main_tls;
    // }

    main_tls.size += (-main_tls.size - (uintptr_t)main_tls.image) & (main_tls.align - 1);
    if (main_tls.align < MIN_TLS_ALIGN)
        main_tls.align = MIN_TLS_ALIGN;
#ifndef TLS_ABOVE_TP
    main_tls.offset = main_tls.size;
#endif

    libc.tls_align = main_tls.align;
    libc.tls_size = 2 * sizeof(void*) + sizeof(struct pthread) + main_tls.size + main_tls.align + MIN_TLS_ALIGN - 1 & -MIN_TLS_ALIGN;

    if (libc.tls_size > sizeof builtin_tls) {
        mem = mmap(0, libc.tls_size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
        if (mem == MAP_FAILED)
            __builtin_trap();
    } else {
        mem = builtin_tls;
    }

    __init_tp(__copy_tls(mem));

    pthread_t self = mem;
    self->mxr_thread = mxr_thread;
    self->tsd = __pthread_tsd_main;
}

weak_alias(static_init_tls, __init_tls);
