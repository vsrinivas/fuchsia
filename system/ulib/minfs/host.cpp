// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// for S_IF*
#define _XOPEN_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <fbl/limits.h>
#include <fbl/ref_ptr.h>
#include <fdio/vfs.h>
#include <fs/vfs.h>
#include <minfs/format.h>
#include <minfs/host.h>
#include <minfs/minfs.h>
#include <zircon/assert.h>

#include "minfs-private.h"

namespace {

zx_status_t do_stat(fbl::RefPtr<fs::Vnode> vn, struct stat* s) {
    vnattr_t a;
    zx_status_t status = vn->Getattr(&a);
    if (status == ZX_OK) {
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
    fs::vdircookie_t dircookie;
} file_t;

#define MAXFD 64

static file_t fdtab[MAXFD];

#define FD_MAGIC 0x45AB0000

file_t* file_get(int fd) {
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

int status_to_errno(zx_status_t status) {
    switch (status) {
    case ZX_OK:
        return 0;
    case ZX_ERR_FILE_BIG:
        return EFBIG;
    case ZX_ERR_NO_SPACE:
        return ENOSPC;
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

fbl::RefPtr<minfs::VnodeMinfs> fake_root = nullptr;
fs::Vfs fake_vfs;

} // namespace anonymous

int emu_mkfs(const char* path) {
    fbl::unique_fd fd(open(path, O_RDWR));
    if (!fd) {
        fprintf(stderr, "error: could not open path %s\n", path);
        return -1;
    }

    struct stat s;
    if (fstat(fd.get(), &s) < 0) {
        fprintf(stderr, "error: minfs could not find end of file/device\n");
        return -1;
    }

    off_t size = s.st_size / minfs::kMinfsBlockSize;

    fbl::unique_ptr<minfs::Bcache> bc;
    if (minfs::Bcache::Create(&bc, fbl::move(fd), (uint32_t) size) < 0) {
        fprintf(stderr, "error: cannot create block cache\n");
        return -1;
    }

    return Mkfs(fbl::move(bc));
}

int emu_mount(const char* path) {
    fbl::unique_fd fd(open(path, O_RDWR));
    if (!fd) {
        fprintf(stderr, "error: could not open path %s\n", path);
        return -1;
    }

    struct stat s;
    if (fstat(fd.get(), &s) < 0) {
        fprintf(stderr, "error: minfs could not find end of file/device\n");
        return 0;
    }

    off_t size = s.st_size / minfs::kMinfsBlockSize;

    fbl::unique_ptr<minfs::Bcache> bc;
    if (minfs::Bcache::Create(&bc, fbl::move(fd), (uint32_t) size) < 0) {
        fprintf(stderr, "error: cannot create block cache\n");
        return -1;
    }

    return minfs::minfs_mount(fbl::move(bc), &fake_root);
}

int emu_mount_bcache(fbl::unique_ptr<minfs::Bcache> bc) {
    return minfs::minfs_mount(fbl::move(bc), &fake_root) == ZX_OK ? 0 : -1;
}

// Since this is a host-side tool, the client may be bringing
// their own C library, and we do not have the guarantee that
// our ZX_FS flags align with the O_* flags.
uint32_t fdio_flags_to_zxio(uint32_t flags) {
    uint32_t result = 0;
    switch (flags & O_ACCMODE) {
    case O_RDONLY:
        result |= ZX_FS_RIGHT_READABLE;
        break;
    case O_WRONLY:
        result |= ZX_FS_RIGHT_WRITABLE;
        break;
    case O_RDWR:
        result |= ZX_FS_RIGHT_READABLE | ZX_FS_RIGHT_WRITABLE;
        break;
    }
#ifdef O_PATH
    if (flags & O_PATH) {
        result |= ZX_FS_FLAG_VNODE_REF_ONLY;
    }
#endif
#ifdef O_DIRECTORY
    if (flags & O_DIRECTORY) {
        result |= ZX_FS_FLAG_DIRECTORY;
    }
#endif
    if (flags & O_CREAT) {
        result |= ZX_FS_FLAG_CREATE;
    }
    if (flags & O_EXCL) {
        result |= ZX_FS_FLAG_EXCLUSIVE;
    }
    if (flags & O_TRUNC) {
        result |= ZX_FS_FLAG_TRUNCATE;
    }
    if (flags & O_APPEND) {
        result |= ZX_FS_FLAG_APPEND;
    }

    return result;
}

int emu_open(const char* path, int flags, mode_t mode) {
    //TODO: fdtab lock
    ZX_DEBUG_ASSERT_MSG(!host_path(path), "'emu_' functions can only operate on target paths");
    int fd;
    if (flags & O_APPEND) {
        errno = ENOTSUP;
        return -1;
    }
    for (fd = 0; fd < MAXFD; fd++) {
        if (fdtab[fd].vn == nullptr) {
            fbl::RefPtr<fs::Vnode> vn_fs;
            fbl::StringPiece str(path + PREFIX_SIZE);
            flags = fdio_flags_to_zxio(flags);
            zx_status_t status = fake_vfs.Open(fake_root, &vn_fs, str, &str, flags, mode);
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
    file_t* f = file_get(fd);
    if (f == nullptr) {
        return -1;
    }
    f->vn->Close();
    f->vn.reset();
    f->off = 0;
    f->dircookie.Reset();
    return 0;
}

ssize_t emu_write(int fd, const void* buf, size_t count) {
    file_t* f = file_get(fd);
    if (f == nullptr) {
        return -1;
    }
    size_t actual;
    zx_status_t status = f->vn->Write(buf, count, f->off, &actual);
    if (status == ZX_OK) {
        f->off += actual;
        ZX_DEBUG_ASSERT(actual <= fbl::numeric_limits<ssize_t>::max());
        return static_cast<ssize_t>(actual);
    }

    ZX_DEBUG_ASSERT(status < 0);
    STATUS(status);
}

ssize_t emu_pwrite(int fd, const void* buf, size_t count, off_t off) {
    file_t* f = file_get(fd);
    if (f == nullptr) {
        return -1;
    }
    size_t actual;
    zx_status_t status = f->vn->Write(buf, count, off, &actual);
    if (status == ZX_OK) {
        ZX_DEBUG_ASSERT(actual <= fbl::numeric_limits<ssize_t>::max());
        return static_cast<ssize_t>(actual);
    }

    ZX_DEBUG_ASSERT(status < 0);
    STATUS(status);
}

ssize_t emu_read(int fd, void* buf, size_t count) {
    file_t* f = file_get(fd);
    if (f == nullptr) {
        return -1;
    }
    size_t actual;
    zx_status_t status = f->vn->Read(buf, count, f->off, &actual);
    if (status == ZX_OK) {
        f->off += actual;
        ZX_DEBUG_ASSERT(actual <= fbl::numeric_limits<ssize_t>::max());
        return static_cast<ssize_t>(actual);
    }
    ZX_DEBUG_ASSERT(status < 0);
    STATUS(status);
}

ssize_t emu_pread(int fd, void* buf, size_t count, off_t off) {
    file_t* f = file_get(fd);
    if (f == nullptr) {
        return -1;
    }
    size_t actual;
    zx_status_t status = f->vn->Read(buf, count, off, &actual);
    if (status == ZX_OK) {
        ZX_DEBUG_ASSERT(actual <= fbl::numeric_limits<ssize_t>::max());
        return static_cast<ssize_t>(actual);
    }
    ZX_DEBUG_ASSERT(status < 0);
    STATUS(status);
}

int emu_ftruncate(int fd, off_t len) {
    file_t* f = file_get(fd);
    if (f == nullptr) {
        return -1;
    }
    int r = f->vn->Truncate(len);
    return r < 0 ? -1 : r;
}

off_t emu_lseek(int fd, off_t offset, int whence) {
    file_t* f = file_get(fd);
    if (f == nullptr) {
        return -1;
    }

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
    file_t* f = file_get(fd);
    if (f == nullptr) {
        return -1;
    }
    STATUS(do_stat(f->vn, s));
}

int emu_stat(const char* fn, struct stat* s) {
    ZX_DEBUG_ASSERT_MSG(!host_path(fn), "'emu_' functions can only operate on target paths");
    fbl::RefPtr<fs::Vnode> vn = fake_root;
    fbl::RefPtr<fs::Vnode> cur = fake_root;
    zx_status_t status;
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
        status = cur->Lookup(&vn_fs, fbl::StringPiece(fn, len));
        if (status != ZX_OK) {
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
    fs::vdircookie_t cookie;
    uint8_t* ptr;
    uint8_t data[DIR_BUFSIZE];
    size_t size;
    struct dirent de;
} MINDIR;

int emu_mkdir(const char* path, mode_t mode) {
    ZX_DEBUG_ASSERT_MSG(!host_path(path), "'emu_' functions can only operate on target paths");
    mode = S_IFDIR;
    int fd = emu_open(path, O_CREAT | O_EXCL, S_IFDIR | (mode & 0777));
    if (fd >= 0) {
        emu_close(fd);
        return 0;
    } else {
        return fd;
    }
}

DIR* emu_opendir(const char* name) {
    ZX_DEBUG_ASSERT_MSG(!host_path(name), "'emu_' functions can only operate on target paths");
    fbl::RefPtr<fs::Vnode> vn;
    fbl::StringPiece path(name + PREFIX_SIZE);
    zx_status_t status = fake_vfs.Open(fake_root, &vn, path, &path, O_RDONLY, 0);
    if (status != ZX_OK) {
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
                ent->d_type = vde->type;
                dir->ptr += vde->size;
                dir->size -= vde->size;
                return ent;
            }
            dir->size = 0;
        }
        size_t actual;
        zx_status_t status = dir->vn->Readdir(&dir->cookie, &dir->data, DIR_BUFSIZE, &actual);
        if (status != ZX_OK || actual == 0) {
            break;
        }
        dir->ptr = dir->data;
        dir->size = actual;
    }
    return nullptr;
}

void emu_rewinddir(DIR* dirp) {
    MINDIR* dir = (MINDIR*)dirp;
    dir->size = 0;
    dir->ptr = NULL;
    dir->cookie.n = 0;
}

int emu_closedir(DIR* dirp) {
    if (((uint64_t*)dirp)[0] != minfs::kMinfsMagic0) {
        return closedir(dirp);
    }

    MINDIR* dir = (MINDIR*)dirp;
    dir->vn->Close();
    dir->vn.reset();
    free(dirp);

    return 0;
}
