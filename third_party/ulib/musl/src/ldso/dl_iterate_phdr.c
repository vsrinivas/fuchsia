#include "libc.h"
#include <elf.h>
#include <link.h>

// This symbol is magically defined by the linker.
extern const ElfW(Ehdr) __ehdr_start __attribute__((visibility("hidden")));

static int static_dl_iterate_phdr(int (*callback)(struct dl_phdr_info* info, size_t size,
                                                  void* data),
                                  void* data) {
    struct dl_phdr_info info = {
        .dlpi_name = "",
        .dlpi_phnum = __ehdr_start.e_phnum,
        .dlpi_phdr = (const void*)((const char*)&__ehdr_start +
                                   __ehdr_start.e_phoff),
    };

    const ElfW(Phdr)* tls_phdr = NULL;
    for (size_t i = 0; i < __ehdr_start.e_phnum; ++i) {
        const ElfW(Phdr)* phdr = &info.dlpi_phdr[i];
        if (phdr->p_type == PT_PHDR)
            info.dlpi_addr = (uintptr_t)info.dlpi_phdr - phdr->p_vaddr;
        if (phdr->p_type == PT_TLS)
            tls_phdr = phdr;
    }

    if (tls_phdr != NULL) {
        info.dlpi_tls_modid = 1;
        info.dlpi_tls_data = (void*)(info.dlpi_addr + tls_phdr->p_vaddr);
    }

    return (*callback)(&info, sizeof(info), data);
}

weak_alias(static_dl_iterate_phdr, dl_iterate_phdr);
