// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <link.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <zircon/assert.h>
#include <zircon/types.h>
#include <zircon/syscalls.h>

#include "inspector/inspector.h"
#include "dso-list-impl.h"
#include "utils-impl.h"

#define rdebug_off_lmap offsetof(struct r_debug, r_map)

#define lmap_off_next offsetof(struct link_map, l_next)
#define lmap_off_name offsetof(struct link_map, l_name)
#define lmap_off_addr offsetof(struct link_map, l_addr)

using inspector::read_mem;
using inspector::fetch_string;
using inspector::fetch_build_id;

const char kDebugDirectory[] = "/boot/debug";
const char kDebugSuffix[] = ".debug";

static inspector_dsoinfo_t* dsolist_add(inspector_dsoinfo_t** list,
                                        const char* name, uintptr_t base) {
    if (!strncmp(name, "app:devhost:", 12)) {
        // devhost processes use their name field to describe
        // the root of their device sub-tree.
        name = "app:/boot/bin/devhost";
    }
    size_t len = strlen(name);
    auto dso = reinterpret_cast<inspector_dsoinfo_t*> (
        calloc(1, sizeof(inspector_dsoinfo_t) + len + 1));
    if (dso == nullptr) {
        return nullptr;
    }
    memcpy(dso->name, name, len + 1);
    memset(dso->buildid, 'x', sizeof(dso->buildid) - 1);
    dso->base = base;
    dso->debug_file_tried = false;
    dso->debug_file_status = ZX_ERR_BAD_STATE;
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

inspector_dsoinfo_t* inspector_dso_fetch_list(zx_handle_t h) {
    // Prepend "app:" to the name we print for the process binary to tell the
    // reader (and the symbolize script!) that the name is the process's.
    // The name property is only 32 characters which may be insufficient.
    // N.B. The symbolize script looks for "app" and "app:".
#define PROCESS_NAME_PREFIX "app:"
#define PROCESS_NAME_PREFIX_LEN (sizeof(PROCESS_NAME_PREFIX) - 1)
    char name[ZX_MAX_NAME_LEN + PROCESS_NAME_PREFIX_LEN];
    strcpy(name, PROCESS_NAME_PREFIX);
    auto status = zx_object_get_property(h, ZX_PROP_NAME, name + PROCESS_NAME_PREFIX_LEN,
                                         sizeof(name) - PROCESS_NAME_PREFIX_LEN);
    if (status != ZX_OK) {
        print_zx_error("zx_object_get_property, falling back to \"app\" for program name", status);
        strlcpy(name, "app", sizeof(name));
    }

    uintptr_t lmap, debug_addr;
    status = zx_object_get_property(h, ZX_PROP_PROCESS_DEBUG_ADDR,
                                    &debug_addr, sizeof(debug_addr));
    if (status != ZX_OK) {
        print_zx_error("zx_object_get_property(ZX_PROP_PROCESS_DEBUG_ADDR), unable to fetch dso list", status);
        return nullptr;
    }
    if (read_mem(h, debug_addr + rdebug_off_lmap, &lmap, sizeof(lmap))) {
        return nullptr;
    }
    inspector_dsoinfo_t* dsolist = nullptr;
    int iter = 0;
    while (lmap != 0) {
        if (iter++ > 500) {
            print_error("dso_fetch_list detected too many entries, possible infinite loop");
            return nullptr;
        }
        char dsoname[64];
        zx_vaddr_t base;
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
        inspector_dsoinfo_t* dso = dsolist_add(&dsolist,
                                               dsoname[0] ? dsoname : name,
                                               base);
        if (dso != nullptr) {
            fetch_build_id(h, dso->base, dso->buildid, sizeof(dso->buildid));
        }
        lmap = next;
    }

    return dsolist;
}

void inspector_dso_free_list(inspector_dsoinfo_t* list) {
    while (list != NULL) {
        inspector_dsoinfo_t* next = list->next;
        free(list->debug_file);
        free(list);
        list = next;
    }
}

inspector_dsoinfo_t* inspector_dso_lookup(inspector_dsoinfo_t* dso_list,
                                          zx_vaddr_t pc) {
    for (inspector_dsoinfo_t* dso = dso_list; dso != NULL; dso = dso->next) {
        if (pc >= dso->base) {
            return dso;
        }
    }

    return nullptr;
}

void inspector_dso_print_list(FILE* f, inspector_dsoinfo_t* dso_list) {
    for (inspector_dsoinfo_t* dso = dso_list; dso != nullptr; dso = dso->next) {
        fprintf(f, "dso: id=%s base=%p name=%s\n",
                dso->buildid, (void*) dso->base, dso->name);
    }
}

zx_status_t inspector_dso_find_debug_file(inspector_dsoinfo_t* dso,
                                          const char** out_debug_file) {
    // Have we already tried?
    // Yeah, if we OOM it's possible it'll succeed next time, but
    // it's not worth the extra complexity to avoid printing the debugging
    // messages twice.
    if (dso->debug_file_tried) {
        switch (dso->debug_file_status) {
        case ZX_OK:
            ZX_DEBUG_ASSERT(dso->debug_file != nullptr);
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
        dso->debug_file_status = ZX_ERR_NO_MEMORY;
        return dso->debug_file_status;
    }

    debugf(1, "looking for debug file %s\n", path);

    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        debugf(1, "debug file for dso %s not found: %s\n", dso->name, path);
        free(path);
        dso->debug_file_status = ZX_ERR_NOT_FOUND;
    } else {
        debugf(1, "found debug file for dso %s: %s\n", dso->name, path);
        close(fd);
        dso->debug_file = path;
        *out_debug_file = path;
        dso->debug_file_status = ZX_OK;
    }

    return dso->debug_file_status;
}
