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

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <unistd.h>

#include <magenta/processargs.h>
#include <magenta/syscalls.h>

#include <mxio/debug.h>
#include <mxio/io.h>
#include <mxio/remoteio.h>
#include <mxio/util.h>
#include <mxio/vfs.h>

#define MXDEBUG 0

// non-thread-safe emulation of unistd io functions
// using the mxio transports

static mxio_t* mxio_root_handle = NULL;

static mx_handle_t mxio_process_handle = 0;

static mxio_t* mxio_fdtab[MAX_MXIO_FD] = {
    NULL,
};

void mxio_install_root(mxio_t* root) {
    if (mxio_root_handle == NULL) {
        mxio_root_handle = root;
    }
}

//TODO: fd's pointing to same mxio, refcount, etc

int mxio_bind_to_fd(mxio_t* io, int fd) {
    if (fd >= 0) {
        if (fd >= MAX_MXIO_FD)
            return ERR_INVALID_ARGS;
        if (mxio_fdtab[fd])
            return ERR_ALREADY_EXISTS;
        mxio_fdtab[fd] = io;
        return fd;
    }
    for (fd = 0; fd < MAX_MXIO_FD; fd++) {
        if (mxio_fdtab[fd] == NULL) {
            mxio_fdtab[fd] = io;
            return fd;
        }
    }
    return ERR_NO_RESOURCES;
}

static inline mxio_t* fd_to_io(int fd) {
    if ((fd < 0) || (fd >= MAX_MXIO_FD))
        return NULL;
    return mxio_fdtab[fd];
}

static void mxio_exit(void) {
    int fd;
    for (fd = 0; fd < MAX_MXIO_FD; fd++) {
        mxio_t* io = mxio_fdtab[fd];
        if (io) {
            io->ops->close(io);
            mxio_fdtab[fd] = NULL;
        }
    }
}

mx_handle_t mxio_get_process_handle(void) {
    return mxio_process_handle;
}

// hook into libc process startup
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
            break;
        case MX_HND_TYPE_MXIO_PIPE:
            mxio_fdtab[arg] = mxio_pipe_create(h);
            break;
        case MX_HND_TYPE_PROC_SELF:
            mxio_process_handle = h;
            continue;
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
        }
    }

    atexit(mxio_exit);
}

mx_status_t mxio_clone_root(mx_handle_t* handles, uint32_t* types) {
    // TODO: better solution
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
    return r;
}

ssize_t mxio_ioctl(int fd, int op, const void* in_buf, size_t in_len, void* out_buf, size_t out_len) {
    mxio_t* io;
    if ((io = fd_to_io(fd)) == NULL) {
        return ERR_BAD_HANDLE;
    }
    if (!io->ops->ioctl) {
        return ERR_NOT_SUPPORTED;
    }
    return io->ops->ioctl(io, op, in_buf, in_len, out_buf, out_len);
}

mx_status_t mxio_wait_fd(int fd, uint32_t events, uint32_t* pending, mx_time_t timeout) {
    mxio_t* io = fd_to_io(fd);
    if (io == NULL)
        return ERR_BAD_HANDLE;
    return io->ops->wait(io, events, pending, timeout);
}

int mx_stat(mxio_t* io, struct stat* s) {
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
// centric posix-y io operations (eg, their method signatures match
// the standard headers even if the names are sometimes prefixed by
// __libc_io_ for plumbing purposes.

int __libc_io_readv(int fd, const struct iovec* iov, int num) {
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

int __libc_io_writev(int fd, const struct iovec* iov, int num) {
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

int __libc_io_rmdir(const char* path) {
    return ERROR(ERR_NOT_SUPPORTED);
}

int __libc_io_unlink(const char* path) {
    return ERROR(ERR_NOT_SUPPORTED);
}

int __libc_io_unlinkat(int fd, const char* path, int flag) {
    return ERROR(ERR_NOT_SUPPORTED);
}

ssize_t read(int fd, void* buf, size_t count) {
    mxio_t* io = fd_to_io(fd);
    if (io == NULL) {
        return ERRNO(EBADFD);
    }
    if (buf == NULL) {
        return ERROR(EINVAL);
    }
    return STATUS(io->ops->read(io, buf, count));
}

ssize_t __libc_io_write(int fd, const void* buf, size_t count) {
    mxio_t* io = fd_to_io(fd);
    if (io == NULL) {
        return ERRNO(EBADFD);
    }
    if (buf == NULL) {
        return ERRNO(EINVAL);
    }
    return STATUS(io->ops->write(io, buf, count));
}

ssize_t write(int fd, const void* data, size_t len) {
    return __libc_io_write(fd, data, len);
}

int __libc_io_close(int fd) {
    mxio_t* io = fd_to_io(fd);
    if (io == NULL) {
        return ERRNO(EBADFD);
    }
    int r = io->ops->close(io);
    mxio_fdtab[fd] = NULL;
    return STATUS(r);
}

off_t lseek(int fd, off_t offset, int whence) {
    mxio_t* io = fd_to_io(fd);
    if (io == NULL) {
        return ERRNO(EBADFD);
    }
    return STATUS(io->ops->seek(io, offset, whence));
}

int getdirents(int fd, void* ptr, size_t len) {
    mxio_t* io = fd_to_io(fd);
    if (io == NULL) {
        return ERRNO(EBADFD);
    }
    return STATUS(io->ops->misc(io, MX_RIO_READDIR, len, ptr, 0));
}

int __libc_io_open(const char* path, int flags, ...) {
    mxio_t* io = NULL;
    mx_status_t r;
    int fd;
    if (path == NULL) {
        return ERRNO(EBADFD);
    }
    if (mxio_root_handle == NULL) {
        return ERRNO(EBADFD);
    }
    r = mxio_root_handle->ops->open(mxio_root_handle, path, flags, &io);
    if (r < 0) {
        return ERROR(r);
    }
    if (io == NULL) {
        return ERRNO(EIO);
    }
    fd = mxio_bind_to_fd(io, -1);
    if (fd < 0) {
        io->ops->close(io);
        return ERRNO(EMFILE);
    }
    return fd;
}

int fstat(int fd, struct stat* s) {
    mxio_t* io = fd_to_io(fd);
    if (io == NULL) {
        return ERRNO(EBADFD);
    }
    return STATUS(mx_stat(io, s));
}

int stat(const char* fn, struct stat* s) {
    mxio_t* io;
    mx_status_t r;
    if (fn == NULL) {
        return ERRNO(EINVAL);
    }
    if (mxio_root_handle == NULL) {
        return ERRNO(EBADFD);
    }
    if ((r = mx_open(mxio_root_handle, fn, 0, &io)) == 0) {
        r = mx_stat(io, s);
        mx_close(io);
    }
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
        mx_close(a);
        mx_close(b);
        return ERROR(pipefd[0]);
    }
    pipefd[1] = mxio_bind_to_fd(b, -1);
    if (pipefd[1] < 0) {
        close(pipefd[0]);
        mx_close(b);
        return ERROR(pipefd[1]);
    }
    return 0;
}
