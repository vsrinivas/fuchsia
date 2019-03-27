// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <libgen.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <fbl/unique_ptr.h>
#include <fs/trace.h>
#include <fuchsia/hardware/block/c/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <minfs/fsck.h>
#include <minfs/minfs.h>
#include <trace-provider/provider.h>
#include <zircon/compiler.h>
#include <zircon/process.h>
#include <zircon/processargs.h>

#include <utility>

namespace {

int Fsck(fbl::unique_ptr<minfs::Bcache> bc, const minfs::MountOptions& options) {
    return Fsck(std::move(bc));
}

int Mount(fbl::unique_ptr<minfs::Bcache> bc, const minfs::MountOptions& options) {
    zx_handle_t h = zx_take_startup_handle(PA_HND(PA_USER0, 0));
    if (h == ZX_HANDLE_INVALID) {
        FS_TRACE_ERROR("minfs: Could not access startup handle to mount point\n");
        return ZX_ERR_BAD_STATE;
    }

    async::Loop loop(&kAsyncLoopConfigNoAttachToThread);
    trace::TraceProvider trace_provider(loop.dispatcher());

    auto loop_quit = [&loop]() {
        loop.Quit();
        FS_TRACE_WARN("minfs: Unmounted\n");
    };
    zx_status_t status;
    if ((status = MountAndServe(options, loop.dispatcher(), std::move(bc), zx::channel(h),
                                std::move(loop_quit)) != ZX_OK)) {
        if (options.verbose) {
            fprintf(stderr, "minfs: Failed to mount: %d\n", status);
        }
        return -1;
    }

    if (options.verbose) {
        fprintf(stderr, "minfs: Mounted successfully\n");
    }

    loop.Run();
    return 0;
}

int Mkfs(fbl::unique_ptr<minfs::Bcache> bc, const minfs::MountOptions& options) {
    return Mkfs(options, std::move(bc));
}

struct {
    const char* name;
    int (*func)(fbl::unique_ptr<minfs::Bcache> bc, const minfs::MountOptions&);
    uint32_t flags;
    const char* help;
} CMDS[] = {
    {"create", Mkfs, O_RDWR | O_CREAT, "initialize filesystem"},
    {"mkfs", Mkfs, O_RDWR | O_CREAT, "initialize filesystem"},
    {"check", Fsck, O_RDONLY, "check filesystem integrity"},
    {"fsck", Fsck, O_RDONLY, "check filesystem integrity"},
};

int usage() {
    fprintf(stderr, "usage: minfs [ <option>* ] <command> [ <arg>* ]\n"
                    "\n"
                    "options:\n"
                    "    -v|--verbose                  Some debug messages\n"
                    "    -r|--readonly                 Mount filesystem read-only\n"
                    "    -m|--metrics                  Collect filesystem metrics\n"
                    "    -s|--fvm_data_slices SLICES   When mkfs on top of FVM,\n"
                    "                                  preallocate |SLICES| slices of data. \n"
                    "    -h|--help                     Display this message\n"
                    "\n"
                    "On Fuchsia, MinFS takes the block device argument by handle.\n"
                    "This can make 'minfs' commands hard to invoke from command line.\n"
                    "Try using the [mkfs,fsck,mount,umount] commands instead\n"
                    "\n");
    for (unsigned n = 0; n < fbl::count_of(CMDS); n++) {
        fprintf(stderr, "%9s %-10s %s\n", n ? "" : "commands:", CMDS[n].name, CMDS[n].help);
    }
    fprintf(stderr, "%9s %-10s %s\n", "", "mount", "mount filesystem");
    fprintf(stderr, "\n");
    return -1;
}

zx_status_t GetInfo(const fbl::unique_fd& fd, off_t* out_size, bool* out_readonly) {
    fuchsia_hardware_block_BlockInfo info;
    const fzl::UnownedFdioCaller connection(fd.get());
    zx_status_t status;
    zx_status_t io_status =
            fuchsia_hardware_block_BlockGetInfo(connection.borrow_channel(), &status, &info);
    if (io_status != ZX_OK) {
        status = io_status;
    }
    if (status != ZX_OK) {
        fprintf(stderr, "error: minfs could not find size of device\n");
        return status;
    }
    *out_size = info.block_size * info.block_count;
    *out_readonly = info.flags & fuchsia_hardware_block_FLAG_READONLY;
    return ZX_OK;
}

} // namespace

int main(int argc, char** argv) {
    minfs::MountOptions options;
    options.readonly = false;
    options.metrics = false;
    options.verbose = false;

    while (1) {
        static struct option opts[] = {
            {"readonly", no_argument, nullptr, 'r'},
            {"metrics", no_argument, nullptr, 'm'},
            {"journal", no_argument, nullptr, 'j'},
            {"verbose", no_argument, nullptr, 'v'},
            {"fvm_data_slices", required_argument, nullptr, 's'},
            {"help", no_argument, nullptr, 'h'},
            {nullptr, 0, nullptr, 0},
        };
        int opt_index;
        int c = getopt_long(argc, argv, "rmjvhs:", opts, &opt_index);
        if (c < 0) {
            break;
        }
        switch (c) {
        case 'r':
            options.readonly = true;
            break;
        case 'm':
            options.metrics = true;
            break;
        case 'j':
            // TODO(planders): Enable journaling here once minfs supports it.
            fprintf(stderr, "minfs: Journaling option not supported\n");
            return usage();
        case 'v':
            options.verbose = true;
            break;
        case 's':
            options.fvm_data_slices = static_cast<uint32_t>(strtoul(optarg, NULL, 0));
            break;
        case 'h':
        default:
            return usage();
        }
    }

    argc -= optind;
    argv += optind;

    // Block device passed by handle
    if (argc != 1) {
        return usage();
    }
    char* cmd = argv[0];

    fbl::unique_fd fd;
    fd.reset(FS_FD_BLOCKDEVICE);

    off_t device_size = 0;
    bool block_readonly = false;
    zx_status_t status = GetInfo(fd, &device_size, &block_readonly);
    if (status != ZX_OK) {
        return status;
    }

    options.readonly = options.readonly || block_readonly;

    if (device_size == 0) {
        fprintf(stderr, "minfs: failed to access block device\n");
        return usage();
    }
    size_t block_count = device_size / minfs::kMinfsBlockSize;

    fbl::unique_ptr<minfs::Bcache> bc;
    if (minfs::Bcache::Create(&bc, std::move(fd), static_cast<uint32_t>(block_count)) < 0) {
        fprintf(stderr, "minfs: error: cannot create block cache\n");
        return -1;
    }

    if (!strcmp(cmd, "mount")) {
        return Mount(std::move(bc), options);
    }

    for (unsigned i = 0; i < fbl::count_of(CMDS); i++) {
        if (!strcmp(cmd, CMDS[i].name)) {
            int r = CMDS[i].func(std::move(bc), options);
            if (options.verbose) {
                fprintf(stderr, "minfs: %s completed with result: %d\n", cmd, r);
            }
            return r;
        }
    }
    return -1;
}
