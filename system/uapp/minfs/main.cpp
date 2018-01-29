// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <async/cpp/loop.h>
#include <fbl/unique_free_ptr.h>
#include <fbl/unique_ptr.h>
#include <fs/trace.h>
#include <minfs/fsck.h>
#include <minfs/minfs.h>
#include <trace-provider/provider.h>
#include <zircon/compiler.h>
#include <zircon/process.h>
#include <zircon/processargs.h>

namespace {

int do_minfs_check(fbl::unique_ptr<minfs::Bcache> bc, int argc, char** argv) {
    return minfs_check(fbl::move(bc));
}

int do_minfs_mount(fbl::unique_ptr<minfs::Bcache> bc, bool readonly) {
    zx_handle_t h = zx_get_startup_handle(PA_HND(PA_USER0, 0));
    if (h == ZX_HANDLE_INVALID) {
        FS_TRACE_ERROR("minfs: Could not access startup handle to mount point\n");
        return ZX_ERR_BAD_STATE;
    }

    async::Loop loop;
    fs::Vfs vfs(loop.async());
    trace::TraceProvider trace_provider(loop.async());
    vfs.SetReadonly(readonly);

    if (MountAndServe(&vfs, fbl::move(bc), zx::channel(h)) != ZX_OK) {
        return -1;
    }

    loop.Run();
    return 0;
}

int do_minfs_mkfs(fbl::unique_ptr<minfs::Bcache> bc, int argc, char** argv) {
    return Mkfs(fbl::move(bc));
}

struct {
    const char* name;
    int (*func)(fbl::unique_ptr<minfs::Bcache> bc, int argc, char** argv);
    uint32_t flags;
    const char* help;
} CMDS[] = {
    {"create", do_minfs_mkfs, O_RDWR | O_CREAT, "initialize filesystem"},
    {"mkfs", do_minfs_mkfs, O_RDWR | O_CREAT, "initialize filesystem"},
    {"check", do_minfs_check, O_RDONLY, "check filesystem integrity"},
    {"fsck", do_minfs_check, O_RDONLY, "check filesystem integrity"},
};

int usage() {
    fprintf(stderr,
            "usage: minfs [ <option>* ] <file-or-device>[@<size>] <command> [ <arg>* ]\n"
            "\n"
            "options:  -v               some debug messages\n"
            "          -vv              all debug messages\n"
            "          --readonly       Mount filesystem read-only\n"
            "\n"
            "On Fuchsia, MinFS takes the block device argument by handle.\n"
            "This can make 'minfs' commands hard to invoke from command line.\n"
            "Try using the [mkfs,fsck,mount,umount] commands instead\n"
            "\n");
    for (unsigned n = 0; n < fbl::count_of(CMDS); n++) {
        fprintf(stderr, "%9s %-10s %s\n", n ? "" : "commands:",
                CMDS[n].name, CMDS[n].help);
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
    off_t size = 0;
    bool readonly = false;
    __UNUSED off_t offset = 0;
    off_t length = 0;

    // handle options
    while (argc > 1) {
        if (!strcmp(argv[1], "--readonly")) {
            readonly = true;
        } else {
            break;
        }
        argc--;
        argv++;
    }

    // Block device passed by handle
    if (argc < 2) {
        return usage();
    }
    char* cmd = argv[1];

    fbl::unique_fd fd;
    fd.reset(FS_FD_BLOCKDEVICE);
    if (!readonly) {
        block_info_t block_info;
        zx_status_t status = static_cast<zx_status_t>(ioctl_block_get_info(fd.get(), &block_info));
        if (status < ZX_OK) {
            fprintf(stderr, "minfs: Unable to query block device, fd: %d status: 0x%x\n", fd.get(),
                    status);
            return -1;
        }
        readonly = block_info.flags & BLOCK_FLAG_READONLY;
    }

    if (size == 0) {
        size = get_size(fd.get());
        if (size == 0) {
            fprintf(stderr, "minfs: failed to access block device\n");
            return usage();
        }
    }

    if (length > size) {
        fprintf(stderr, "Invalid length\n");
        return usage();
    } else if (length > 0) {
        size = length;
    }

    size /= minfs::kMinfsBlockSize;

    fbl::unique_ptr<minfs::Bcache> bc;
    if (minfs::Bcache::Create(&bc, fbl::move(fd), (uint32_t)size) < 0) {
        fprintf(stderr, "error: cannot create block cache\n");
        return -1;
    }

    if (!strcmp(cmd, "mount")) {
        return do_minfs_mount(fbl::move(bc), readonly);
    }

    for (unsigned i = 0; i < fbl::count_of(CMDS); i++) {
        if (!strcmp(cmd, CMDS[i].name)) {
            return CMDS[i].func(fbl::move(bc), argc - 3, argv + 3);
        }
    }
    return -1;
}
