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

#include <fbl/unique_ptr.h>
#include <zircon/compiler.h>
#include <zircon/process.h>
#include <zircon/processargs.h>

#ifdef __Fuchsia__
#include <async/loop.h>
#include <fs/trace.h>
#endif

#include "minfs-private.h"
#ifndef __Fuchsia__
#include <minfs/host.h>
#endif

#ifndef __Fuchsia__

extern fbl::RefPtr<minfs::VnodeMinfs> fake_root;

#endif

namespace {

int do_minfs_check(fbl::unique_ptr<minfs::Bcache> bc, int argc, char** argv) {
    return minfs_check(fbl::move(bc));
}

#ifdef __Fuchsia__
int do_minfs_mount(fbl::unique_ptr<minfs::Bcache> bc, bool readonly) {
    fbl::RefPtr<minfs::VnodeMinfs> vn;
    if (minfs_mount(&vn, fbl::move(bc)) < 0) {
        return -1;
    }

    zx_handle_t h = zx_get_startup_handle(PA_HND(PA_USER0, 0));
    if (h == ZX_HANDLE_INVALID) {
        FS_TRACE_ERROR("minfs: Could not access startup handle to mount point\n");
        return ZX_ERR_BAD_STATE;
    }

    async::Loop loop;
    fs::Vfs vfs(loop.async());
    vfs.SetReadonly(readonly);
    zx_status_t status;
    if ((status = vfs.ServeDirectory(fbl::move(vn), zx::channel(h))) != ZX_OK) {
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

int cp_file(const char* src_path, const char* dst_path) {
    FileWrapper src;
    FileWrapper dst;

    if (FileWrapper::Open(src_path, O_RDONLY, 0, &src) < 0) {
        fprintf(stderr, "error: cannot open '%s'\n", src_path);
        return -1;
    }
    if (FileWrapper::Open(dst_path, O_WRONLY | O_CREAT | O_EXCL, 0644, &dst) < 0) {
        fprintf(stderr, "error: cannot open '%s'\n", dst_path);
        return -1;
    }

    char buffer[256 * 1024];
    ssize_t r;
    for (;;) {
        if ((r = src.Read(buffer, sizeof(buffer))) < 0) {
            fprintf(stderr, "error: reading from '%s'\n", src_path);
            break;
        } else if (r == 0) {
            break;
        }
        void* ptr = buffer;
        ssize_t len = r;
        while (len > 0) {
            if ((r = dst.Write(ptr, len)) < 0) {
                fprintf(stderr, "error: writing to '%s'\n", dst_path);
                goto done;
            }
            ptr = (void*)((uintptr_t)ptr + r);
            len -= r;
        }
    }
done:
    return r;
}

int do_cp(fbl::unique_ptr<minfs::Bcache> bc, int argc, char** argv) {
    if (argc != 2) {
        fprintf(stderr, "cp requires two arguments\n");
        return -1;
    }

    if (io_setup(fbl::move(bc))) {
        return -1;
    }

    return cp_file(argv[0], argv[1]);
}

// Add PATH_PREFIX to path if it isn't there
void get_emu_path(const char* path, char* out) {
    out[0] = 0;
    int remaining = PATH_MAX;

    if (strncmp(path, PATH_PREFIX, PREFIX_SIZE)) {
        strncat(out, PATH_PREFIX, remaining);
        remaining -= strlen(PATH_PREFIX);
    }

    strncat(out, path, remaining);
}

// Process line in |manifest|, add directories as needed and copy src file to dst
// Returns "ZX_ERR_OUT_OF_RANGE" when manifest has reached EOF.
zx_status_t process_manifest_line(FILE* manifest) {
    size_t size = 0;
    char* line = nullptr;

    int r = getline(&line, &size, manifest);

    if (r < 0) {
        return ZX_ERR_OUT_OF_RANGE;
    }

    fbl::unique_free_ptr<char> ptr(line);

    // Exit early if line is commented out
    if (line[0] == '#') {
        return ZX_OK;
    }

    char* eq_ptr = strchr(line, '=');

    if (eq_ptr == nullptr) {
        fprintf(stderr, "Not enough '=' in input\n");
        return ZX_ERR_INVALID_ARGS;
    }

    if (strchr(eq_ptr+1, '=') != nullptr) {
        fprintf(stderr, "Too many '=' in input\n");
        return ZX_ERR_INVALID_ARGS;
    }

    char* nl_ptr = strchr(line, '\n');
    if (nl_ptr != nullptr) {
        *nl_ptr = '\0';
    }

    *eq_ptr = '\0';
    char src[PATH_MAX];
    char dst[PATH_MAX];
    strncpy(dst, line, PATH_MAX);
    strncpy(src, eq_ptr + 1, PATH_MAX);

    // Create directories if they don't exist
    char* sl_ptr = strchr(dst, '/');
    while (sl_ptr != nullptr) {
        *sl_ptr = '\0';

        char emu_dir[PATH_MAX];
        get_emu_path(dst, emu_dir);

        DIR* d = emu_opendir(emu_dir);

        if (d) {
            emu_closedir(d);
        } else if (emu_mkdir(emu_dir, 0) < 0) {
            fprintf(stderr, "Failed to create directory %s\n", emu_dir);
            return ZX_ERR_INTERNAL;
        }

        *sl_ptr = '/';
        sl_ptr = strchr(sl_ptr + 1, '/');
    }

    // Copy src to dst
    char emu_dst[PATH_MAX];
    get_emu_path(dst, emu_dst);
    if (cp_file(src, emu_dst) < 0) {
        fprintf(stderr, "Failed to copy %s to %s\n", src, emu_dst);
        return ZX_ERR_IO;
    }

    return ZX_OK;
}

// Add contents of a manifest to a minfs filesystem
int do_add_manifest(fbl::unique_ptr<minfs::Bcache> bc, int argc, char** argv) {
    if (argc != 1) {
        fprintf(stderr, "add requires one argument\n");
        return -1;
    }

    if (io_setup(fbl::move(bc))) {
        return -1;
    }

    fbl::unique_fd fd(open(argv[0], O_RDONLY, 0));
    if (!fd) {
        fprintf(stderr, "error: Could not open %s\n", argv[0]);
        return ZX_ERR_IO;
    }

    FILE* manifest = fdopen(fd.release(), "r");

    while (true) {
        zx_status_t status = process_manifest_line(manifest);
        if (status == ZX_ERR_OUT_OF_RANGE) {
            fclose(manifest);
            return 0;
        } else if (status != ZX_OK) {
            fclose(manifest);
            return -1;
        }
    }
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
#ifndef __Fuchsia__
    {"cp", do_cp, O_RDWR, "copy to/from fs. Prefix fs paths with '::'"},
    {"mkdir", do_mkdir, O_RDWR, "create directory. Prefix paths with '::'"},
    {"ls", do_ls, O_RDWR, "list content of directory. Prefix paths with '::'"},
    {"manifest", do_add_manifest, O_RDWR, "Add files to fs as specified in manifest. The format "
                                          "of the manifest must be as follows:\n"
                                          "\t\t\t'dst/path=src/path', "
                                          "with one dst/src pair on each line."},
#endif
};

int usage() {
    fprintf(stderr,
            "usage: minfs [ <option>* ] <file-or-device>[@<size>] <command> [ <arg>* ]\n"
            "\n"
            "options:  -v          some debug messages\n"
            "          -vv         all debug messages\n"
            "          --readonly  Mount filesystem read-only\n"
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
#ifdef __Fuchsia__
    fprintf(stderr, "%9s %-10s %s\n", "", "mount", "mount filesystem");
#endif
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

} // namespace

int main(int argc, char** argv) {
    off_t size = 0;
    bool readonly = false;

    // handle options
    while (argc > 1) {
        if (!strcmp(argv[1], "-v")) {
            fs_trace_on(FS_TRACE_SOME);
        } else if (!strcmp(argv[1], "-vv")) {
            fs_trace_on(FS_TRACE_ALL);
        } else if (!strcmp(argv[1], "--readonly")) {
            readonly = true;
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

    fbl::unique_fd fd;
#ifdef __Fuchsia__
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
    fd.reset(open(fn, readonly ? O_RDONLY : flags, 0644));
    if (!fd) {
        fprintf(stderr, "error: cannot open '%s'\n", fn);
        return -1;
    }
#endif
    if (size == 0) {
        size = get_size(fd.get());
        if (size == 0) {
            fprintf(stderr, "minfs: failed to access block device\n");
            return usage();
        }
    }
    size /= minfs::kMinfsBlockSize;

    fbl::unique_ptr<minfs::Bcache> bc;
    if (minfs::Bcache::Create(&bc, fbl::move(fd), (uint32_t)size) < 0) {
        fprintf(stderr, "error: cannot create block cache\n");
        return -1;
    }

#ifdef __Fuchsia__
    if (!strcmp(cmd, "mount")) {
        return do_minfs_mount(fbl::move(bc), readonly);
    }
#endif

    for (unsigned i = 0; i < fbl::count_of(CMDS); i++) {
        if (!strcmp(cmd, CMDS[i].name)) {
            return CMDS[i].func(fbl::move(bc), argc - 3, argv + 3);
        }
    }
    return -1;
}
