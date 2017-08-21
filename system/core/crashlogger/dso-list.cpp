// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <link.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <magenta/assert.h>
#include <magenta/types.h>
#include <magenta/syscalls.h>

#include "dso-list.h"
#include "utils.h"

#define rdebug_off_lmap offsetof(struct r_debug, r_map)

#define lmap_off_next offsetof(struct link_map, l_next)
#define lmap_off_name offsetof(struct link_map, l_name)
#define lmap_off_addr offsetof(struct link_map, l_addr)

const char kDebugDirectory[] = "/boot/debug";
const char kDebugSuffix[] = ".debug";

static dsoinfo_t* dsolist_add(dsoinfo_t** list, const char* name, uintptr_t base) {
    if (!strncmp(name, "app:devhost:", 12)) {
        // devhost processes use their name field to describe
        // the root of their device sub-tree.
        name = "app:/boot/bin/devhost";
    }
    size_t len = strlen(name);
    auto dso = reinterpret_cast<dsoinfo_t*> (calloc(1, sizeof(dsoinfo_t) + len + 1));
    if (dso == nullptr) {
        return nullptr;
    }
    memcpy(dso->name, name, len + 1);
    memset(dso->buildid, 'x', sizeof(dso->buildid) - 1);
    dso->base = base;
    dso->debug_file_tried = false;
    dso->debug_file_status = MX_ERR_BAD_STATE;
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
    uintptr_t lmap, debug_addr;
    mx_status_t status = mx_object_get_property(h, MX_PROP_PROCESS_DEBUG_ADDR,
                                                &debug_addr, sizeof(debug_addr));
    if (status != MX_OK) {
        print_mx_error("mx_object_get_property(MX_PROP_PROCESS_DEBUG_ADDR), unable to fetch dso list", status);
        return nullptr;
    }
    if (read_mem(h, debug_addr + rdebug_off_lmap, &lmap, sizeof(lmap))) {
        return nullptr;
    }
    dsoinfo_t* dsolist = nullptr;
    int iter = 0;
    while (lmap != 0) {
        if (iter++ > 50) {
            print_error("dso_fetch_list detected too many entries, possible infinite loop");
            return nullptr;
        }
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
        free(list->debug_file);
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

mx_status_t dso_find_debug_file(dsoinfo_t* dso, const char** out_debug_file) {
    // Have we already tried?
    // Yeah, if we OOM it's possible it'll succeed next time, but
    // it's not worth the extra complexity to avoid printing the debugging
    // messages twice.
    if (dso->debug_file_tried) {
        switch (dso->debug_file_status) {
        case MX_OK:
            MX_DEBUG_ASSERT(dso->debug_file != nullptr);
            *out_debug_file = dso->debug_file;
            // fall through
        default:
            debugf(2, "returning %d, already tried to find debug file for %s\n",
                   dso->debug_file_status, dso->name);
            return dso->debug_file_status;
        }
    }

    dso->debug_file_tried = true;

    char* path;
    if (asprintf(&path, "%s/%s%s", kDebugDirectory, dso->buildid, kDebugSuffix) < 0) {
        debugf(1, "OOM building debug file path for dso %s\n", dso->name);
        dso->debug_file_status = MX_ERR_NO_MEMORY;
        return dso->debug_file_status;
    }

    debugf(1, "looking for debug file %s\n", path);

    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        debugf(1, "debug file for dso %s not found: %s\n", dso->name, path);
        free(path);
        dso->debug_file_status = MX_ERR_NOT_FOUND;
    } else {
        debugf(1, "found debug file for dso %s: %s\n", dso->name, path);
        close(fd);
        dso->debug_file = path;
        *out_debug_file = path;
        dso->debug_file_status = MX_OK;
    }

    return dso->debug_file_status;
}
