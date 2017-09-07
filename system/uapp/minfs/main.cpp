// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// for S_IF*
#define _XOPEN_SOURCE
#include <dirent.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <magenta/compiler.h>
#include <magenta/process.h>
#include <magenta/processargs.h>
#include <fbl/unique_ptr.h>

#ifdef __Fuchsia__
#include <async/loop.h>
#include <fs/async-dispatcher.h>
#endif

#include "minfs-private.h"
#ifndef __Fuchsia__
#include "host.h"
#endif

#ifndef __Fuchsia__

extern fbl::RefPtr<minfs::VnodeMinfs> fake_root;

int run_fs_tests(int argc, char** argv);

#endif

namespace {

int do_minfs_check(fbl::unique_ptr<minfs::Bcache> bc, int argc, char** argv) {
#ifdef __Fuchsia__
    return minfs_check(fbl::move(bc));
#else
    return -1;
#endif
}

#ifdef __Fuchsia__
int do_minfs_mount(fbl::unique_ptr<minfs::Bcache> bc, int argc, char** argv) {
    fbl::RefPtr<minfs::VnodeMinfs> vn;
    if (minfs_mount(&vn, fbl::move(bc)) < 0) {
        return -1;
    }

    mx_handle_t h = mx_get_startup_handle(PA_HND(PA_USER0, 0));
    if (h == MX_HANDLE_INVALID) {
        FS_TRACE_ERROR("minfs: Could not access startup handle to mount point\n");
        return MX_ERR_BAD_STATE;
    }

    async::Loop loop;
    fs::AsyncDispatcher dispatcher(loop.async());
    minfs::vfs.SetDispatcher(&dispatcher);
    mx_status_t status;
    if ((status = minfs::vfs.ServeDirectory(fbl::move(vn),
                                            mx::channel(h))) != MX_OK) {
        return status;
    }
    loop.Run();
    return 0;
}
#else
int io_setup(fbl::unique_ptr<minfs::Bcache> bc) {
    fbl::RefPtr<minfs::VnodeMinfs> vn;
    if (minfs_mount(&vn, fbl::move(bc)) < 0) {
        return -1;
    }
    fake_root = vn;
    return 0;
}

int do_minfs_test(fbl::unique_ptr<minfs::Bcache> bc, int argc, char** argv) {
    if (io_setup(fbl::move(bc))) {
        return -1;
    }
    return run_fs_tests(argc, argv);
}

int do_cp(fbl::unique_ptr<minfs::Bcache> bc, int argc, char** argv) {
    if (argc != 2) {
        fprintf(stderr, "cp requires two arguments\n");
        return -1;
    }

    if (io_setup(fbl::move(bc))) {
        return -1;
    }

    int fdi, fdo;
    if ((fdi = emu_open(argv[0], O_RDONLY, 0)) < 0) {
        fprintf(stderr, "error: cannot open '%s'\n", argv[0]);
        return -1;
    }
    if ((fdo = emu_open(argv[1], O_WRONLY | O_CREAT | O_EXCL, 0644)) < 0) {
        fprintf(stderr, "error: cannot open '%s'\n", argv[1]);
        return -1;
    }

    char buffer[256 * 1024];
    ssize_t r;
    for (;;) {
        if ((r = emu_read(fdi, buffer, sizeof(buffer))) < 0) {
            fprintf(stderr, "error: reading from '%s'\n", argv[0]);
            break;
        } else if (r == 0) {
            break;
        }
        void* ptr = buffer;
        ssize_t len = r;
        while (len > 0) {
            if ((r = emu_write(fdo, ptr, len)) < 0) {
                fprintf(stderr, "error: writing to '%s'\n", argv[1]);
                goto done;
            }
            ptr = (void*)((uintptr_t)ptr + r);
            len -= r;
        }
    }
done:
    emu_close(fdi);
    emu_close(fdo);
    return r;
}

int do_mkdir(fbl::unique_ptr<minfs::Bcache> bc, int argc, char** argv) {
    if (argc != 1) {
        fprintf(stderr, "mkdir requires one argument\n");
        return -1;
    }
    if (io_setup(fbl::move(bc))) {
        return -1;
    }
    // TODO(jpoichet) add support making parent directories when not present
    const char* path = argv[0];
    if (strncmp(path, PATH_PREFIX, PREFIX_SIZE)) {
        fprintf(stderr, "error: mkdir can only operate minfs paths (must start with %s)\n", PATH_PREFIX);
        return -1;
    }
    return emu_mkdir(path, 0);
}

int do_unlink(fbl::unique_ptr<minfs::Bcache> bc, int argc, char** argv) {
    if (argc != 1) {
        fprintf(stderr, "unlink requires one argument\n");
        return -1;
    }
    if (io_setup(fbl::move(bc))) {
        return -1;
    }
    const char* path = argv[0];
    if (strncmp(path, PATH_PREFIX, PREFIX_SIZE)) {
        fprintf(stderr, "error: unlink can only operate minfs paths (must start with %s)\n", PATH_PREFIX);
        return -1;
    }
    return emu_unlink(path);
}

int do_rename(fbl::unique_ptr<minfs::Bcache> bc, int argc, char** argv) {
    if (argc != 2) {
        fprintf(stderr, "rename requires two arguments\n");
        return -1;
    }
    if (io_setup(fbl::move(bc))) {
        return -1;
    }
    const char* old_path = argv[0];
    const char* new_path = argv[1];
    if (strncmp(old_path, PATH_PREFIX, PREFIX_SIZE)) {
        fprintf(stderr, "error: rename can only operate minfs paths (must start with %s)\n", PATH_PREFIX);
        return -1;
    }
    if (strncmp(new_path, PATH_PREFIX, PREFIX_SIZE)) {
        fprintf(stderr, "error: rename can only operate minfs paths (must start with %s)\n", PATH_PREFIX);
        return -1;
    }
    return emu_rename(old_path, new_path);
}

static const char* modestr(uint32_t mode) {
    switch (mode & S_IFMT) {
    case S_IFREG:
        return "-";
    case S_IFCHR:
        return "c";
    case S_IFBLK:
        return "b";
    case S_IFDIR:
        return "d";
    default:
        return "?";
    }
}

int do_ls(fbl::unique_ptr<minfs::Bcache> bc, int argc, char** argv) {
    if (argc != 1) {
        fprintf(stderr, "ls requires one argument\n");
        return -1;
    }
    if (io_setup(fbl::move(bc))) {
        return -1;
    }
    const char* path = argv[0];
    if (strncmp(path, PATH_PREFIX, PREFIX_SIZE)) {
        fprintf(stderr, "error: ls can only operate minfs paths (must start with %s)\n", PATH_PREFIX);
        return -1;
    }

    DIR* d = emu_opendir(path);
    if (!d) {
        return -1;
    }

    struct dirent* de;
    char tmp[2048];
    struct stat s;
    while ((de = emu_readdir(d)) != nullptr) {
        if (strcmp(de->d_name, ".") && strcmp(de->d_name, "..")) {
            memset(&s, 0, sizeof(struct stat));
            if ((strlen(de->d_name) + strlen(path) + 2) <= sizeof(tmp)) {
                snprintf(tmp, sizeof(tmp), "%s/%s", path, de->d_name);
                emu_stat(tmp, &s);
            }
            fprintf(stdout, "%s %8jd %s\n", modestr(s.st_mode), (intmax_t)s.st_size, de->d_name);
        }
    }
    emu_closedir(d);
    return 0;
}

#endif

int do_minfs_mkfs(fbl::unique_ptr<minfs::Bcache> bc, int argc, char** argv) {
    return minfs_mkfs(fbl::move(bc));
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
#ifdef __Fuchsia__
    {"mount", do_minfs_mount, O_RDWR, "mount filesystem"},
#else
    {"test", do_minfs_test, O_RDWR, "run tests against filesystem"},
    {"cp", do_cp, O_RDWR, "copy to/from fs"},
    {"mkdir", do_mkdir, O_RDWR, "create directory"},
    {"rm", do_unlink, O_RDWR, "delete file or directory"},
    {"unlink", do_unlink, O_RDWR, "delete file or directory"},
    {"mv", do_rename, O_RDWR, "rename file or directory"},
    {"rename", do_rename, O_RDWR, "rename file or directory"},
    {"ls", do_ls, O_RDWR, "list content of directory"},
#endif
};

int usage() {
    fprintf(stderr,
            "usage: minfs [ <option>* ] <file-or-device>[@<size>] <command> [ <arg>* ]\n"
            "\n"
            "options:  -v         some debug messages\n"
            "          -vv        all debug messages\n"
#ifdef __Fuchsia__
            "\n"
            "On Fuchsia, MinFS takes the block device argument by handle.\n"
            "This can make 'minfs' commands hard to invoke from command line.\n"
            "Try using the [mkfs,fsck,mount,umount] commands instead\n"
#endif
            "\n");
    for (unsigned n = 0; n < fbl::count_of(CMDS); n++) {
        fprintf(stderr, "%9s %-10s %s\n", n ? "" : "commands:",
                CMDS[n].name, CMDS[n].help);
    }
    fprintf(stderr, "\n");
    return -1;
}

off_t get_size(int fd) {
#ifdef __Fuchsia__
    block_info_t info;
    if (ioctl_block_get_info(fd, &info) != sizeof(info)) {
        fprintf(stderr, "error: minfs could not find size of device\n");
        return 0;
    }
    return info.block_size * info.block_count;
#else
    struct stat s;
    if (fstat(fd, &s) < 0) {
        fprintf(stderr, "error: minfs could not find end of file/device\n");
        return 0;
    }
    return s.st_size;
#endif
}

} // namespace anonymous

int main(int argc, char** argv) {
    off_t size = 0;

    // handle options
    while (argc > 1) {
        if (!strcmp(argv[1], "-v")) {
            fs_trace_on(FS_TRACE_SOME);
        } else if (!strcmp(argv[1], "-vv")) {
            fs_trace_on(FS_TRACE_ALL);
        } else {
            break;
        }
        argc--;
        argv++;
    }

#ifdef __Fuchsia__
    // Block device passed by handle
    if (argc < 2) {
        return usage();
    }
    char* cmd = argv[1];
#else
    // Block device passed by path
    if (argc < 3) {
        return usage();
    }
    char* fn = argv[1];
    char* cmd = argv[2];
    char* sizestr;
    if ((sizestr = strchr(fn, '@')) != nullptr) {
        *sizestr++ = 0;
        char* end;
        size = strtoull(sizestr, &end, 10);
        if (end == sizestr) {
            fprintf(stderr, "minfs: bad size: %s\n", sizestr);
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
            fprintf(stderr, "minfs: bad size: %s\n", sizestr);
            return usage();
        }
    }
#endif

    int fd;
#ifdef __Fuchsia__
    fd = FS_FD_BLOCKDEVICE;
#else
    uint32_t flags = O_RDWR;
    for (unsigned i = 0; i < fbl::count_of(CMDS); i++) {
        if (!strcmp(cmd, CMDS[i].name)) {
            flags = CMDS[i].flags;
            goto found;
        }
    }
    fprintf(stderr, "minfs: unknown command: %s\n", cmd);
    return usage();
found:
    if ((fd = open(fn, flags, 0644)) < 0) {
        if (flags & O_CREAT) {
            // temporary workaround for Magenta devfs issue
            flags &= (~O_CREAT);
            goto found;
        }
        fprintf(stderr, "error: cannot open '%s'\n", fn);
        return -1;
    }
#endif
    if (size == 0) {
        size = get_size(fd);
        if (size == 0) {
            fprintf(stderr, "minfs: failed to access block device\n");
            return usage();
        }
    }
    size /= minfs::kMinfsBlockSize;

    fbl::unique_ptr<minfs::Bcache> bc;
    if (minfs::Bcache::Create(&bc, fd, (uint32_t) size) < 0) {
        fprintf(stderr, "error: cannot create block cache\n");
        return -1;
    }

    for (unsigned i = 0; i < fbl::count_of(CMDS); i++) {
        if (!strcmp(cmd, CMDS[i].name)) {
            return CMDS[i].func(fbl::move(bc), argc - 3, argv + 3);
        }
    }
    return -1;
}
