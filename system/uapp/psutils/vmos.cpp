// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <magenta/status.h>
#include <magenta/syscalls.h>
#include <magenta/syscalls/object.h>
#include <pretty/sizes.h>
#include <task-utils/get.h>

#include "vmo-utils.h"

static constexpr size_t kRightsStrLen = sizeof("rwxmdt");

static const char* handle_rights_to_string(uint32_t rights,
                                           char str[kRightsStrLen]) {
    char* c = str;
    *c++ = (rights & MX_RIGHT_READ) ? 'r' : '-';
    *c++ = (rights & MX_RIGHT_WRITE) ? 'w' : '-';
    *c++ = (rights & MX_RIGHT_EXECUTE) ? 'x' : '-';
    *c++ = (rights & MX_RIGHT_MAP) ? 'm' : '-';
    *c++ = (rights & MX_RIGHT_DUPLICATE) ? 'd' : '-';
    *c++ = (rights & MX_RIGHT_TRANSFER) ? 't' : '-';
    *c = '\0';
    return str;
}

static void print_vmo(const mx_info_vmo_t* vmo) {

    char rights_str[kRightsStrLen];
    if (vmo->flags & MX_INFO_VMO_VIA_HANDLE) {
        handle_rights_to_string(vmo->handle_rights, rights_str);
    } else {
        rights_str[0] = '-';
        rights_str[1] = '\0';
    }

    char size_str[MAX_FORMAT_SIZE_LEN];
    format_size(size_str, sizeof(size_str), vmo->size_bytes);

    char alloc_str[MAX_FORMAT_SIZE_LEN];
    switch (MX_INFO_VMO_TYPE(vmo->flags)) {
    case MX_INFO_VMO_TYPE_PAGED:
        format_size(alloc_str, sizeof(alloc_str), vmo->committed_bytes);
        break;
    case MX_INFO_VMO_TYPE_PHYSICAL:
        strlcpy(alloc_str, "phys", sizeof(alloc_str));
        break;
    default:
        // Unexpected: all VMOs should be one of the above types.
        snprintf(alloc_str, sizeof(alloc_str), "?0x%" PRIx32 "?", vmo->flags);
        break;
    }

    char clone_str[21];
    if (vmo->flags & MX_INFO_VMO_IS_COW_CLONE) {
        snprintf(clone_str, sizeof(clone_str), "%" PRIu64, vmo->parent_koid);
    } else {
        clone_str[0] = '-';
        clone_str[1] = '\0';
    }

    char name[MX_MAX_NAME_LEN];
    strlcpy(name, vmo->name, sizeof(name));
    if (name[0] == '\0') {
        name[0] = '-';
        name[1] = '\0';
    }

    printf("%6s " // rights
           "%5" PRIu64 " " // koid
           "%6s " // clone parent koid
           "%5zu " // number of children
           "%4zu " // map count
           "%4zu " // share count
           "%7s " // size in bytes
           "%7s " // allocated bytes
           "%s\n", // name
           rights_str,
           vmo->koid,
           clone_str,
           vmo->num_children,
           vmo->num_mappings,
           vmo->share_count,
           size_str,
           alloc_str,
           name);
}

static void print_header() {
    printf("rights  koid parent #chld #map #shr    size   alloc name\n");
}

// Pretty-prints the contents of |vmos| to stdout.
void print_vmos(const mx_info_vmo_t* vmos, size_t count, size_t avail) {
    print_header();
    for (size_t i = 0; i < count; i++) {
        print_vmo(vmos + i);
    }
    if (avail > count) {
        printf("[%zd entries truncated]\n", avail - count);
    }
    print_header();
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
    printf("Dumps a process's VMOs to stdout.\n");
    printf("\n");
    printf("The process either maps or has a handle to every VMO in this list (or both).\n");
    printf("The same VMO may appear multiple times: the process could map the same VMO\n");
    printf("twice, or have two handles to it, or both map it and have a handle to it.\n");
    // TODO(dbort): Consider de-duping the entries.
    printf("\n");
    printf("Columns:\n");
    printf("  rights: If the process points to the VMO via a handle, this column\n");
    printf("      shows the rights that the handle has, zero or more of:\n");
    printf("          r: MX_RIGHT_READ\n");
    printf("          w: MX_RIGHT_WRITE\n");
    printf("          x: MX_RIGHT_EXECUTE\n");
    printf("          m: MX_RIGHT_MAP\n");
    printf("          d: MX_RIGHT_DUPLICATE\n");
    printf("          t: MX_RIGHT_TRANSFER\n");
    printf("      NOTE: Non-handle entries will have a single '-' in this column.\n");
    printf("  koid: The koid of the VMO, if it has one. Zero otherwise. A VMO without a\n");
    printf("      koid was created by the kernel, and has never had a userspace handle.\n");
    printf("  parent: The koid of the VMO's parent, if it's a clone.\n");
    printf("  #chld: The number of active clones (children) of the VMO.\n");
    printf("  #map: The number of times the VMO is currently mapped into VMARs.\n");
    printf("  #shr: The number of processes that map (share) the VMO.\n");
    printf("  size: The VMO's current size, in bytes.\n");
    printf("  alloc: The amount of physical memory allocated to the VMO, in bytes.\n");
    printf("      NOTE: If this column contains the value 'phys', it means that the\n");
    printf("      VMO points to a raw physical address range like a memory-mapped\n");
    printf("      device. 'phys' VMOs do not consume RAM.\n");
    printf("  name: The name of the VMO, or - if its name is empty.\n");
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
    mx_koid_t koid = strtoull(argv[1], &end, 0);
    if (argv[1][0] == '\0' || *end != '\0') {
        fprintf(stderr, "ERROR: \"%s\" is not a number\n", argv[1]);
        usage(argv[0]);
    }

    mx_handle_t process;
    mx_obj_type_t type;
    mx_status_t s = get_task_by_koid(koid, &type, &process);
    if (s == MX_OK && type != MX_OBJ_TYPE_PROCESS) {
        mx_handle_close(process);
        s = MX_ERR_WRONG_TYPE;
    }
    if (s != MX_OK) {
        fprintf(stderr,
                "ERROR: couldn't find process with koid %" PRIu64 ": %s (%d)\n",
                koid, mx_status_get_string(s), s);
        usage(argv[0]);
    }

    mx_info_vmo_t* vmos;
    size_t count;
    size_t avail;
    s = get_vmos(process, &vmos, &count, &avail);
    mx_handle_close(process);
    if (s != MX_OK) {
        fprintf(stderr,
                "ERROR: couldn't get vmos for process with koid %" PRIu64
                ": %s (%d)\n",
                koid, mx_status_get_string(s), s);
        return 1;
    }
    print_vmos(vmos, count, avail);
    free(vmos);
    return 0;
}
