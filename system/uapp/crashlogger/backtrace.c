// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>
#include <link.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <magenta/types.h>
#include <magenta/syscalls.h>

#include "backtrace.h"

extern struct r_debug* _dl_debug_addr;

#define BUILDIDSZ 0x14

typedef struct dsoinfo dsoinfo_t;
struct dsoinfo {
    dsoinfo_t* next;
    uintptr_t base;
    char buildid[BUILDIDSZ * 2 + 1];
    char name[];
};

static dsoinfo_t* dsolist_add(dsoinfo_t** list, const char* name, uintptr_t base) {
    if (!strcmp(name, "libc.so")) {
        name = "libmusl.so";
    }
    size_t len = strlen(name);
    dsoinfo_t* dso = calloc(1, sizeof(dsoinfo_t) + len + 1);
    if (dso == NULL) {
        return NULL;
    }
    memcpy(dso->name, name, len + 1);
    memset(dso->buildid, 'x', BUILDIDSZ * 2);
    dso->base = base;
    while (*list != NULL) {
        if ((*list)->base < dso->base) {
            dso->next = *list;
            *list = dso;
            return dso;
        }
        list = &((*list)->next);
    }
    *list = dso;
    dso->next = NULL;
    return dso;
}

#define rdebug_vaddr ((uintptr_t) _dl_debug_addr)
#define rdebug_off_lmap offsetof(struct r_debug, r_map)

#define lmap_off_next offsetof(struct link_map, l_next)
#define lmap_off_name offsetof(struct link_map, l_name)
#define lmap_off_addr offsetof(struct link_map, l_addr)

typedef uint64_t mem_handle_t;

static inline mx_status_t read_mem(mx_handle_t h, uint64_t vaddr, void* ptr, size_t len) {
    mx_status_t status = mx_debug_read_memory(h, vaddr, len, ptr);
    if (status != (mx_status_t)len) {
        printf("read_mem @%p FAILED %d\n", (void*) (uintptr_t)vaddr, status);
        return ERR_IO;
    } else {
        return NO_ERROR;
    }
}

static mx_status_t fetch_string(mem_handle_t h, uintptr_t vaddr, char* ptr, size_t max) {
    while (max > 1) {
        mx_status_t status;
        if ((status = read_mem(h, vaddr, ptr, 1)) < 0) {
            *ptr = 0;
            return status;
        }
        ptr++;
        vaddr++;
        max--;
    }
    *ptr = 0;
    return NO_ERROR;
}

#define ehdr_off_phoff offsetof(Elf64_Ehdr, e_phoff)
#define ehdr_off_phnum offsetof(Elf64_Ehdr, e_phnum)

#define phdr_off_type offsetof(Elf64_Phdr, p_type)
#define phdr_off_offset offsetof(Elf64_Phdr, p_offset)
#define phdr_off_filesz offsetof(Elf64_Phdr, p_filesz)

typedef struct {
    uint32_t namesz;
    uint32_t descsz;
    uint32_t type;
    uint32_t name;
} notehdr;

void fetch_build_id(mx_handle_t h, dsoinfo_t* dso) {
    uint64_t vaddr = dso->base;
    uint8_t tmp[4];
    if (read_mem(h, vaddr, tmp, 4) ||
        memcmp(tmp, ELFMAG, SELFMAG)) {
        return;
    }
    Elf64_Off phoff;
    Elf64_Half num;
    if (read_mem(h, vaddr + ehdr_off_phoff, &phoff, sizeof(phoff)) ||
        read_mem(h, vaddr + ehdr_off_phnum, &num, sizeof(num))) {
        return;
    }
    for (unsigned n = 0; n < num; n++) {
        uint64_t phaddr = vaddr + phoff + (n * sizeof(Elf64_Phdr));
        Elf64_Word type;
        if (read_mem(h, phaddr + phdr_off_type, &type, sizeof(type))) {
            return;
        }
        if (type != PT_NOTE) {
            continue;
        }
        Elf64_Off off;
        Elf64_Word size;
        if (read_mem(h, phaddr + phdr_off_offset, &off, sizeof(off)) ||
            read_mem(h, phaddr + phdr_off_filesz, &size, sizeof(size))) {
            return;
        }
        if (size < 0x24) {
            continue;
        }
        notehdr hdr;
        if (read_mem(h, vaddr + off, &hdr, sizeof(hdr))) {
            return;
        }
        if ((hdr.namesz != 4) || (hdr.descsz != 0x14) ||
            (hdr.type != 3) || (hdr.name != 0x554e47)) {
            continue;
        }
        uint8_t buildid[BUILDIDSZ];
        if (read_mem(h, vaddr + off + sizeof(hdr), buildid, sizeof(buildid))) {
            return;
        }
        for (n = 0; n < BUILDIDSZ; n++) {
            sprintf(dso->buildid + n * 2, "%02x", buildid[n]);
        }
        return;
    }
}

dsoinfo_t* fetch_dso_list(mx_handle_t h, const char* name) {
    uintptr_t lmap;
    if (read_mem(h, rdebug_vaddr + rdebug_off_lmap, &lmap, sizeof(lmap))) {
        return NULL;
    }
    dsoinfo_t* dsolist = NULL;
    while (lmap != 0) {
        char dsoname[64];
        uint64_t base;
        uintptr_t next;
        uintptr_t str;
        if (read_mem(h, lmap + lmap_off_addr, &base, sizeof(base))) {
            break;
        }
        if (read_mem(h, lmap + lmap_off_next, &next, sizeof(next))) {
            break;
        }
        if (read_mem(h, lmap + lmap_off_name, &str, sizeof(str))) {
            break;
        }
        if (fetch_string(h, str, dsoname, sizeof(dsoname))) {
            break;
        }
        dsoinfo_t* dso = dsolist_add(&dsolist, dsoname[0] ? dsoname : name, base);
        if (dso != NULL) {
            fetch_build_id(h, dso);
        }
        lmap = next;
    }

    return dsolist;
}

static void btprint(dsoinfo_t* list, int n, uintptr_t pc, uintptr_t sp) {
    dsoinfo_t* dso;
    for (dso = list; dso != NULL; dso = dso->next) {
        if (pc >= dso->base) {
            break;
        }
    }
    if (dso == NULL) {
        fprintf(stderr, "bt#%02d: pc %p sp %p\n",
                n, (void*)pc, (void*)sp);
    } else {
        fprintf(stderr, "bt#%02d: pc %p sp %p (%s,%p)\n",
                n, (void*)pc, (void*)sp, dso->name, (void*)(pc - dso->base));
    }
}

void backtrace(mx_handle_t h, uintptr_t pc, uintptr_t fp) {
    dsoinfo_t* list = fetch_dso_list(h, "app");
    int n = 1;

    for (dsoinfo_t* dso = list; dso != NULL; dso = dso->next) {
        printf("dso: id=%s base=%p name=%s\n",
               dso->buildid, (void*)dso->base, dso->name);
    }

    btprint(list, n++, pc, fp);
    while ((fp >= 0x1000000) && (n < 50)) {
        if (read_mem(h, fp + 8, &pc, sizeof(pc))) {
            break;
        }
        btprint(list, n++, pc, fp);
        if (read_mem(h, fp, &fp, sizeof(fp))) {
            break;
        }
    }

    while (list != NULL) {
        dsoinfo_t* next = list->next;
        free(list);
        list = next;
    }
}
