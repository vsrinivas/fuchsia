// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sys/socket.h>
#include <unistd.h>

#include <errno.h>
#include <sys/stat.h>

#include "unistd.h"

// checkfile and checkfd let us error out if the object
// doesn't exist, which allows the stubs to be a little
// more 'real'
static int checkfile(const char* path, int err) {
    struct stat s;
    if (stat(path, &s)) {
        return -1;
    }
    if (err) {
        errno = err;
        return -1;
    } else {
        return 0;
    }
}

static int checkfd(int fd, int err) {
    mxio_t* io;
    if ((io = fd_to_io(fd)) == NULL) {
        errno = EBADF;
        return -1;
    }
    mxio_release(io);
    if (err) {
        errno = err;
        return -1;
    } else {
        return 0;
    }
}

static int checksocket(int fd, int sock_err, int err) {
    mxio_t* io = fd_to_io(fd);
    if (io == NULL) {
        errno = EBADF;
        return -1;
    }
    int32_t is_socket = io->flags & MXIO_FLAG_SOCKET;
    mxio_release(io);
    if (!is_socket) {
        errno = sock_err;
        return -1;
    }
    if (err) {
        errno = err;
        return -1;
    }
    return 0;
}

// not supported by any filesystems yet
int link(const char* path, const char* newpath) {
    errno = ENOSYS;
    return -1;
}
int symlink(const char* existing, const char* new) {
    errno = ENOSYS;
    return -1;
}
ssize_t readlink(const char* restrict path, char* restrict buf, size_t bufsize) {
    // EINVAL = not symlink
    return checkfile(path, EINVAL);
}

// creating things we don't have plumbing for yet
int mkfifo(const char *path, mode_t mode) {
    errno = ENOSYS;
    return -1;
}
int mknod(const char* path, mode_t mode, dev_t dev) {
    errno = ENOSYS;
    return -1;
}

// no permissions support yet
int chown(const char *path, uid_t owner, gid_t group) {
    return checkfile(path, ENOSYS);
}
int fchown(int fd, uid_t owner, gid_t group) {
    return checkfd(fd, ENOSYS);
}
int lchown(const char *path, uid_t owner, gid_t group) {
    return checkfile(path, ENOSYS);
}

// no permissions support, but treat rwx bits as don't care rather than error
int chmod(const char *path, mode_t mode) {
    return checkfile(path, (mode & (~0777)) ? ENOSYS : 0);
}
int fchmod(int fd, mode_t mode) {
    return checkfd(fd, (mode & (~0777)) ? ENOSYS : 0);
}

int access(const char* path, int mode) {
    return checkfile(path, 0);
}

// TODO(kulakowski) No symlinks to ignore yet.
int lstat(const char* path, struct stat* buf) {
    return stat(path, buf);
}

void sync(void) {
}
int fsync(int fd) {
    return checkfd(fd, 0);
}
int fdatasync(int fd) {
    return checkfd(fd, 0);
}

// at the moment our unlink works on all fs objects
int rmdir(const char* path) {
    return unlink(path);
}

// Socket stubbing.

int socketpair(int domain, int type, int protocol, int fd[2]) {
    errno = ENOSYS;
    return -1;
}

// So far just a bit of plumbing around checking whether the fds are
// indeed fds, and if so, are indeed sockets.

int shutdown(int fd, int how) {
    return checksocket(fd, ENOTSOCK, ENOSYS);
}

ssize_t sendto(int fd, const void* buf, size_t buflen, int flags, const struct sockaddr* addr, socklen_t addrlen) {
    return checksocket(fd, ENOTSOCK, ENOSYS);
}

ssize_t recvfrom(int fd, void* restrict buf, size_t buflen, int flags, struct sockaddr* restrict addr, socklen_t* restrict addrlen) {
    return checksocket(fd, ENOTSOCK, ENOSYS);
}

ssize_t sendmsg(int fd, const struct msghdr* msg, int flags) {
    return checksocket(fd, ENOTSOCK, ENOSYS);
}

ssize_t recvmsg(int fd, struct msghdr* msg, int flags) {
    return checksocket(fd, ENOTSOCK, ENOSYS);
}

int sendmmsg(int fd, struct mmsghdr* msgvec, unsigned int vlen, unsigned int flags) {
    return checksocket(fd, ENOTSOCK, ENOSYS);
}

int recvmmsg(int fd, struct mmsghdr* msgvec, unsigned int vlen, unsigned int flags, struct timespec* timeout) {
    return checksocket(fd, ENOTSOCK, ENOSYS);
}

int sockatmark(int fd) {
    // ENOTTY is sic.
    return checksocket(fd, ENOTTY, ENOSYS);
}
