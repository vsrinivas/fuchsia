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
#include <lib/zx/channel.h>
#include <blobfs/blobfs.h>
#include <blobfs/fsck.h>
#include <fbl/auto_call.h>
#include <fbl/string.h>
#include <fbl/unique_fd.h>
#include <fbl/unique_ptr.h>
#include <fbl/vector.h>
#include <fs/vfs.h>
#include <trace-provider/provider.h>
#include <zircon/process.h>
#include <zircon/processargs.h>

namespace {

int Mount(fbl::unique_fd fd, blobfs::MountOptions* options) {
    if (!options->readonly) {
        block_info_t block_info;
        zx_status_t status = static_cast<zx_status_t>(ioctl_block_get_info(fd.get(), &block_info));
        if (status < ZX_OK) {
            FS_TRACE_ERROR("blobfs: Unable to query block device, fd: %d status: 0x%x\n",
                           fd.get(), status);
            return -1;
        }
        options->readonly = block_info.flags & BLOCK_FLAG_READONLY;
    }

    zx::channel root = zx::channel(zx_take_startup_handle(PA_HND(PA_USER0, 0)));
    if (!root.is_valid()) {
        FS_TRACE_ERROR("blobfs: Could not access startup handle to mount point\n");
        return -1;
    }

    async::Loop loop(&kAsyncLoopConfigNoAttachToThread);
    trace::TraceProvider provider(loop.dispatcher());
    auto loop_quit = [&loop]() { loop.Quit(); };
    if (blobfs::Mount(loop.dispatcher(), fbl::move(fd), *options,
                            fbl::move(root), fbl::move(loop_quit)) != ZX_OK) {
        return -1;
    }
    loop.Run();
    return ZX_OK;
}

int Mkfs(fbl::unique_fd fd, blobfs::MountOptions* options) {
    uint64_t block_count;
    if (blobfs::GetBlockCount(fd.get(), &block_count)) {
        fprintf(stderr, "blobfs: cannot find end of underlying device\n");
        return -1;
    }

    return blobfs::Mkfs(fd.get(), block_count);
}

int Fsck(fbl::unique_fd fd, blobfs::MountOptions* options) {
    fbl::unique_ptr<blobfs::Blobfs> blobfs;
    if (blobfs::Initialize(fbl::move(fd), *options, &blobfs) != ZX_OK) {
        return -1;
    }

    return blobfs::Fsck(fbl::move(blobfs));
}

typedef int (*CommandFunction)(fbl::unique_fd fd, blobfs::MountOptions* options);

const struct {
    const char* name;
    CommandFunction func;
    const char* help;
} kCmds[] = {
    {"create", Mkfs, "initialize filesystem"},
    {"mkfs", Mkfs, "initialize filesystem"},
    {"check", Fsck, "check filesystem integrity"},
    {"fsck", Fsck, "check filesystem integrity"},
    {"mount", Mount, "mount filesystem"},
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
    for (unsigned n = 0; n < (sizeof(kCmds) / sizeof(kCmds[0])); n++) {
        fprintf(stderr, "%9s %-10s %s\n", n ? "" : "commands:",
                kCmds[n].name, kCmds[n].help);
    }
    fprintf(stderr, "\n");
    return -1;
}

// Process options/commands and return open fd to device
int ProcessArgs(int argc, char** argv, CommandFunction* func, blobfs::MountOptions* options) {
    while (1) {
        static struct option opts[] = {
            {"readonly", no_argument, nullptr, 'r'},
            {"metrics", no_argument, nullptr, 'm'},
            {"journal", no_argument, nullptr, 'j'},
            {"help", no_argument, nullptr, 'h'},
            {nullptr, 0, nullptr, 0},
        };
        int opt_index;
        int c = getopt_long(argc, argv, "rmjh", opts, &opt_index);
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
        case 'j':
            options->journal = true;
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
    for (unsigned i = 0; i < sizeof(kCmds) / sizeof(kCmds[0]); i++) {
        if (!strcmp(command, kCmds[i].name)) {
            *func = kCmds[i].func;
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
    blobfs::MountOptions options;
    fbl::unique_fd fd(ProcessArgs(argc, argv, &func, &options));

    if (!fd) {
        return -1;
    }

    return func(fbl::move(fd), &options);
}
