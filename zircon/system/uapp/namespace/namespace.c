// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <lib/fdio/io.h>
#include <lib/fdio/namespace.h>
#include <lib/fdio/spawn.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

void print_namespace(fdio_flat_namespace_t* flat) {
    for (size_t n = 0; n < flat->count; n++) {
        fprintf(stderr, "{ .handle = 0x%08x, type = 0x%08x, .path = '%s' },\n",
                flat->handle[n], flat->type[n], flat->path[n]);
    }
}

zx_status_t load_file(const char* path, zx_handle_t* vmo) {
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return ZX_ERR_IO;
    zx_status_t status = fdio_get_vmo_clone(fd, vmo);
    close(fd);
    return status;
}

int run_in_namespace(const char** argv, size_t count, const char* const* mapping) {
    zx_status_t status;
    zx_handle_t binary;
    if ((status = load_file(argv[0], &binary)) != ZX_OK) {
        fprintf(stderr, "error: failed to read '%s': %d (%s)\n", argv[0], status,
                zx_status_get_string(status));
        return -1;
    }

    fdio_ns_t* ns;
    if ((status = fdio_ns_create(&ns)) < 0) {
        fprintf(stderr, "error: failed to create namespace: %d (%s)\n", status,
                zx_status_get_string(status));
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
        if ((status = fdio_ns_bind_fd(ns, dst, fd)) < 0) {
            fprintf(stderr, "error: binding fd %d to '%s' failed: %d (%s)\n", fd, dst, status,
                    zx_status_get_string(status));
            close(fd);
            return -1;
        }
        close(fd);
    }
    fdio_flat_namespace_t* flat;
    fdio_ns_opendir(ns);
    status = fdio_ns_export(ns, &flat);
    fdio_ns_destroy(ns);
    if (status < 0) {
        fprintf(stderr, "error: cannot flatten namespace: %d (%s)\n", status,
                zx_status_get_string(status));
        return -1;
    }

    print_namespace(flat);

    fdio_spawn_action_t actions[flat->count + 1];

    for (size_t i = 0; i < flat->count; ++i) {
        fdio_spawn_action_t add_ns_entry = {
            .action = FDIO_SPAWN_ACTION_ADD_NS_ENTRY,
            .ns = {
                .prefix = flat->path[i],
                .handle = flat->handle[i],
            },
        };
        actions[i] = add_ns_entry;
    }

    fdio_spawn_action_t set_name = {.action = FDIO_SPAWN_ACTION_SET_NAME,
                                    .name = {.data = argv[0]}};
    actions[flat->count] = set_name;

    uint32_t flags = FDIO_SPAWN_CLONE_ALL & ~FDIO_SPAWN_CLONE_NAMESPACE;

    if (replacement_argv0)
        argv[0] = replacement_argv0;

    char err_msg[FDIO_SPAWN_ERR_MSG_MAX_LENGTH];
    zx_handle_t proc = ZX_HANDLE_INVALID;
    status = fdio_spawn_vmo(ZX_HANDLE_INVALID, flags, binary, argv, NULL,
                            countof(actions), actions, &proc, err_msg);

    free(flat);

    if (status != ZX_OK) {
        fprintf(stderr, "error: failed to launch command: %d (%s): %s\n",
                status, zx_status_get_string(status), err_msg);
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

int main(int argc, const char** argv) {
    if (argc == 2 && strcmp(argv[1], "--dump") == 0) {
        return dump_current_namespace();
    }

    if (argc > 1) {
        const char* kDefaultArgv[] = { "/boot/bin/sh", NULL };
        const char** child_argv = kDefaultArgv;
        size_t count = 0;
        const char* const* mapping = argv + 1;
        for (int i = 1; i < argc; ++i) {
            if (strcmp(argv[i], "--") == 0) {
                if (i + 1 < argc)
                    child_argv = argv + i + 1;
                break;
            }
            ++count;
        }
        return run_in_namespace(child_argv, count, mapping);
    }

    printf("Usage: %s ( --dump | [dst=src]+ [--replace-child-argv0=child_argv0] [ -- cmd arg1 ... argn ] )\n"
           "Dumps the current namespace or runs a command with src mapped to dst.\n"
           "If no command is specified, runs a shell.\n"
           "If --replace-child-argv0 is supplied, that string will be used for argv[0]\n"
           "as the child process sees it.\n",
           argv[0]);
    return -1;
}
