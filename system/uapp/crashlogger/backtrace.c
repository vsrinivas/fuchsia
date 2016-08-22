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

typedef struct dsoinfo dsoinfo_t;
struct dsoinfo {
    dsoinfo_t* next;
    uintptr_t base;
    char name[];
};

static void dsolist_add(dsoinfo_t** list, const char* name, uintptr_t base) {
    if (!strcmp(name, "libc.so")) {
        name = "libmusl.so";
    }
    size_t len = strlen(name);
    dsoinfo_t* dso = calloc(1, sizeof(dsoinfo_t) + len + 1);
    if (dso == NULL) {
        return;
    }
    memcpy(dso->name, name, len + 1);
    dso->base = base;
    while (*list != NULL) {
        if ((*list)->base < dso->base) {
            dso->next = *list;
            *list = dso;
            return;
        }
        list = &((*list)->next);
    }
    *list = dso;
    dso->next = NULL;
}

#define rdebug_vaddr ((uintptr_t) _dl_debug_addr)
#define rdebug_off_lmap offsetof(struct r_debug, r_map)

#define lmap_off_next offsetof(struct link_map, l_next)
#define lmap_off_name offsetof(struct link_map, l_name)
#define lmap_off_addr offsetof(struct link_map, l_addr)

typedef uint64_t mem_handle_t;

static inline mx_status_t read_mem(mem_handle_t h, uint64_t vaddr, void* ptr, size_t len) {
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

dsoinfo_t* fetch_dso_list(mem_handle_t h, const char* name) {
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
        dsolist_add(&dsolist, dsoname[0] ? dsoname : name, base);
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

void backtrace(mem_handle_t h, uintptr_t pc, uintptr_t fp, bool print_dsolist) {
    dsoinfo_t* list = fetch_dso_list(h, "app");
    int n = 1;

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
        if (print_dsolist) {
            printf("dso: @%p '%s'\n", (void*) list->base, list->name);
        }
        free(list);
        list = next;
    }
}
