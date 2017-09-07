// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// for S_IF*
#define _XOPEN_SOURCE
#include "host.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <fs/vfs.h>

#include <mxio/vfs.h>
#include "minfs.h"
#include "minfs-private.h"

static mx_status_t do_stat(fbl::RefPtr<fs::Vnode> vn, struct stat* s) {
    vnattr_t a;
    mx_status_t status = vn->Getattr(&a);
    if (status == MX_OK) {
        memset(s, 0, sizeof(struct stat));
        s->st_mode = a.mode;
        s->st_size = a.size;
        s->st_ino = a.inode;
        s->st_ctime = a.create_time;
        s->st_mtime = a.modify_time;
    }
    return status;
}

typedef struct {
    fbl::RefPtr<fs::Vnode> vn;
    uint64_t off;
    vdircookie_t dircookie;
} file_t;

#define MAXFD 64

static file_t fdtab[MAXFD];

#define FD_MAGIC 0x45AB0000

static file_t* file_get(int fd) {
    if (((fd)&0xFFFF0000) != FD_MAGIC) {
        return nullptr;
    }
    fd &= 0x0000FFFF;
    if ((fd < 0) || (fd >= MAXFD)) {
        return nullptr;
    }
    if (fdtab[fd].vn == nullptr) {
        return nullptr;
    }
    return fdtab + fd;
}

int status_to_errno(mx_status_t status) {
    switch (status) {
    case MX_OK:
        return 0;
    default:
        return EIO;
    }
}

#define FAIL(err)              \
    do {                       \
        errno = (err);         \
        return errno ? -1 : 0; \
    } while (0)
#define STATUS(status) \
    FAIL(status_to_errno(status))
#define FILE_GET(f, fd)                   \
    do {                                  \
        if ((f = file_get(fd)) == nullptr) { \
            FAIL(EBADF);                  \
        }                                 \
    } while (0)
#define FILE_WRAP(f, fd, name, args...) \
    do {                                \
        if ((f = file_get(fd)) == nullptr) \
            return name(args);          \
    } while (0)
#define PATH_WRAP(path, name, args...) \
    do {                               \
        if (check_path(path))          \
            return name(args);         \
    } while (0)

fbl::RefPtr<fs::Vnode> fake_root;

static inline int check_path(const char* path) {
    if (strncmp(path, PATH_PREFIX, PREFIX_SIZE) || (fake_root == nullptr)) {
        return -1;
    }
    return 0;
}

int emu_open(const char* path, int flags, mode_t mode) {
    //TODO: fdtab lock
    PATH_WRAP(path, open, path, flags, mode);
    int fd;
    for (fd = 0; fd < MAXFD; fd++) {
        if (fdtab[fd].vn == nullptr) {
            const char* pathout = nullptr;
            fbl::RefPtr<fs::Vnode> vn_fs;
            mx_status_t status = minfs::vfs.Open(fake_root, &vn_fs, path + PREFIX_SIZE, &pathout, flags, mode);
            if (status < 0) {
                STATUS(status);
            }
            fdtab[fd].vn = fbl::RefPtr<fs::Vnode>::Downcast(vn_fs);
            return fd | FD_MAGIC;
        }
    }
    FAIL(EMFILE);
}

int emu_close(int fd) {
    //TODO: fdtab lock
    file_t* f;
    FILE_WRAP(f, fd, close, fd);
    f->vn->Close();
    memset(f, 0, sizeof(file_t));
    return 0;
}

int emu_mkdir(const char* path, mode_t mode) {
    PATH_WRAP(path, mkdir, path, mode);
    mode = S_IFDIR;
    int fd = emu_open(path, O_CREAT | O_EXCL, S_IFDIR | (mode & 0777));
    if (fd >= 0) {
        emu_close(fd);
        return 0;
    } else {
        return fd;
    }
}

ssize_t emu_read(int fd, void* buf, size_t count) {
    file_t* f;
    FILE_WRAP(f, fd, read, fd, buf, count);
    ssize_t r = f->vn->Read(buf, count, f->off);
    if (r > 0) {
        f->off += r;
    }
    return r;
}

ssize_t emu_write(int fd, const void* buf, size_t count) {
    file_t* f;
    FILE_WRAP(f, fd, write, fd, buf, count);
    ssize_t r = f->vn->Write(buf, count, f->off);
    if (r > 0) {
        f->off += r;
    }
    return r;
}

off_t emu_lseek(int fd, off_t offset, int whence) {
    file_t* f;
    FILE_WRAP(f, fd, lseek, fd, offset, whence);

    uint64_t old = f->off;
    uint64_t n;
    vnattr_t a;

    switch (whence) {
    case SEEK_SET:
        if (offset < 0) {
            FAIL(EINVAL);
        }
        f->off = offset;
        break;
    case SEEK_END:
        if (f->vn->Getattr(&a)) {
            FAIL(EINVAL);
        }
        old = a.size;
    // fall through
    case SEEK_CUR:
        n = old + offset;
        if (offset < 0) {
            if (n >= old) {
                FAIL(EINVAL);
            }
        } else {
            if (n < old) {
                FAIL(EINVAL);
            }
        }
        f->off = n;
        break;
    default:
        FAIL(EINVAL);
    }
    return f->off;
}

int emu_fstat(int fd, struct stat* s) {
    file_t* f;
    FILE_WRAP(f, fd, fstat, fd, s);
    STATUS(do_stat(f->vn, s));
}

int emu_unlink(const char* path) {
    PATH_WRAP(path, unlink, path);
    fbl::RefPtr<fs::Vnode> vn;
    mx_status_t status = minfs::vfs.Walk(fake_root, &vn, path + PREFIX_SIZE, &path);
    if (status == MX_OK) {
        status = vn->Unlink(path, strlen(path), false);
        vn->Close();
    }
    STATUS(status);
}

int emu_rename(const char* oldpath, const char* newpath) {
    STATUS(MX_ERR_NOT_SUPPORTED);
}

int emu_stat(const char* fn, struct stat* s) {
    PATH_WRAP(fn, stat, fn, s);
    fbl::RefPtr<fs::Vnode> vn = fake_root;
    fbl::RefPtr<fs::Vnode> cur = fake_root;
    mx_status_t status;
    const char* nextpath = nullptr;
    size_t len;

    fn += PREFIX_SIZE;
    do {
        while (fn[0] == '/') {
            fn++;
        }
        if (fn[0] == 0) {
            fn = ".";
        }
        len = strlen(fn);
        nextpath = strchr(fn, '/');
        if (nextpath != nullptr) {
            len = nextpath - fn;
            nextpath++;
        }
        fbl::RefPtr<fs::Vnode> vn_fs;
        status = cur->Lookup(&vn_fs, fn, len);
        if (status != MX_OK) {
            return -ENOENT;
        }
        vn = fbl::RefPtr<fs::Vnode>::Downcast(vn_fs);
        if (cur != fake_root) {
            cur->Close();
        }
        cur = vn;
        fn = nextpath;
    } while (nextpath != nullptr);

    status = do_stat(vn, s);
    if (vn != fake_root) {
        vn->Close();
    }
    STATUS(status);
}

#define DIR_BUFSIZE 2048

typedef struct MINDIR {
    uint64_t magic;
    fbl::RefPtr<fs::Vnode> vn;
    vdircookie_t cookie;
    uint8_t* ptr;
    uint8_t data[DIR_BUFSIZE];
    size_t size;
    struct dirent de;
} MINDIR;

DIR* emu_opendir(const char* name) {
    PATH_WRAP(name, opendir, name);
    fbl::RefPtr<fs::Vnode> vn;
    mx_status_t status = minfs::vfs.Open(fake_root, &vn, name + PREFIX_SIZE, &name, O_RDONLY, 0);
    if (status != MX_OK) {
        return nullptr;
    }
    MINDIR* dir = (MINDIR*)calloc(1, sizeof(MINDIR));
    dir->magic = minfs::kMinfsMagic0;
    dir->vn = fbl::RefPtr<fs::Vnode>::Downcast(vn);
    return (DIR*) dir;
}

struct dirent* emu_readdir(DIR* dirp) {
    MINDIR* dir = (MINDIR*)dirp;
    for (;;) {
        if (dir->size >= sizeof(vdirent_t)) {
            vdirent_t* vde = (vdirent_t*)dir->ptr;
            if (dir->size >= vde->size) {
                struct dirent* ent = &dir->de;
                strcpy(ent->d_name, vde->name);
                dir->ptr += vde->size;
                dir->size -= vde->size;
                return ent;
            }
            dir->size = 0;
        }
        mx_status_t status = dir->vn->Readdir(&dir->cookie, &dir->data, DIR_BUFSIZE);
        if (status <= 0) {
            break;
        }
        dir->ptr = dir->data;
        dir->size = status;
    }
    return nullptr;
}

int emu_closedir(DIR* dirp) {
    if (((uint64_t*)dirp)[0] != minfs::kMinfsMagic0) {
        return closedir(dirp);
    }

    MINDIR* dir = (MINDIR*)dirp;
    dir->vn->Close();
    free(dirp);

    return 0;
}
