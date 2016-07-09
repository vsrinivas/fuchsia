// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <unistd.h>

#include <magenta/processargs.h>
#include <magenta/syscalls.h>

#include <runtime/mutex.h>

#include <mxio/debug.h>
#include <mxio/io.h>
#include <mxio/remoteio.h>
#include <mxio/util.h>
#include <mxio/vfs.h>

#include "private.h"

#define MXDEBUG 0

// non-thread-safe emulation of unistd io functions
// using the mxio transports

static mxr_mutex_t mxio_lock = MXR_MUTEX_INIT;

static mxio_t* mxio_root_handle = NULL;

static mxio_t* mxio_cwd_handle = NULL;

static mxio_t* mxio_fdtab[MAX_MXIO_FD] = {
    NULL,
};

static bool mxio_root_init = true;

void mxio_install_root(mxio_t* root) {
    mxr_mutex_lock(&mxio_lock);
    if (mxio_root_init) {
        mxio_root_handle = root;
        mxio_root_init = false;
    }
    mxr_mutex_unlock(&mxio_lock);
}

// Attaches an mxio to an fdtab slot.
// The mxio must have been upref'd on behalf of the
// fdtab prior to binding.
int mxio_bind_to_fd(mxio_t* io, int fd) {
    mxio_t* io_to_close = NULL;

    mxr_mutex_lock(&mxio_lock);
    if (fd >= 0) {
        if (fd >= MAX_MXIO_FD) {
            errno = EINVAL;
            fd = -1;
            goto fail;
        } else if (mxio_fdtab[fd]) {
            io_to_close = mxio_fdtab[fd];
        }
    } else {
        //TODO: bitmap, ffs, etc
        for (fd = 0; fd < MAX_MXIO_FD; fd++) {
            if (mxio_fdtab[fd] == NULL) {
                goto ok;
            }
        }
        errno = EMFILE;
        fd = -1;
        goto fail;
    }

ok:
    if (io_to_close) {
        io_to_close->dupcount--;
        if (io_to_close->dupcount > 0) {
            // still alive in another fdtab slot
            mxio_release(io_to_close);
            io_to_close = NULL;
        }
    }
    io->dupcount++;
    mxio_fdtab[fd] = io;
fail:
    mxr_mutex_unlock(&mxio_lock);
    if (io_to_close) {
        io_to_close->ops->close(io_to_close);
        mxio_release(io_to_close);
    }
    return fd;
}

static inline mxio_t* fd_to_io(int fd) {
    mxio_t* io = NULL;
    mxr_mutex_lock(&mxio_lock);
    if ((fd < 0) || (fd >= MAX_MXIO_FD)) {
        io = NULL;
    } else {
        if ((io = mxio_fdtab[fd]) != NULL) {
            mxio_acquire(io);
        }
    }
    mxr_mutex_unlock(&mxio_lock);
    return io;
}

static void mxio_exit(void) {
    mxr_mutex_lock(&mxio_lock);
    for (int fd = 0; fd < MAX_MXIO_FD; fd++) {
        mxio_t* io = mxio_fdtab[fd];
        if (io) {
            mxio_fdtab[fd] = NULL;
            io->dupcount--;
            if (io->dupcount == 0) {
                io->ops->close(io);
            }
            mxio_release(io);
        }
    }
    mxr_mutex_unlock(&mxio_lock);
}

mx_status_t mxio_close(mxio_t* io) {
    if (io->dupcount > 0) {
        printf("mxio_close(%p): dupcount nonzero!\n", io);
    }
    return io->ops->close(io);
}

static mx_status_t __mxio_open(mxio_t** io, const char* path, int flags) {
    mxio_t* iodir;
    if (path == NULL) {
        return ERR_INVALID_ARGS;
    }
    if (path[0] == 0) {
        return ERR_INVALID_ARGS;
    }

    mxr_mutex_lock(&mxio_lock);
    if (path[0] == '/') {
        iodir = mxio_root_handle;
        path++;
        if (path[0] == 0) {
            path = ".";
        }
    } else {
        iodir = mxio_cwd_handle;
    }
    mxio_acquire(iodir);
    mxr_mutex_unlock(&mxio_lock);

    mx_status_t r = iodir->ops->open(iodir, path, flags, io);
    mxio_release(iodir);
    return r;
}

// opens the directory containing path
// returns the non-directory portion of the path as name on success
static mx_status_t __mxio_opendir_containing(mxio_t** io, const char* path, const char** _name) {
    char dirpath[1024];
    mx_status_t r;

    if (path == NULL) {
        return ERR_INVALID_ARGS;
    }

    mxio_t* iodir;
    mxr_mutex_lock(&mxio_lock);
    if (path[0] == '/') {
        path++;
        iodir = mxio_root_handle;
    } else {
        iodir = mxio_cwd_handle;
    }
    mxio_acquire(iodir);
    mxr_mutex_unlock(&mxio_lock);

    const char* name = strrchr(path, '/');
    if (name == NULL) {
        name = path;
        dirpath[0] = '.';
        dirpath[1] = 0;
    } else {
        if ((name - path) > (ptrdiff_t)(sizeof(dirpath) - 1)) {
            r = ERR_INVALID_ARGS;
            goto fail;
        }
        memcpy(dirpath, path, name - path);
        dirpath[name - path] = 0;
        name++;
    }
    if (name[0] == 0) {
        r = ERR_INVALID_ARGS;
        goto fail;
    }

    *_name = name;
    r = iodir->ops->open(iodir, dirpath, O_DIRECTORY, io);

fail:
    mxio_release(iodir);
    return r;
}

// hook into libc process startup
// this is called prior to main to set up the mxio world
// and thus does not use the mxio_lock
void __libc_extensions_init(mx_proc_info_t* pi) {
    int n;

    // extract handles we care about
    for (n = 0; n < pi->handle_count; n++) {
        unsigned arg = MX_HND_INFO_ARG(pi->handle_info[n]);
        mx_handle_t h = pi->handle[n];

        switch (MX_HND_INFO_TYPE(pi->handle_info[n])) {
        case MX_HND_TYPE_MXIO_ROOT:
            mxio_root_handle = mxio_remote_create(h, 0);
            break;
        case MX_HND_TYPE_MXIO_REMOTE:
            // remote objects may have a second handle
            // which is for signalling events
            if (((n + 1) < pi->handle_count) &&
                (pi->handle_info[n] == pi->handle_info[n + 1])) {
                mxio_fdtab[arg] = mxio_remote_create(h, pi->handle[n + 1]);
                pi->handle_info[n + 1] = 0;
            } else {
                mxio_fdtab[arg] = mxio_remote_create(h, 0);
            }
            mxio_fdtab[arg]->dupcount++;
            break;
        case MX_HND_TYPE_MXIO_PIPE:
            mxio_fdtab[arg] = mxio_pipe_create(h);
            mxio_fdtab[arg]->dupcount++;
            break;
        default:
            // unknown handle, leave it alone
            continue;
        }
        pi->handle[n] = 0;
        pi->handle_info[n] = 0;
    }

    // install null stdin/out/err if not init'd
    for (n = 0; n < 3; n++) {
        if (mxio_fdtab[n] == NULL) {
            mxio_fdtab[n] = mxio_null_create();
            mxio_fdtab[n]->dupcount++;
        }
    }

    if (mxio_root_handle) {
        mxio_root_init = true;
        __mxio_open(&mxio_cwd_handle, "/", O_DIRECTORY);
    } else {
        // placeholder null handle
        mxio_root_handle = mxio_null_create();
    }
    if (mxio_cwd_handle == NULL) {
        mxio_cwd_handle = mxio_null_create();
    }

    atexit(mxio_exit);
}

mx_status_t mxio_clone_root(mx_handle_t* handles, uint32_t* types) {
    // The root handle is established in the init hook called from
    // libc startup (or, in the special case of devmgr, installed
    // slightly later), and is never NULL and does not change
    // in normal operation
    mx_status_t r = mxio_root_handle->ops->clone(mxio_root_handle, handles, types);
    if (r > 0) {
        *types = MX_HND_TYPE_MXIO_ROOT;
    }
    return r;
}

mx_status_t mxio_clone_fd(int fd, int newfd, mx_handle_t* handles, uint32_t* types) {
    mx_status_t r;
    mxio_t* io;
    if ((io = fd_to_io(fd)) == NULL) {
        return ERR_BAD_HANDLE;
    }
    if ((r = io->ops->clone(io, handles, types)) > 0) {
        for (int i = 0; i < r; i++) {
            types[i] |= (newfd << 16);
        }
    }
    mxio_release(io);
    return r;
}

ssize_t mxio_ioctl(int fd, int op, const void* in_buf, size_t in_len, void* out_buf, size_t out_len) {
    mxio_t* io;
    if ((io = fd_to_io(fd)) == NULL) {
        return ERR_BAD_HANDLE;
    }
    ssize_t r = io->ops->ioctl(io, op, in_buf, in_len, out_buf, out_len);
    mxio_release(io);
    return r;
}

mx_status_t mxio_wait_fd(int fd, uint32_t events, uint32_t* pending, mx_time_t timeout) {
    mxio_t* io;
    if ((io = fd_to_io(fd)) == NULL) {
        return ERR_BAD_HANDLE;
    }
    mx_status_t r = io->ops->wait(io, events, pending, timeout);
    mxio_release(io);
    return r;
}

int mxio_stat(mxio_t* io, struct stat* s) {
    vnattr_t attr;
    int r = io->ops->misc(io, MX_RIO_STAT, sizeof(attr), &attr, 0);
    if (r < 0) {
        return ERR_BAD_HANDLE;
    }
    if (r < (int)sizeof(attr)) {
        return ERR_IO;
    }
    memset(s, 0, sizeof(struct stat));
    s->st_mode = attr.mode;
    s->st_size = attr.size;
    s->st_ino = attr.inode;
    return 0;
}

// TODO: determine complete correct mapping
static int status_to_errno(mx_status_t status) {
    switch (status) {
    case ERR_NOT_FOUND: return ENOENT;
    case ERR_NO_MEMORY: return ENOMEM;
    case ERR_NOT_VALID: return EINVAL;
    case ERR_INVALID_ARGS: return EINVAL;
    case ERR_NOT_ENOUGH_BUFFER: return EINVAL;
    case ERR_TIMED_OUT: return ETIMEDOUT;
    case ERR_ALREADY_EXISTS: return EEXIST;
    case ERR_CHANNEL_CLOSED: return ENOTCONN;
    case ERR_NOT_ALLOWED: return EPERM;
    case ERR_BAD_PATH: return ENAMETOOLONG;
    case ERR_IO: return EIO;
    case ERR_NOT_DIR: return ENOTDIR;
    case ERR_NOT_SUPPORTED: return ENOTSUP;
    case ERR_TOO_BIG: return E2BIG;
    case ERR_CANCELLED: return ECANCELED;
    case ERR_NOT_IMPLEMENTED: return ENOTSUP;
    case ERR_BUSY: return EBUSY;
    case ERR_OUT_OF_RANGE: return EINVAL;
    case ERR_FAULT: return EFAULT;
    case ERR_NO_RESOURCES: return ENOMEM;
    case ERR_BAD_HANDLE: return EBADFD;
    case ERR_ACCESS_DENIED: return EACCES;

    // no translation
    default: return 999;
    }
}

// set errno to the closest match for error and return -1
static int ERROR(mx_status_t error) {
    errno = status_to_errno(error);
    return -1;
}

// if status is negative, set errno as appropriate and return -1
// otherwise return status
static int STATUS(mx_status_t status) {
    if (status < 0) {
        errno = status_to_errno(status);
        return -1;
    } else {
        return status;
    }
}

// set errno to e, return -1
static inline int ERRNO(int e) {
    errno = e;
    return -1;
}

// The functions from here on provide implementations of fd and path
// centric posix-y io operations.

ssize_t readv(int fd, const struct iovec* iov, int num) {
    ssize_t count = 0;
    ssize_t r;
    while (num > 0) {
        if (iov->iov_len != 0) {
            r = read(fd, iov->iov_base, iov->iov_len);
            if (r < 0) {
                return count ? count : r;
            }
            if ((size_t)r < iov->iov_len) {
                return count + r;
            }
            count += r;
        }
        iov++;
        num--;
    }
    return count;
}

ssize_t writev(int fd, const struct iovec* iov, int num) {
    ssize_t count = 0;
    ssize_t r;
    while (num > 0) {
        if (iov->iov_len != 0) {
            r = write(fd, iov->iov_base, iov->iov_len);
            if (r < 0) {
                return count ? count : r;
            }
            if ((size_t)r < iov->iov_len) {
                return count + r;
            }
            count += r;
        }
        iov++;
        num--;
    }
    return count;
}

int rmdir(const char* path) {
    return ERROR(ERR_NOT_SUPPORTED);
}

int unlinkat(int fd, const char* path, int flag) {
    return ERROR(ERR_NOT_SUPPORTED);
}

ssize_t read(int fd, void* buf, size_t count) {
    if (buf == NULL) {
        return ERRNO(EINVAL);
    }

    mxio_t* io = fd_to_io(fd);
    if (io == NULL) {
        return ERRNO(EBADFD);
    }
    ssize_t r = STATUS(io->ops->read(io, buf, count));
    mxio_release(io);
    return r;
}

ssize_t write(int fd, const void* buf, size_t count) {
    if (buf == NULL) {
        return ERRNO(EINVAL);
    }

    mxio_t* io = fd_to_io(fd);
    if (io == NULL) {
        return ERRNO(EBADFD);
    }
    ssize_t r = STATUS(io->ops->write(io, buf, count));
    mxio_release(io);
    return r;
}

int close(int fd) {
    mxr_mutex_lock(&mxio_lock);
    if ((fd < 0) || (fd >= MAX_MXIO_FD) || (mxio_fdtab[fd] == NULL)) {
        mxr_mutex_unlock(&mxio_lock);
        return ERRNO(EBADFD);
    }
    mxio_t* io = mxio_fdtab[fd];
    io->dupcount--;
    mxio_fdtab[fd] = NULL;
    if (io->dupcount > 0) {
        // still alive in other fdtab slots
        mxr_mutex_unlock(&mxio_lock);
        mxio_release(io);
        return NO_ERROR;
    } else {
        mxr_mutex_unlock(&mxio_lock);
        int r = io->ops->close(io);
        mxio_release(io);
        return STATUS(r);
    }
}

int _dup2(int oldfd, int newfd) {
    mxio_t* io = fd_to_io(oldfd);
    if (io == NULL) {
        return ERRNO(EBADFD);
    }
    int fd = mxio_bind_to_fd(io, newfd);
    if (fd < 0) {
        mxio_release(io);
    }
    return fd;
}

int _dup(int oldfd) {
    return dup2(oldfd, -1);
}

off_t lseek(int fd, off_t offset, int whence) {
    mxio_t* io = fd_to_io(fd);
    if (io == NULL) {
        return ERRNO(EBADFD);
    }
    off_t r = STATUS(io->ops->seek(io, offset, whence));
    mxio_release(io);
    return r;
}

static int getdirents(int fd, void* ptr, size_t len) {
    mxio_t* io = fd_to_io(fd);
    if (io == NULL) {
        return ERRNO(EBADFD);
    }
    int r = STATUS(io->ops->misc(io, MX_RIO_READDIR, len, ptr, 0));
    mxio_release(io);
    return r;
}

int unlink(const char* path) {
    const char* name;
    mxio_t* io;
    mx_status_t r;
    if ((r = __mxio_opendir_containing(&io, path, &name)) < 0) {
        return ERROR(r);
    }
    r = io->ops->misc(io, MX_RIO_UNLINK, 0, (void*) name, strlen(name));
    io->ops->close(io);
    mxio_release(io);
    return STATUS(r);
}

int open(const char* path, int flags, ...) {
    mxio_t* io = NULL;
    mx_status_t r;
    int fd;

    if ((r = __mxio_open(&io, path, flags)) < 0) {
        return ERROR(r);
    }
    if ((fd = mxio_bind_to_fd(io, -1)) < 0) {
        io->ops->close(io);
        mxio_release(io);
        return ERRNO(EMFILE);
    }
    return fd;
}

int fstat(int fd, struct stat* s) {
    mxio_t* io = fd_to_io(fd);
    if (io == NULL) {
        return ERRNO(EBADFD);
    }
    int r = STATUS(mxio_stat(io, s));
    mxio_release(io);
    return r;
}

int stat(const char* fn, struct stat* s) {
    mxio_t* io;
    mx_status_t r;

    if ((r = __mxio_open(&io, fn, 0)) < 0) {
        return ERROR(r);
    }
    r = mxio_stat(io, s);
    mxio_close(io);
    mxio_release(io);
    return STATUS(r);
}

int pipe(int pipefd[2]) {
    mxio_t *a, *b;
    int r = mxio_pipe_pair(&a, &b);
    if (r < 0) {
        return ERROR(r);
    }
    pipefd[0] = mxio_bind_to_fd(a, -1);
    if (pipefd[0] < 0) {
        mxio_close(a);
        mxio_release(a);
        mxio_close(b);
        mxio_release(b);
        return ERROR(pipefd[0]);
    }
    pipefd[1] = mxio_bind_to_fd(b, -1);
    if (pipefd[1] < 0) {
        close(pipefd[0]);
        mxio_close(b);
        mxio_release(b);
        return ERROR(pipefd[1]);
    }
    return 0;
}

int chdir(const char* path) {
    mxio_t* io;
    mx_status_t r;
    if ((r = __mxio_open(&io, path, O_DIRECTORY)) < 0) {
        return STATUS(r);
    }
    mxr_mutex_lock(&mxio_lock);
    mxio_t* old = mxio_cwd_handle;
    mxio_cwd_handle = io;
    old->ops->close(old);
    mxio_release(old);
    mxr_mutex_unlock(&mxio_lock);
    return 0;
}

#define DIR_BUFSIZE 2048

struct __dirstream {
    mxr_mutex_t lock;
    int fd;
    size_t size;
    uint8_t* ptr;
    uint8_t data[DIR_BUFSIZE];
    struct dirent de;
};

DIR* opendir(const char* name) {
    DIR* dir;
    if ((dir = calloc(1, sizeof(DIR))) == NULL) {
        return NULL;
    }
    dir->lock = MXR_MUTEX_INIT;
    dir->size = 0;
    if ((dir->fd = open(name, O_DIRECTORY)) < 0) {
        free(dir);
        return NULL;
    }
    return dir;
}

int closedir(DIR* dir) {
    close(dir->fd);
    free(dir);
    return 0;
}

struct dirent *readdir(DIR* dir) {
    mxr_mutex_lock(&dir->lock);
    struct dirent* de = &dir->de;
    for (;;) {
        if (dir->size >= sizeof(vdirent_t)) {
            vdirent_t* vde = (void*) dir->ptr;
            if (dir->size >= vde->size) {
                de->d_ino = 0;
                de->d_off = 0;
                de->d_reclen = 0;
                de->d_type = vde->type;
                strcpy(de->d_name, vde->name);
                dir->ptr += vde->size;
                dir->size -= vde->size;
                break;
            }
            dir->size = 0;
        }
        int r = getdirents(dir->fd, dir->data, DIR_BUFSIZE);
        if (r > 0) {
            dir->ptr = dir->data;
            dir->size = r;
            continue;
        }
        de = NULL;
        break;
    }
    mxr_mutex_unlock(&dir->lock);
    return de;
}

int isatty(int fd) {
    mxio_t* io = fd_to_io(fd);
    if (io == NULL) {
        errno = EBADF;
        return 0;
    }

    int ret;
    // For now, stdout etc. needs to be a tty for line buffering to
    // work. So let's pretend those are ttys but nothing else is.
    if (fd == 0 || fd == 1 || fd == 2) {
        ret = 1;
    } else {
        ret = 0;
        errno = ENOTTY;
    }

    mxio_release(io);

    return ret;
}
