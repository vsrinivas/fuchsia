// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <link.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <magenta/types.h>
#include <magenta/syscalls.h>

#include "dso-list.h"
#include "utils.h"

extern struct r_debug* _dl_debug_addr;

#define rdebug_vaddr ((uintptr_t) _dl_debug_addr)
#define rdebug_off_lmap offsetof(struct r_debug, r_map)

#define lmap_off_next offsetof(struct link_map, l_next)
#define lmap_off_name offsetof(struct link_map, l_name)
#define lmap_off_addr offsetof(struct link_map, l_addr)

static dsoinfo_t* dsolist_add(dsoinfo_t** list, const char* name, uintptr_t base) {
    if (!strcmp(name, "libc.so")) {
        name = "libmusl.so";
    }
    size_t len = strlen(name);
    auto dso = reinterpret_cast<dsoinfo_t*> (calloc(1, sizeof(dsoinfo_t) + len + 1));
    if (dso == nullptr) {
        return nullptr;
    }
    memcpy(dso->name, name, len + 1);
    memset(dso->buildid, 'x', sizeof(dso->buildid) - 1);
    dso->base = base;
    while (*list != nullptr) {
        if ((*list)->base < dso->base) {
            dso->next = *list;
            *list = dso;
            return dso;
        }
        list = &((*list)->next);
    }
    *list = dso;
    dso->next = nullptr;
    return dso;
}

dsoinfo_t* dso_fetch_list(mx_handle_t h, const char* name) {
    uintptr_t lmap;
    if (read_mem(h, rdebug_vaddr + rdebug_off_lmap, &lmap, sizeof(lmap))) {
        return nullptr;
    }
    dsoinfo_t* dsolist = nullptr;
    while (lmap != 0) {
        char dsoname[64];
        mx_vaddr_t base;
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
        if (dso != nullptr) {
            fetch_build_id(h, dso->base, dso->buildid, sizeof(dso->buildid));
        }
        lmap = next;
    }

    return dsolist;
}

void dso_free_list(dsoinfo_t* list) {
    while (list != NULL) {
        dsoinfo_t* next = list->next;
        free(list);
        list = next;
    }
}

dsoinfo_t* dso_lookup(dsoinfo_t* dso_list, mx_vaddr_t pc) {
    for (dsoinfo_t* dso = dso_list; dso != NULL; dso = dso->next) {
        if (pc >= dso->base) {
            return dso;
        }
    }

    return nullptr;
}

void dso_print_list(dsoinfo_t* dso_list) {
    for (dsoinfo_t* dso = dso_list; dso != nullptr; dso = dso->next) {
        printf("dso: id=%s base=%p name=%s\n",
               dso->buildid, (void*) dso->base, dso->name);
    }
}
