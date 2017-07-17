// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <fbl/ref_ptr.h>
#include <zircon/process.h>
#include <zircon/processargs.h>

#include "blobstore-private.h"
#include <fs/vfs.h>

#ifdef __Fuchsia__
#include <async/loop.h>
#endif

namespace {

#ifdef __Fuchsia__

int do_blobstore_mount(int fd, int argc, char** argv) {
    fbl::RefPtr<blobstore::VnodeBlob> vn;
    if (blobstore::blobstore_mount(&vn, fd) < 0) {
        return -1;
    }
    zx_handle_t h = zx_get_startup_handle(PA_HND(PA_USER0, 0));
    if (h == ZX_HANDLE_INVALID) {
        FS_TRACE_ERROR("blobstore: Could not access startup handle to mount point\n");
        return h;
    }

    async::Loop loop;
    fs::Vfs vfs(loop.async());
    zx_status_t status;
    if ((status = vfs.ServeDirectory(fbl::move(vn), zx::channel(h))) != ZX_OK) {
        return status;
    }
    loop.Run();
    return 0;
}

int do_blobstore_check(int fd, int argc, char** argv) {
    fbl::RefPtr<blobstore::Blobstore> vn;
    if (blobstore::blobstore_create(&vn, fd) < 0) {
        return -1;
    }

    return blobstore::blobstore_check(vn);
}

#else

int do_blobstore_add_blob(int fd, int argc, char** argv) {
    if (argc < 1) {
        fprintf(stderr, "Adding a blob requires an additional file argument\n");
        close(fd);
        return -1;
    }

    int data_fd = open(argv[0], O_RDONLY, 0644);
    if (data_fd < 0) {
        fprintf(stderr, "error: cannot open '%s'\n", argv[0]);
        close(fd);
        return -1;
    }
    int r;
    if ((r = blobstore::blobstore_add_blob(fd, data_fd)) != 0) {
        fprintf(stderr, "blobstore: Failed to add blob '%s'\n", argv[0]);
    }
    close(fd);
    close(data_fd);
    return r;
}

#endif

int do_blobstore_mkfs(int fd, int argc, char** argv) {
    uint64_t block_count;
    if (blobstore::blobstore_get_blockcount(fd, &block_count)) {
        fprintf(stderr, "blobstore: cannot find end of underlying device\n");
        return -1;
    }
    return blobstore::blobstore_mkfs(fd, block_count);
}

struct {
    const char* name;
    int (*func)(int fd, int argc, char** argv);
    const char* help;
} CMDS[] = {
    {"create", do_blobstore_mkfs, "initialize filesystem"},
    {"mkfs", do_blobstore_mkfs, "initialize filesystem"},
#ifdef __Fuchsia__
    {"mount", do_blobstore_mount, "mount filesystem"},
    {"check", do_blobstore_check, "check filesystem integrity"},
    {"fsck", do_blobstore_check, "check filesystem integrity"},
#else
    {"add", do_blobstore_add_blob, "add a blob to a blobstore image"},
#endif
};

int usage() {
    fprintf(stderr,
#ifdef __Fuchsia__
            "usage: blobstore <command> [ <arg>* ]\n"
            "\n"
            "On Fuchsia, blobstore takes the block device argument by handle.\n"
            "This can make 'blobstore' commands hard to invoke from command line.\n"
            "Try using the [mkfs,fsck,mount,umount] commands instead\n"
#else
            "usage: blobstore <file-or-device>[@<size>] <command> [ <arg>* ]\n"
#endif
            "\n");
    for (unsigned n = 0; n < (sizeof(CMDS) / sizeof(CMDS[0])); n++) {
        fprintf(stderr, "%9s %-10s %s\n", n ? "" : "commands:",
                CMDS[n].name, CMDS[n].help);
    }
    fprintf(stderr, "\n");
    return -1;
}

} // namespace

int main(int argc, char** argv) {
    int fd;
#ifdef __Fuchsia__
    if (argc < 2) {
        return usage();
    }
    char* cmd = argv[1];
    // Block device passed by handle
    fd = FS_FD_BLOCKDEVICE;
    argv += 2;
    argc -= 2;
#else
    if (argc < 3) {
        return usage();
    }
    char* device = argv[1];
    char* cmd = argv[2];

    char* sizestr = nullptr;
    if ((sizestr = strchr(device, '@')) != nullptr) {
        // Create a file with an explicitly requested size
        *sizestr++ = 0;
        char* end;
        size_t size = strtoull(sizestr, &end, 10);
        if (end == sizestr) {
            fprintf(stderr, "blobstore: bad size: %s\n", sizestr);
            return usage();
        }
        switch (end[0]) {
        case 'M':
        case 'm':
            size *= (1024 * 1024);
            end++;
            break;
        case 'G':
        case 'g':
            size *= (1024 * 1024 * 1024);
            end++;
            break;
        }
        if (end[0]) {
            fprintf(stderr, "blobstore: bad size: %s\n", sizestr);
            return usage();
        }

        if ((fd = open(device, O_RDWR | O_CREAT, 0644)) < 0) {
            fprintf(stderr, "error: cannot open '%s'\n", device);
            return -1;
        } else if (ftruncate(fd, size)) {
            fprintf(stderr, "error: cannot truncate device '%s'\n", device);
            return -1;
        }
    } else if ((fd = open(device, O_RDWR, 0644)) < 0) {
        // Open a file without an explicit size
        fprintf(stderr, "error: cannot open '%s'\n", device);
        return -1;
    }
    argv += 3;
    argc -= 3;
#endif
    for (unsigned i = 0; i < sizeof(CMDS) / sizeof(CMDS[0]); i++) {
        if (!strcmp(cmd, CMDS[i].name)) {
            return CMDS[i].func(fd, argc, argv);
        }
    }
    close(fd);
    return usage();
}
