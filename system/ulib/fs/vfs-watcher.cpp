// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef __Fuchsia__
#include <magenta/device/vfs.h>
#include <magenta/syscalls.h>
#include <mxtl/auto_lock.h>
#endif

#include <fs/vfs.h>
#include <mxalloc/new.h>

namespace fs {

VnodeWatcher::VnodeWatcher() : h(MX_HANDLE_INVALID) {}

VnodeWatcher::~VnodeWatcher() {
    if (h != MX_HANDLE_INVALID) {
        mx_handle_close(h);
    }
}

mx_status_t WatcherContainer::WatchDir(mx_handle_t* out) {
    AllocChecker ac;
    mxtl::unique_ptr<VnodeWatcher> watcher(new (&ac) VnodeWatcher);
    if (!ac.check()) {
        return ERR_NO_MEMORY;
    }
    if (mx_channel_create(0, out, &watcher->h) < 0) {
        return ERR_NO_RESOURCES;
    }
    mxtl::AutoLock lock(&lock_);
    watch_list_.push_back(mxtl::move(watcher));
    return NO_ERROR;
}

void WatcherContainer::NotifyAdd(const char* name, size_t len) {
    if (len > VFS_WATCH_NAME_MAX) {
        return;
    }

    mxtl::AutoLock lock(&lock_);

    if (watch_list_.is_empty()) {
        return;
    }

    uint8_t msg[len + 2];
    msg[0] = VFS_WATCH_EVT_ADDED;
    msg[1] = static_cast<uint8_t>(len);
    memcpy(msg + 2, name, len);

    for (auto it = watch_list_.begin(); it != watch_list_.end();) {
        mx_status_t status = mx_channel_write(it->h, 0, msg,
                                              static_cast<uint32_t>(len + 2),
                                              nullptr, 0);
        if (status < 0) {
            // Lazily remove watchers when their handles cannot accept incoming
            // watch messages.
            auto to_remove = it;
            ++it;
            watch_list_.erase(to_remove);
        } else {
            ++it;
        }
    }
}

} // namespace fs
