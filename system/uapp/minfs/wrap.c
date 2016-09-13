// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// for S_IF*
#define _XOPEN_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/stat.h>

#include <mxio/vfs.h>
#include "vfs.h"

struct vnode {
    VNODE_BASE_FIELDS
};

static mx_status_t do_stat(vnode_t* vn, struct stat* s) {
    vnattr_t a;
    mx_status_t status = vn->ops->getattr(vn, &a);
    if (status == NO_ERROR) {
        memset(s, 0, sizeof(struct stat));
        s->st_mode = a.mode;
        s->st_size = a.size;
        s->st_ino = a.inode;
    }
    return status;
}

#define FN(n) __wrap_##n
#define FL(n) __real_##n

typedef struct {
    vnode_t* vn;
    uint64_t off;
    vdircookie_t dircookie;
} file_t;

#define MAXFD 64

static file_t fdtab[MAXFD];

#define FD_MAGIC 0x45AB0000

static file_t* file_get(int fd) {
    if (((fd) & 0xFFFF0000) != FD_MAGIC) {
        return NULL;
    }
    fd &= 0x0000FFFF;
    if ((fd < 0) || (fd >= MAXFD)) {
        return NULL;
    }
    if (fdtab[fd].vn == NULL) {
        return NULL;
    }
    return fdtab + fd;
}

int status_to_errno(mx_status_t status) {
    switch (status) {
    case NO_ERROR:
        return 0;
    default:
        return EIO;
    }
}

#define FAIL(err) \
    do { errno = (err); return errno ? -1 : 0; } while (0)
#define STATUS(status) \
    FAIL(status_to_errno(status))
#define FILE_GET(f, fd) \
    do { if ((f = file_get(fd)) == NULL) FAIL(EBADF); } while (0)
#define FILE_WRAP(f, fd, name, args...) \
    do { if ((f = file_get(fd)) == NULL) return __real_##name(args); } while (0)
#define PATH_WRAP(path, name, args...) \
    do { if (check_path(path)) return __real_##name(args); } while (0)

vnode_t* fake_root;

#define PATH_PREFIX "::"
#define PREFIX_SIZE 2

static inline int check_path(const char* path) {
    if (strncmp(path, PATH_PREFIX, PREFIX_SIZE) || (fake_root == NULL)) {
        return -1;
    }
    return 0;
}

int FL(open)(const char* path, int flags, mode_t mode);
int FN(open)(const char* path, int flags, mode_t mode) {
    //TODO: fdtab lock
    PATH_WRAP(path, open, path, flags, mode);
    int fd;
    for (fd = 0; fd < MAXFD; fd++) {
        if (fdtab[fd].vn == NULL) {
            mx_status_t status = vfs_open(fake_root, &fdtab[fd].vn, path + PREFIX_SIZE, flags, mode);
            if (status < 0) {
                STATUS(status);
            }
            return fd | FD_MAGIC;
        }
    }
    FAIL(EMFILE);
}

int FL(close)(int fd);
int FN(close)(int fd) {
    //TODO: fdtab lock
    file_t* f;
    FILE_WRAP(f, fd, close, fd);
    vfs_close(f->vn);
    memset(f, 0, sizeof(file_t));
    return 0;
}

int FL(mkdir)(const char* path, mode_t mode);
int FN(mkdir)(const char* path, mode_t mode) {
    PATH_WRAP(path, mkdir, path, mode);
    mode = S_IFDIR;
    int fd = FN(open)(path, O_CREAT | O_EXCL, S_IFDIR | (mode & 0777));
    if (fd >= 0) {
        FN(close)(fd);
        return 0;
    } else {
        return fd;
    }
}

ssize_t FL(read)(int fd, void* buf, size_t count);
ssize_t FN(read)(int fd, void* buf, size_t count) {
    file_t* f;
    FILE_WRAP(f, fd, read, fd, buf, count);
    ssize_t r = f->vn->ops->read(f->vn, buf, count, f->off);
    if (r > 0) {
        f->off += r;
    }
    return r;
}

ssize_t FL(write)(int fd, const void* buf, size_t count);
ssize_t FN(write)(int fd, const void* buf, size_t count) {
    file_t* f;
    FILE_WRAP(f, fd, write, fd, buf, count);
    ssize_t r = f->vn->ops->write(f->vn, buf, count, f->off);
    if (r > 0) {
        f->off += r;
    }
    return r;
}

off_t FL(lseek)(int fd, off_t offset, int whence);
off_t FN(lseek)(int fd, off_t offset, int whence) {
    file_t* f;
    FILE_WRAP(f, fd, lseek, fd, offset, whence);

    uint64_t old = f->off;
    uint64_t n;
    vnattr_t a;

    switch (whence) {
    case SEEK_SET:
        if (offset < 0) FAIL(EINVAL);
        f->off = offset;
        break;
    case SEEK_END:
        if (f->vn->ops->getattr(f->vn, &a)) FAIL(EINVAL);
        old = a.size;
        // fall through
    case SEEK_CUR:
        n = old + offset;
        if (offset < 0) {
            if (n >= old) FAIL(EINVAL);
        } else {
            if (n < old) FAIL(EINVAL);
        }
        f->off = n;
        break;
    default:
        FAIL(EINVAL);
    }
    return f->off;
}

int FL(fstat)(int fd, struct stat* s);
int FN(fstat)(int fd, struct stat* s) {
    file_t* f;
    FILE_WRAP(f, fd, fstat, fd, s);
    STATUS(do_stat(f->vn, s));
}

int FL(unlink)(const char* path);
int FN(unlink)(const char* path) {
    PATH_WRAP(path, unlink, path);
    vnode_t* vn;
    mx_status_t status = vfs_walk(fake_root, &vn, path + PREFIX_SIZE, &path);
    if (status == NO_ERROR) {
        status = vn->ops->unlink(vn, path, strlen(path));
        vfs_close(vn);
    }
    STATUS(status);
}

int FL(rename)(const char* oldpath, const char* newpath);
int FN(rename)(const char* oldpath, const char* newpath) {
    PATH_WRAP(oldpath, rename, oldpath, newpath);
    mx_status_t status = vfs_rename(fake_root, oldpath + PREFIX_SIZE, newpath + PREFIX_SIZE);
    STATUS(status);
}

int FL(stat)(const char* fn, struct stat* s);
int FN(stat)(const char* fn, struct stat* s) {
    PATH_WRAP(fn, stat, fn, s);
    vnode_t* vn;
    mx_status_t status = vfs_walk(fake_root, &vn, fn + PREFIX_SIZE, &fn);
    if (status == NO_ERROR) {
        status = do_stat(vn, s);
        vfs_close(vn);
    }
    STATUS(status);
}

static int __getdirents(int fd, void* dirents, size_t len) {
    file_t* f;
    FILE_GET(f, fd);
    STATUS(f->vn->ops->readdir(f->vn, &f->dircookie, dirents, len));
}
