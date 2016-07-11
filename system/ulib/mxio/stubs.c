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
#include <sys/stat.h>

#include <unistd.h>

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
        errno = EBADFD;
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

// no plumbing for these yet
int truncate(const char* path, off_t len) {
    return checkfile(path, ENOSYS);
}
int ftruncate(int fd, off_t len) {
    return checkfd(fd, ENOSYS);
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
int mkdir(const char* path, mode_t mode) {
    errno = ENOSYS;
    return -1;
}
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

int fcntl(int fd, int cmd, ...) {
    return checkfd(fd, ENOSYS);
}

int access(const char* path, int mode) {
    return checkfile(path, 0);
}

void sync(void) {
}
int fsync(int fd) {
    return checkfd(fd, 0);
}
int fdatasync(int fd) {
    return checkfd(fd, 0);
}

int rename(const char* oldpath, const char* newpath) {
    return checkfile(oldpath, ENOSYS);
}

// at the moment our unlink works on all fs objects
int rmdir(const char* path) {
    return unlink(path);
}
int remove(const char* path) {
    return unlink(path);
}
