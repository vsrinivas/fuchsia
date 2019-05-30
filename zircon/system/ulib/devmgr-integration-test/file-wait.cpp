// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/devmgr-integration-test/fixture.h>

#include <errno.h>
#include <fcntl.h>

#include <fbl/unique_fd.h>
#include <lib/fdio/watcher.h>

namespace devmgr_integration_test {

// Waits for |file| to appear in |dir|, and opens it when it does.  Times out if
// the deadline passes.
zx_status_t WaitForFile(const fbl::unique_fd& dir, const char* file, zx::time deadline,
                        fbl::unique_fd* out) {
    auto watch_func = [](int dirfd, int event, const char* fn, void* cookie) -> zx_status_t {
        auto file = reinterpret_cast<const char*>(cookie);
        if (event != WATCH_EVENT_ADD_FILE) {
            return ZX_OK;
        }
        if (!strcmp(fn, file)) {
            return ZX_ERR_STOP;
        }
        return ZX_OK;
    };

    zx_status_t status = fdio_watch_directory(dir.get(), watch_func, deadline.get(),
                                              const_cast<char*>(file));
    if (status != ZX_ERR_STOP) {
        return status;
    }
    out->reset(openat(dir.get(), file, O_RDWR));
    if (!out->is_valid()) {
        return ZX_ERR_IO;
    }
    return ZX_OK;
}

namespace {

// This variant of WaitForFile opens the file specified relative to the rootdir,
// using the full_path. This is a workaround to deal with the fact that devhosts
// do not implement open_at.
zx_status_t WaitForFile2(const fbl::unique_fd& rootdir, const fbl::unique_fd& dir,
                         const char* full_path, const char* file, zx::time deadline,
                         fbl::unique_fd* out) {
    auto watch_func = [](int dirfd, int event, const char* fn, void* cookie) -> zx_status_t {
        auto file = reinterpret_cast<const char*>(cookie);
        if (event != WATCH_EVENT_ADD_FILE) {
            return ZX_OK;
        }
        if (!strcmp(fn, file)) {
            return ZX_ERR_STOP;
        }
        return ZX_OK;
    };

    zx_status_t status = fdio_watch_directory(dir.get(), watch_func, deadline.get(),
                                              const_cast<char*>(file));
    if (status != ZX_ERR_STOP) {
        return status;
    }
    out->reset(openat(rootdir.get(), full_path, O_RDWR));
    if (!out->is_valid()) {
        return ZX_ERR_IO;
    }
    return ZX_OK;
}

// Version of recursive_wait_for_file that can mutate its path
zx_status_t RecursiveWaitForFileHelper(const fbl::unique_fd& rootdir, const fbl::unique_fd& dir,
                                       const char* full_path, char* path, zx::time deadline,
                                       fbl::unique_fd* out) {
    char* first_slash = strchr(path, '/');
    if (first_slash == nullptr) {
        // If there's no first slash, then we're just waiting for the file
        // itself to appear.
        return WaitForFile2(rootdir, dir, full_path, path, deadline, out);
    }
    *first_slash = 0;

    fbl::unique_fd next_dir;
    zx_status_t status = WaitForFile2(rootdir, dir, full_path, path, deadline, &next_dir);
    if (status != ZX_OK) {
        return status;
    }
    *first_slash = '/';
    return RecursiveWaitForFileHelper(rootdir, next_dir, full_path, first_slash + 1,
                                      deadline, out);
}

} // namespace

// Waits for the relative |path| starting in |dir| to appear, and opens it.
zx_status_t RecursiveWaitForFile(const fbl::unique_fd& dir, const char* path,
                                 zx::time deadline, fbl::unique_fd* out) {
    char path_copy[PATH_MAX];
    if (strlen(path) >= sizeof(path_copy)) {
        return ZX_ERR_INVALID_ARGS;
    }
    strcpy(path_copy, path);
    zx_status_t status = RecursiveWaitForFileHelper(dir, dir, path_copy, path_copy, deadline, out);
    if (status != ZX_OK) {
        // TODO(FLK-299): Remove this once the root cause is found.
        fbl::unique_fd check(openat(dir.get(), path, O_RDWR));
        if (check.is_valid()) {
            printf("wait-for-file failed with %d, but opening %s succeeded\n", status, path);
        } else {
            printf("wait-for-file failed with %d, and opening %s failed with %d\n", status, path,
                errno);
        }
    }
    return status;
}

} // namespace devmgr_integration_test
