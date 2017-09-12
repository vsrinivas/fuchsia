// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/compiler.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/object.h>
#include <zircon/types.h>
#include <pretty/sizes.h>
#include <task-utils/get.h>
#include <task-utils/walker.h>

#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Reads the zx_info_maps_t entries for the process.
// Caller is responsible for the |out_maps| pointer.
zx_status_t get_maps(zx_koid_t koid, zx_handle_t process,
                     zx_info_maps_t** out_maps, size_t* out_count,
                     size_t* out_avail) {
    size_t count = 4096; // Should be more than enough.
    zx_info_maps_t* maps = NULL;
    int pass = 3;
    while (true) {
        maps = (zx_info_maps_t*)realloc(maps, count * sizeof(zx_info_maps_t));

        size_t actual;
        size_t avail;
        zx_status_t s = zx_object_get_info(process, ZX_INFO_PROCESS_MAPS,
                                           maps, count * sizeof(zx_info_maps_t),
                                           &actual, &avail);
        if (s != ZX_OK) {
            fprintf(stderr,
                    "ERROR: couldn't get maps for process with koid %" PRIu64
                    ": %s (%d)\n",
                    koid, zx_status_get_string(s), s);
            free(maps);
            return s;
        }
        if (actual < avail && pass-- > 0) {
            count = (avail * 10) / 9;
            continue;
        }
        *out_maps = maps;
        *out_count = actual;
        *out_avail = avail;
        return ZX_OK;
    }
}

void print_ptr(zx_vaddr_t addr) {
    if (addr <= UINT32_MAX) {
        printf("________%08" PRIx32, (uint32_t)addr);
    } else {
        printf("%016" PRIx64, addr);
    }
}

void print_range(zx_vaddr_t addr, size_t size) {
    print_ptr(addr);
    printf("-");
    print_ptr(addr + size);
}

void print_mmu_flags(unsigned int mmu_flags) {
    if (mmu_flags & ZX_VM_FLAG_PERM_READ) {
        printf("r");
    } else {
        printf("-");
    }
    if (mmu_flags & ZX_VM_FLAG_PERM_WRITE) {
        printf("w");
    } else {
        printf("-");
    }
    if (mmu_flags & ZX_VM_FLAG_PERM_EXECUTE) {
        printf("x");
    } else {
        printf("-");
    }
}

// Pretty-prints the contents of |maps| to stdout.
zx_status_t print_maps(zx_info_maps_t* maps, size_t count, size_t avail) {
    size_t max_depth = 2;
    for (size_t i = 0; i < count; i++) {
        zx_info_maps_t* e = maps + i;
        if (e->depth > max_depth) {
            max_depth = e->depth;
        }
    }

    char size_str[MAX_FORMAT_SIZE_LEN];
    for (size_t i = 0; i < count; i++) {
        zx_info_maps_t* e = maps + i;
        char tc = 0;
        switch (e->type) {
        case ZX_INFO_MAPS_TYPE_ASPACE:
            tc = 'A';
            break;
        case ZX_INFO_MAPS_TYPE_VMAR:
            tc = 'R';
            break;
        case ZX_INFO_MAPS_TYPE_MAPPING:
            tc = 'M';
            break;
        default:
            break;
        }
        if (tc == 0) {
            continue;
        }

        // Print the type character, indented to show its place in the tree.
        if (e->depth < 2) {
            // This is the aspace or root vmar.
            // They'll always exist and always be the parents of everything.
            printf("/%c%*s", tc, (int)(max_depth - 3), "");
        } else {
            printf("%*s%c%*s", (int)(e->depth - 2), "",
                   tc, (int)(max_depth - e->depth), "");
        }

        printf(" ");
        print_range(e->base, e->size);

        int size_width;
        if (e->type == ZX_INFO_MAPS_TYPE_MAPPING) {
            printf(" ");
            print_mmu_flags(e->u.mapping.mmu_flags);
            size_width = 5;
        } else {
            size_width = 9;
        }

        format_size(size_str, sizeof(size_str), e->size);
        printf(" %*s:sz", size_width, size_str);
        if (e->type == ZX_INFO_MAPS_TYPE_MAPPING) {
            const zx_info_maps_mapping_t* u = &e->u.mapping;
            format_size(size_str, sizeof(size_str),
                        u->committed_pages * PAGE_SIZE);
            printf(" %4s:res", size_str);
            printf(" %5" PRIu64 ":vmo", u->vmo_koid);
        } else {
            printf("%19s", "");
        }

        printf(" '%s'\n", e->name);
    }
    if (avail > count) {
        printf("[%zd entries truncated]\n", avail - count);
    }
    return ZX_OK;
}

void try_help(char** argv) {
    const char* c = argv[1];
    while (*c == '-') {
        c++;
    }
    if (strcmp(c, "help") != 0) {
        return;
    }

    printf("Usage: %s <process-koid>\n", argv[0]);
    printf("\n");
    printf("Dumps a process's memory maps to stdout.\n");
    printf("\n");
    printf("First column:\n");
    printf("  \"/A\" -- Process address space\n");
    printf("  \"/R\" -- Root VMAR\n");
    printf("  \"R\"  -- VMAR (R for Region)\n");
    printf("  \"M\"  -- Mapping\n");
    printf("\n");
    printf("  Indentation indicates parent/child relationship.\n");
    exit(0);
}

__NO_RETURN void usage(const char* argv0) {
    fprintf(stderr, "Usage: %s <process-koid>|help\n", argv0);
    exit(1);
}

int main(int argc, char** argv) {
    if (argc != 2) {
        usage(argv[0]);
    }
    try_help(argv);
    char* end;
    zx_koid_t koid = strtoull(argv[1], &end, 0);
    if (argv[1][0] == '\0' || *end != '\0') {
        fprintf(stderr, "ERROR: \"%s\" is not a number\n", argv[1]);
        usage(argv[0]);
    }

    zx_handle_t process;
    zx_obj_type_t type;
    zx_status_t s = get_task_by_koid(koid, &type, &process);
    if (s == ZX_OK && type != ZX_OBJ_TYPE_PROCESS) {
        zx_handle_close(process);
        s = ZX_ERR_WRONG_TYPE;
    }
    if (s != ZX_OK) {
        fprintf(stderr,
                "ERROR: couldn't find process with koid %" PRIu64 ": %s (%d)\n",
                koid, zx_status_get_string(s), s);
        usage(argv[0]);
    }

    zx_info_maps_t* maps;
    size_t count;
    size_t avail;
    s = get_maps(koid, process, &maps, &count, &avail);
    zx_handle_close(process);
    if (s != ZX_OK) {
        return 1;
    }
    s = print_maps(maps, count, avail);
    free(maps);
    return s == ZX_OK ? 0 : 1;
}
