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

#include <fbl/unique_free_ptr.h>
#include <fbl/unique_ptr.h>
#include <fs/trace.h>
#include <lib/async-loop/cpp/loop.h>
#include <minfs/fsck.h>
#include <minfs/minfs.h>
#include <trace-provider/provider.h>
#include <zircon/compiler.h>
#include <zircon/process.h>
#include <zircon/processargs.h>

namespace {

int do_minfs_check(fbl::unique_ptr<minfs::Bcache> bc, const minfs::Options& options) {
    return minfs_check(fbl::move(bc));
}

int do_minfs_mount(fbl::unique_ptr<minfs::Bcache> bc, const minfs::Options& options) {
    zx_handle_t h = zx_take_startup_handle(PA_HND(PA_USER0, 0));
    if (h == ZX_HANDLE_INVALID) {
        FS_TRACE_ERROR("minfs: Could not access startup handle to mount point\n");
        return ZX_ERR_BAD_STATE;
    }

    async::Loop loop(&kAsyncLoopConfigNoAttachToThread);
    trace::TraceProvider trace_provider(loop.dispatcher());

    auto loop_quit = [&loop]() { loop.Quit(); };
    zx_status_t status;
    if ((status = MountAndServe(&options, loop.dispatcher(), fbl::move(bc), zx::channel(h),
                                fbl::move(loop_quit)) != ZX_OK)) {
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

int do_minfs_mkfs(fbl::unique_ptr<minfs::Bcache> bc, const minfs::Options& options) {
    return Mkfs(options, fbl::move(bc));
}

struct {
    const char* name;
    int (*func)(fbl::unique_ptr<minfs::Bcache> bc, const minfs::Options&);
    uint32_t flags;
    const char* help;
} CMDS[] = {
    {"create", do_minfs_mkfs, O_RDWR | O_CREAT, "initialize filesystem"},
    {"mkfs", do_minfs_mkfs, O_RDWR | O_CREAT, "initialize filesystem"},
    {"check", do_minfs_check, O_RDONLY, "check filesystem integrity"},
    {"fsck", do_minfs_check, O_RDONLY, "check filesystem integrity"},
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

off_t get_size(int fd) {
    block_info_t info;
    if (ioctl_block_get_info(fd, &info) != sizeof(info)) {
        fprintf(stderr, "error: minfs could not find size of device\n");
        return 0;
    }
    return info.block_size * info.block_count;
}

} // namespace

int main(int argc, char** argv) {
    minfs::minfs_options_t options;
    options.readonly = false;
    options.metrics = false;
    options.verbose = false;

    while (1) {
        static struct option opts[] = {
            {"readonly", no_argument, nullptr, 'r'},
            {"metrics", no_argument, nullptr, 'm'},
            {"verbose", no_argument, nullptr, 'v'},
            {"fvm_data_slices", required_argument, nullptr, 's'},
            {"help", no_argument, nullptr, 'h'},
            {nullptr, 0, nullptr, 0},
        };
        int opt_index;
        int c = getopt_long(argc, argv, "rmvhs:", opts, &opt_index);
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
    if (!options.readonly) {
        block_info_t block_info;
        zx_status_t status = static_cast<zx_status_t>(ioctl_block_get_info(fd.get(), &block_info));
        if (status < ZX_OK) {
            fprintf(stderr, "minfs: Unable to query block device, fd: %d status: 0x%x\n", fd.get(),
                    status);
            return -1;
        }
        options.readonly = block_info.flags & BLOCK_FLAG_READONLY;
    }

    off_t size = get_size(fd.get());
    if (size == 0) {
        fprintf(stderr, "minfs: failed to access block device\n");
        return usage();
    }
    size /= minfs::kMinfsBlockSize;

    fbl::unique_ptr<minfs::Bcache> bc;
    if (minfs::Bcache::Create(&bc, fbl::move(fd), (uint32_t)size) < 0) {
        fprintf(stderr, "minfs: error: cannot create block cache\n");
        return -1;
    }

    if (!strcmp(cmd, "mount")) {
        return do_minfs_mount(fbl::move(bc), options);
    }

    for (unsigned i = 0; i < fbl::count_of(CMDS); i++) {
        if (!strcmp(cmd, CMDS[i].name)) {
            int r = CMDS[i].func(fbl::move(bc), options);
            if (options.verbose) {
                fprintf(stderr, "minfs: %s completed with result: %d\n", cmd, r);
            }
            return r;
        }
    }
    return -1;
}
