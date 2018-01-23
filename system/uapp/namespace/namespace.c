// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <launchpad/launchpad.h>
#include <launchpad/vmo.h>
#include <fdio/namespace.h>
#include <zircon/syscalls.h>

void print_namespace(fdio_flat_namespace_t* flat) {
    for (size_t n = 0; n < flat->count; n++) {
        fprintf(stderr, "{ .handle = 0x%08x, type = 0x%08x, .path = '%s' },\n",
                flat->handle[n], flat->type[n], flat->path[n]);
    }
}

int run_in_namespace(int argc, const char* const* argv,
                     size_t count, const char* const* mapping) {
    zx_status_t r;
    zx_handle_t binary;
    r = launchpad_vmo_from_file(argv[0], &binary);
    if (r != ZX_OK) {
        fprintf(stderr, "error: failed to read '%s': %d\n", argv[0], r);
        return -1;
    }

    fdio_ns_t* ns;
    if ((r = fdio_ns_create(&ns)) < 0) {
        fprintf(stderr, "error: failed to create namespace: %d\n", r);
        return -1;
    }
    const char* replacement_argv0 = NULL;
    for (size_t n = 0; n < count; n++) {
        const char* dst = *mapping++;
        char* src = strchr(dst, '=');
        if (src == NULL) {
            fprintf(stderr, "error: mapping '%s' not in form of '<dst>=<src>'\n", dst);
            return -1;
        }
        *src++ = 0;
        if (strcmp(dst, "--replace-child-argv0") == 0) {
            if (replacement_argv0) {
                fprintf(stderr, "error: multiple --replace-child-argv0 specified\n");
                return -1;
            }
            replacement_argv0 = src;
            continue;
        }
        int fd = open(src, O_RDONLY | O_DIRECTORY);
        if (fd < 0) {
            fprintf(stderr, "error: cannot open '%s'\n", src);
            return -1;
        }
        if ((r = fdio_ns_bind_fd(ns, dst, fd)) < 0) {
            fprintf(stderr, "error: binding fd %d to '%s' failed: %d\n", fd, dst, r);
            close(fd);
            return -1;
        }
        close(fd);
    }
    fdio_flat_namespace_t* flat;
    fdio_ns_opendir(ns);
    r = fdio_ns_export(ns, &flat);
    fdio_ns_destroy(ns);
    if (r < 0) {
        fprintf(stderr, "error: cannot flatten namespace: %d\n", r);
        return -1;
    }

    print_namespace(flat);

    launchpad_t* lp;
    launchpad_create(0, argv[0], &lp);
    launchpad_clone(lp, LP_CLONE_FDIO_STDIO | LP_CLONE_ENVIRON | LP_CLONE_DEFAULT_JOB);

    if (replacement_argv0) {
        const char** argv_with_replaced_argv0 = malloc(argc * sizeof(const char*));
        argv_with_replaced_argv0[0] = replacement_argv0;
        for (int i = 1; i < argc; i++)
            argv_with_replaced_argv0[i] = argv[i];
        launchpad_set_args(lp, argc, argv_with_replaced_argv0);
        free(argv_with_replaced_argv0);
    } else {
        launchpad_set_args(lp, argc, argv);
    }

    launchpad_set_nametable(lp, flat->count, flat->path);
    launchpad_add_handles(lp, flat->count, flat->handle, flat->type);
    launchpad_load_from_vmo(lp, binary);
    free(flat);
    const char* errmsg;
    zx_handle_t proc;
    if ((r = launchpad_go(lp, &proc, &errmsg)) < 0) {
        fprintf(stderr, "error: failed to launch command: %s\n", errmsg);
        return -1;
    }
    zx_object_wait_one(proc, ZX_PROCESS_TERMINATED, ZX_TIME_INFINITE, NULL);
    zx_info_process_t info;
    zx_object_get_info(proc, ZX_INFO_PROCESS, &info, sizeof(info), NULL, NULL);
    fprintf(stderr, "[done]\n");
    return info.return_code;
}

int dump_current_namespace(void) {
    fdio_flat_namespace_t* flat;
    zx_status_t r = fdio_ns_export_root(&flat);

    if (r < 0) {
        fprintf(stderr, "error: cannot export namespace: %d\n", r);
        return -1;
    }

    print_namespace(flat);
    return 0;
}

static const char* kShell[] = { "/boot/bin/sh" };

int main(int argc, const char* const* argv) {
    if (argc == 2 && strcmp(argv[1], "--dump") == 0) {
        return dump_current_namespace();
    }

    if (argc > 1) {
        int child_argc = 1;
        const char* const* child_argv = kShell;
        size_t count = 0;
        const char* const* mapping = argv + 1;
        for (int i = 1; i < argc; ++i) {
            if (strcmp(argv[i], "--") == 0) {
                if (i + 1 < argc) {
                    child_argc = argc - i - 1;
                    child_argv = &argv[i + 1];
                }
                break;
            }
            ++count;
        }
        return run_in_namespace(child_argc, child_argv, count, mapping);
    }

    printf("Usage: %s ( --dump | [dst=src]+ [--replace-child-argv0=child_argv0] [ -- cmd arg1 ... argn ] )\n"
           "Dumps the current namespace or runs a command with src mapped to dst.\n"
           "If no command is specified, runs a shell.\n"
           "If --replace-child-argv0 is supplied, that string will be used for argv[0]\n"
           "as the child process sees it.\n",
           argv[0]);
    return -1;
}
