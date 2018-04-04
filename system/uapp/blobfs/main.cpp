// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <fcntl.h>
#include <getopt.h>
#include <libgen.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <lib/async-loop/cpp/loop.h>
#include <blobfs/blobfs.h>
#include <blobfs/fsck.h>
#include <fbl/auto_call.h>
#include <fbl/ref_ptr.h>
#include <fbl/string.h>
#include <fbl/unique_fd.h>
#include <fbl/vector.h>
#include <fs/vfs.h>
#include <trace-provider/provider.h>
#include <zircon/process.h>
#include <zircon/processargs.h>

namespace {

typedef struct {
    bool readonly = false;
    bool metrics = false;
} blob_options_t;

int do_blobfs_mount(fbl::unique_fd fd, const blob_options_t& options) {
    bool readonly = options.readonly;
    if (!readonly) {
        block_info_t block_info;
        zx_status_t status = static_cast<zx_status_t>(ioctl_block_get_info(fd.get(), &block_info));
        if (status < ZX_OK) {
            FS_TRACE_ERROR("blobfs: Unable to query block device, fd: %d status: 0x%x\n",
                           fd.get(), status);
            return -1;
        }
        readonly = block_info.flags & BLOCK_FLAG_READONLY;
    }

    fbl::RefPtr<blobfs::VnodeBlob> vn;
    async::Loop loop;
    if (blobfs::blobfs_mount(loop.async(), fbl::move(fd), options.metrics, &vn) != ZX_OK) {
        return -1;
    }
    zx_handle_t h = zx_get_startup_handle(PA_HND(PA_USER0, 0));
    if (h == ZX_HANDLE_INVALID) {
        FS_TRACE_ERROR("blobfs: Could not access startup handle to mount point\n");
        return -1;
    }

    fs::Vfs vfs(loop.async());
    vfs.SetReadonly(readonly);
    zx_status_t status;
    if ((status = vfs.ServeDirectory(fbl::move(vn), zx::channel(h))) != ZX_OK) {
        return status;
    }
    trace::TraceProvider provider(loop.async());
    loop.Run();
    return ZX_OK;
}

int do_blobfs_mkfs(fbl::unique_fd fd, const blob_options_t& options) {
    uint64_t block_count;
    if (blobfs::blobfs_get_blockcount(fd.get(), &block_count)) {
        fprintf(stderr, "blobfs: cannot find end of underlying device\n");
        return -1;
    }

    int r = blobfs::blobfs_mkfs(fd.get(), block_count);

    return r;
}

int do_blobfs_check(fbl::unique_fd fd, const blob_options_t& options) {
    fbl::RefPtr<blobfs::Blobfs> vn;
    if (blobfs::blobfs_create(&vn, fbl::move(fd)) < 0) {
        return -1;
    }

    return blobfs::blobfs_check(vn);
}

typedef int (*CommandFunction)(fbl::unique_fd fd, const blob_options_t& options);

struct {
    const char* name;
    CommandFunction func;
    const char* help;
} CMDS[] = {
    {"create", do_blobfs_mkfs, "initialize filesystem"},
    {"mkfs", do_blobfs_mkfs, "initialize filesystem"},
    {"check", do_blobfs_check, "check filesystem integrity"},
    {"fsck", do_blobfs_check, "check filesystem integrity"},
    {"mount", do_blobfs_mount, "mount filesystem"},
};

int usage() {
    fprintf(stderr,
            "usage: blobfs [ <options>* ] <command> [ <arg>* ]\n"
            "\n"
            "options: -r|--readonly  Mount filesystem read-only\n"
            "         -m|--metrics   Collect filesystem metrics\n"
            "         -h|--help      Display this message\n"
            "\n"
            "On Fuchsia, blobfs takes the block device argument by handle.\n"
            "This can make 'blobfs' commands hard to invoke from command line.\n"
            "Try using the [mkfs,fsck,mount,umount] commands instead\n"
            "\n");
    for (unsigned n = 0; n < (sizeof(CMDS) / sizeof(CMDS[0])); n++) {
        fprintf(stderr, "%9s %-10s %s\n", n ? "" : "commands:",
                CMDS[n].name, CMDS[n].help);
    }
    fprintf(stderr, "\n");
    return -1;
}

// Process options/commands and return open fd to device
int process_args(int argc, char** argv, CommandFunction* func, blob_options_t* options) {
    while (1) {
        static struct option opts[] = {
            {"readonly", no_argument, nullptr, 'r'},
            {"metrics", no_argument, nullptr, 'm'},
            {"help", no_argument, nullptr, 'h'},
            {nullptr, 0, nullptr, 0},
        };
        int opt_index;
        int c = getopt_long(argc, argv, "rmh", opts, &opt_index);
        if (c < 0) {
            break;
        }
        switch (c) {
        case 'r':
            options->readonly = true;
            break;
        case 'm':
            options->metrics = true;
            break;
        case 'h':
        default:
            return usage();
        }
    }

    argc -= optind;
    argv += optind;

    if (argc < 1) {
        return usage();
    }
    const char* command = argv[0];

    // Validate command
    for (unsigned i = 0; i < sizeof(CMDS) / sizeof(CMDS[0]); i++) {
        if (!strcmp(command, CMDS[i].name)) {
            *func = CMDS[i].func;
        }
    }

    if (*func == nullptr) {
        fprintf(stderr, "Unknown command: %s\n", command);
        return usage();
    }

    // Block device passed by handle
    return FS_FD_BLOCKDEVICE;
}
} // namespace

int main(int argc, char** argv) {
    CommandFunction func = nullptr;
    blob_options_t options;
    fbl::unique_fd fd(process_args(argc, argv, &func, &options));

    if (!fd) {
        return -1;
    }

    return func(fbl::move(fd), options);
}
