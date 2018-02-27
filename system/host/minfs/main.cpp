// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// for S_IF*
#define _XOPEN_SOURCE

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
#include <minfs/fsck.h>
#include <minfs/host.h>
#include <minfs/minfs.h>
#include <zircon/compiler.h>
#include <zircon/process.h>
#include <zircon/processargs.h>

namespace {

int do_minfs_check(fbl::unique_ptr<minfs::Bcache> bc, int argc, char** argv) {
    return minfs_check(fbl::move(bc));
}

int io_setup(fbl::unique_ptr<minfs::Bcache> bc) {
    return emu_mount_bcache(fbl::move(bc));
}

int is_dir(const char* path, bool* result) {
    struct stat s;
    int r = host_path(path) ? stat(path, &s) : emu_stat(path, &s);
    if (S_ISDIR(s.st_mode)) {
        *result = true;
    } else {
        *result = false;
    }
    return r;
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

// Recursive helper function for cp_dir.
int cp_dir_(char* src, char* dst) {
    if (DirWrapper::Make(dst, 0777) && errno != EEXIST) {
        fprintf(stderr, "minfs: could not create directory\n");
        return -1;
    }
    DirWrapper current_dir;
    if (DirWrapper::Open(src, &current_dir)) {
        return -1;
    }

    size_t src_len = strlen(src);
    size_t dst_len = strlen(dst);
    struct dirent* de;
    while ((de = current_dir.ReadDir()) != nullptr) {
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) {
            continue;
        }
        size_t name_len = strlen(de->d_name);
        if (src_len + name_len + 1 > PATH_MAX - 1 ||
            dst_len + name_len + 1 > PATH_MAX - 1) {
            return -1;
        }
        strncat(src, "/", 1);
        strncat(src, de->d_name, PATH_MAX - src_len - 2);
        strncat(dst, "/", 1);
        strncat(dst, de->d_name, PATH_MAX - dst_len - 2);

        bool dir;
        if (is_dir(src, &dir)) {
            return -1;
        }
        if (dir) {
            int err = cp_dir_(src, dst);
            if (err) {
                return err;
            }
        } else if (cp_file(src, dst)) {
            return -1;
        }
        src[src_len] = '\0';
        dst[dst_len] = '\0';
    }
    return 0;
}

// Copies a directory recursively.
int cp_dir(const char* src_path, const char* dst_path) {
    char src[PATH_MAX];
    char dst[PATH_MAX];

    if (strlen(src_path) >= PATH_MAX || strlen(dst_path) >= PATH_MAX) {
        return -1;
    }
    strncpy(src, src_path, PATH_MAX);
    strncpy(dst, dst_path, PATH_MAX);
    return cp_dir_(src, dst);
}

int do_cp(fbl::unique_ptr<minfs::Bcache> bc, int argc, char** argv) {
    if (argc != 2) {
        fprintf(stderr, "cp requires two arguments\n");
        return -1;
    }

    if (io_setup(fbl::move(bc))) {
        return -1;
    }

    bool dir;
    if (is_dir(argv[0], &dir)) {
        fprintf(stderr, "minfs: failed to stat %s\n", argv[0]);
        return -1;
    }
    if (dir) {
        return cp_dir(argv[0], argv[1]);
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
zx_status_t process_manifest_line(FILE* manifest, const char* dir_path) {
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
    src[0] = '\0';
    char dst[PATH_MAX];
    strncpy(dst, line, PATH_MAX);

    if (eq_ptr[1] != '/') {
        strncpy(src, dir_path, PATH_MAX);
        strncat(src, "/", PATH_MAX - strlen(src));
    }

    strncat(src, eq_ptr + 1, PATH_MAX - strlen(src));

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

    char dir_path[PATH_MAX];
    strncpy(dir_path, dirname(argv[0]), PATH_MAX);
    FILE* manifest = fdopen(fd.release(), "r");

    while (true) {
        zx_status_t status = process_manifest_line(manifest, dir_path);
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
    {"cp", do_cp, O_RDWR, "copy to/from fs. Prefix fs paths with '::'"},
    {"mkdir", do_mkdir, O_RDWR, "create directory. Prefix paths with '::'"},
    {"ls", do_ls, O_RDWR, "list content of directory. Prefix paths with '::'"},
    {"manifest", do_add_manifest, O_RDWR, "Add files to fs as specified in manifest. The format "
                                          "of the manifest must be as follows:\n"
                                          "\t\t\t'dst/path=src/path', "
                                          "with one dst/src pair on each line."},
};

int usage() {
    fprintf(stderr,
            "usage: minfs [ <option>* ] <file-or-device>[@<size>] <command> [ <arg>* ]\n"
            "\n"
            "options:  -r|--readonly       Mount filesystem read-only\n"
            "          -o|--offset [bytes] Byte offset at which minfs partition starts\n"
            "                              Default = 0\n"
            "          -l|--length [bytes] Length in bytes of minfs partition\n"
            "                              Default = Remaining Length\n"
            "          -h|--help           Display this message\n"
            "\n");
    for (unsigned n = 0; n < fbl::count_of(CMDS); n++) {
        fprintf(stderr, "%9s %-10s %s\n", n ? "" : "commands:",
                CMDS[n].name, CMDS[n].help);
    }
    fprintf(stderr, "\n");
    return -1;
}

off_t get_size(int fd) {
    struct stat s;
    if (fstat(fd, &s) < 0) {
        fprintf(stderr, "error: minfs could not find end of file/device\n");
        return 0;
    }
    return s.st_size;
}

} // namespace

int main(int argc, char** argv) {
    off_t size = 0;
    bool readonly = false;
    off_t offset = 0;
    off_t length = 0;

    while (1) {
        static struct option opts[] = {
            {"readonly", no_argument, nullptr, 'r'},
            {"offset", required_argument, nullptr, 'o'},
            {"length", required_argument, nullptr, 'l'},
            {"help", no_argument, nullptr, 'h'},
            {nullptr, 0, nullptr, 0},
        };
        int opt_index;
        int c = getopt_long(argc, argv, "ro:l:vh", opts, &opt_index);
        if (c < 0) {
            break;
        }
        switch (c) {
        case 'r':
            readonly = true;
            break;
        case 'o':
            offset = atoi(optarg);
            break;
        case 'l':
            length = atoi(optarg);
            break;
        case 'h':
        default:
            return usage();
        }
    }

    argc -= optind;
    argv += optind;

    // Block device passed by path
    if (argc < 2) {
        return usage();
    }
    char* fn = argv[0];
    char* cmd = argv[1];
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

        struct stat s;
        if (stat(fn, &s) == 0) {
            if (S_ISBLK(s.st_mode)) {
                fprintf(stderr, "minfs: @size argument is not supported for block device targets\n");
                return -1;
            }
        } else {
            int fd = open(fn, O_CREAT, 0666);
            if (!fd) {
                fprintf(stderr, "minfs: failed to create %s: %s", fn, strerror(errno));
                return -1;
            }
            close(fd);
        }

        if (truncate(fn, size) != 0) {
            fprintf(stderr, "minfs: failed to truncate %s: %s\n", fn, strerror(errno));
            return -1;
        }

    }

    fbl::unique_fd fd;
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

    bc->SetOffset(offset);

    for (unsigned i = 0; i < fbl::count_of(CMDS); i++) {
        if (!strcmp(cmd, CMDS[i].name)) {
            return CMDS[i].func(fbl::move(bc), argc - 2, argv + 2);
        }
    }
    return -1;
}
