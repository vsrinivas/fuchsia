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
#include <mx/channel.h>
#include <mxalloc/new.h>

namespace fs {

VnodeWatcher::VnodeWatcher(mx::channel h, uint32_t mask) : h(mxtl::move(h)), mask(mask) {}

VnodeWatcher::~VnodeWatcher() {}

mx_status_t WatcherContainer::WatchDir(mx_handle_t* out) {
    AllocChecker ac;
    mxtl::unique_ptr<VnodeWatcher> watcher(new (&ac) VnodeWatcher(mx::channel(),
                                                                  VFS_WATCH_MASK_ADDED));
    if (!ac.check()) {
        return MX_ERR_NO_MEMORY;
    }
    mx::channel out_channel;
    if (mx::channel::create(0, &out_channel, &watcher->h) != MX_OK) {
        return MX_ERR_NO_RESOURCES;
    }
    mxtl::AutoLock lock(&lock_);
    watch_list_.push_back(mxtl::move(watcher));
    *out = out_channel.release();
    return MX_OK;
}

constexpr uint32_t kSupportedMasks = VFS_WATCH_MASK_ADDED;

mx_status_t WatcherContainer::WatchDirV2(const vfs_watch_dir_t* cmd) {
    mx::channel c = mx::channel(cmd->channel);
    if ((cmd->mask & VFS_WATCH_MASK_ALL) == 0) {
        // No events to watch
        return MX_ERR_INVALID_ARGS;
    }
    if (cmd->mask & ~kSupportedMasks) {
        // Asking for an unsupported event
        // TODO(smklein): Add more supported events
        return MX_ERR_NOT_SUPPORTED;
    }

    AllocChecker ac;
    mxtl::unique_ptr<VnodeWatcher> watcher(new (&ac) VnodeWatcher(mxtl::move(c), cmd->mask));
    if (!ac.check()) {
        return MX_ERR_NO_MEMORY;
    }
    mxtl::AutoLock lock(&lock_);
    watch_list_.push_back(mxtl::move(watcher));
    return MX_OK;
}

void WatcherContainer::NotifyAdd(const char* name, size_t len) {
    if (len > VFS_WATCH_NAME_MAX) {
        return;
    }

    mxtl::AutoLock lock(&lock_);

    if (watch_list_.is_empty()) {
        return;
    }

    uint8_t msg[sizeof(vfs_watch_msg_t) + len];
    vfs_watch_msg_t* vmsg = reinterpret_cast<vfs_watch_msg_t*>(msg);
    vmsg->event = VFS_WATCH_EVT_ADDED;
    vmsg->len = static_cast<uint8_t>(len);
    memcpy(vmsg->name, name, len);

    for (auto it = watch_list_.begin(); it != watch_list_.end();) {
        if (!(it->mask & VFS_WATCH_MASK_ADDED)) {
            continue;
        }

        mx_status_t status = it->h.write(0, msg, static_cast<uint32_t>(sizeof(msg)), nullptr, 0);
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
