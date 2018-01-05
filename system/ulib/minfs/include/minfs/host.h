// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#ifdef __Fuchsia__
#error Host-only Header
#endif

#include <dirent.h>
#include <fcntl.h>
#include <stdbool.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <fbl/macros.h>
#include <fbl/unique_ptr.h>
#include <minfs/bcache.h>

#define PATH_PREFIX "::"
#define PREFIX_SIZE 2

// Return "true" if the path refers to a file on the
// host machine. Otherwise, refers to a file within the
// target disk image.
static inline bool host_path(const char* path) {
    if (strncmp(path, PATH_PREFIX, PREFIX_SIZE)) {
        return true;
    }
    return false;
}
int emu_mkfs(const char* path);
int emu_mount(const char* path);
int emu_mount_bcache(fbl::unique_ptr<minfs::Bcache> bc);

int emu_open(const char* path, int flags, mode_t mode);
int emu_close(int fd);

ssize_t emu_write(int fd, const void* buf, size_t count);
ssize_t emu_pwrite(int fd, const void* buf, size_t count, off_t off);
ssize_t emu_read(int fd, void* buf, size_t count);
ssize_t emu_pread(int fd, void* buf, size_t count, off_t off);

int emu_ftruncate(int fd, off_t len);
off_t emu_lseek(int fd, off_t offset, int whence);
int emu_fstat(int fd, struct stat* s);
int emu_stat(const char* fn, struct stat* s);

int emu_mkdir(const char* path, mode_t mode);
DIR* emu_opendir(const char* name);
struct dirent* emu_readdir(DIR* dirp);
void emu_rewinddir(DIR* dirp);
int emu_closedir(DIR* dirp);

// FileWrapper is a wrapper around an open file descriptor,
// which abstracts away the "hostness" or "targetness"
// of the underlying target. Additionally, it provides
// RAII semantics to the underlying file descriptor.
class FileWrapper {
public:
    DISALLOW_COPY_ASSIGN_AND_MOVE(FileWrapper);
    constexpr FileWrapper() : hostfile_(false), fd_(0) {};

    ~FileWrapper() {
        Close();
    }

    static int Open(const char* path, int flags, mode_t mode, FileWrapper* out) {
        int r;
        if (host_path(path)) {
            out->hostfile_ = true;
            r = open(path, flags, mode);
        } else {
            out->hostfile_ = false;
            r = emu_open(path, flags, mode);
        }
        if (r > 0) {
            out->fd_ = r;
        }
        return r;
    }
    int Close() {
        if (fd_ == 0) {
            return -1;
        }
        int r = hostfile_ ? close(fd_) : emu_close(fd_);
        fd_ = 0;
        return r;
    }
    ssize_t Read(void* buf, size_t count) {
        return hostfile_ ? read(fd_, buf, count) : emu_read(fd_, buf, count);
    }
    ssize_t Write(void* buf, size_t count) {
        return hostfile_ ? write(fd_, buf, count) : emu_write(fd_, buf, count);
    }
private:
    bool hostfile_;
    int fd_{};
};

// DirWrapper is a wrapper around an open directory,
// which abstracts away the "hostness" or "targetness"
// of the underlying target. Additionally, it provides
// RAII semantics to the underlying directory.
class DirWrapper {
public:
    DISALLOW_COPY_ASSIGN_AND_MOVE(DirWrapper);
    constexpr DirWrapper() : hostdir_(false), dir_(NULL) {};

    ~DirWrapper() {
        Close();
    }

    static int Make(const char* path, mode_t mode) {
        return host_path(path) ? mkdir(path, mode) : emu_mkdir(path, mode);
    }
    static int Open(const char* path, DirWrapper* out) {
        if (host_path(path)) {
            out->hostdir_ = true;
            out->dir_ = opendir(path);
        } else {
            out->hostdir_ = false;
            out->dir_ = emu_opendir(path);
        }
        return out->dir_ == NULL ? -1 : 0;
    }
    int Close() {
        return hostdir_ ? closedir(dir_) : emu_closedir(dir_);
    }
    struct dirent* ReadDir() {
        return hostdir_ ? readdir(dir_) : emu_readdir(dir_);
    }
private:
    bool hostdir_;
    DIR* dir_;
};
