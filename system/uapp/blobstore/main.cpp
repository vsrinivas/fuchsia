// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <fcntl.h>
#include <libgen.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <async/cpp/loop.h>
#include <blobstore/blobstore.h>
#include <blobstore/fsck.h>
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

#define MIN_ARGS 2

typedef struct {
    bool readonly = false;
    uint64_t data_blocks = blobstore::kStartBlockMinimum; // Account for reserved blocks
    fbl::Vector<fbl::String> blob_list;
} blob_options_t;

int do_blobstore_mount(fbl::unique_fd fd, const blob_options_t& options) {
    bool readonly = options.readonly;
    if (!readonly) {
        block_info_t block_info;
        zx_status_t status = static_cast<zx_status_t>(ioctl_block_get_info(fd.get(), &block_info));
        if (status < ZX_OK) {
            FS_TRACE_ERROR("blobstore: Unable to query block device, fd: %d status: 0x%x\n",
                            fd.get(), status);
            return -1;
        }
        readonly = block_info.flags & BLOCK_FLAG_READONLY;
    }

    fbl::RefPtr<blobstore::VnodeBlob> vn;
    if (blobstore::blobstore_mount(&vn, fbl::move(fd)) < 0) {
        return -1;
    }
    zx_handle_t h = zx_get_startup_handle(PA_HND(PA_USER0, 0));
    if (h == ZX_HANDLE_INVALID) {
        FS_TRACE_ERROR("blobstore: Could not access startup handle to mount point\n");
        return -1;
    }

    async::Loop loop;
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

int do_blobstore_mkfs(fbl::unique_fd fd, const blob_options_t& options) {
    uint64_t block_count;
    if (blobstore::blobstore_get_blockcount(fd.get(), &block_count)) {
        fprintf(stderr, "blobstore: cannot find end of underlying device\n");
        return -1;
    }

    int r = blobstore::blobstore_mkfs(fd.get(), block_count);

    return r;
}

int do_blobstore_check(fbl::unique_fd fd, const blob_options_t& options) {
    fbl::RefPtr<blobstore::Blobstore> vn;
    if (blobstore::blobstore_create(&vn, fbl::move(fd)) < 0) {
        return -1;
    }

    return blobstore::blobstore_check(vn);
}

typedef int (*CommandFunction)(fbl::unique_fd fd, const blob_options_t& options);

struct {
    const char* name;
    CommandFunction func;
    const char* help;
} CMDS[] = {
    {"create", do_blobstore_mkfs, "initialize filesystem"},
    {"mkfs", do_blobstore_mkfs, "initialize filesystem"},
    {"check", do_blobstore_check, "check filesystem integrity"},
    {"fsck", do_blobstore_check, "check filesystem integrity"},
    {"mount", do_blobstore_mount, "mount filesystem"},
};

int usage() {
    fprintf(stderr,
            "usage: blobstore [ <options>* ] <command> [ <arg>* ]\n"
            "\n"
            "options: --readonly  Mount filesystem read-only\n"
            "\n"
            "On Fuchsia, blobstore takes the block device argument by handle.\n"
            "This can make 'blobstore' commands hard to invoke from command line.\n"
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
    if (argc < MIN_ARGS) {
        fprintf(stderr, "Not enough args\n");
        return usage();
    }

    argc--;
    argv++;

    // Read options
    while (argc > 1) {
        if (!strcmp(argv[0], "--readonly")) {
            options->readonly = true;
        } else {
            break;
        }
        argc--;
        argv++;
    }

    char* command = argv[0];

    // Validate command
    for (unsigned i = 0; i < sizeof(CMDS) / sizeof(CMDS[0]); i++) {
        if (!strcmp(command, CMDS[i].name)) {
            *func = CMDS[i].func;
        }
    }

    if (*func == nullptr) {
        fprintf(stderr, "Unknown command: %s\n", argv[0]);
        return usage();
    }

    argc--;
    argv++;

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
