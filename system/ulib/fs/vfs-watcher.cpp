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

VnodeWatcher::VnodeWatcher(mx::channel h, uint32_t mask) : h(mxtl::move(h)),
    mask(mask & ~(VFS_WATCH_MASK_EXISTING | VFS_WATCH_MASK_IDLE)) {}

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

constexpr uint32_t kSupportedMasks = VFS_WATCH_MASK_ADDED |
                                     VFS_WATCH_MASK_EXISTING |
                                     VFS_WATCH_MASK_IDLE;

mx_status_t WatcherContainer::WatchDirV2(Vnode* vn, const vfs_watch_dir_t* cmd) {
    mx::channel c = mx::channel(cmd->channel);
    if ((cmd->mask & VFS_WATCH_MASK_ALL) == 0) {
        // No events to watch
        return MX_ERR_INVALID_ARGS;
    }
    if (cmd->mask & ~kSupportedMasks) {
        // Asking for an unsupported event
        return MX_ERR_NOT_SUPPORTED;
    }

    AllocChecker ac;
    mxtl::unique_ptr<VnodeWatcher> watcher(new (&ac) VnodeWatcher(mxtl::move(c), cmd->mask));
    if (!ac.check()) {
        return MX_ERR_NO_MEMORY;
    }

    if (cmd->mask & VFS_WATCH_MASK_EXISTING) {
        vdircookie_t dircookie;
        memset(&dircookie, 0, sizeof(dircookie));
        char buf[MXIO_CHUNK_SIZE];
        {
            // Send "VFS_WATCH_EVT_EXISTING" for all entries in readdir
            mxtl::AutoLock lock(&vfs_lock);
            while (true) {
                mx_status_t status = vn->Readdir(&dircookie, &buf, sizeof(buf));
                if (status <= 0) {
                    break;
                }
                void* ptr = buf;
                while (status > 0) {
                    auto dirent = reinterpret_cast<vdirent_t*>(ptr);
                    if (dirent->name[0]) {
                        size_t len = strlen(dirent->name);
                        uint8_t msg[sizeof(vfs_watch_msg_t) + len];
                        vfs_watch_msg_t* vmsg = reinterpret_cast<vfs_watch_msg_t*>(msg);
                        vmsg->event = static_cast<uint8_t>(VFS_WATCH_EVT_EXISTING);
                        vmsg->len = static_cast<uint8_t>(len);
                        memcpy(vmsg->name, dirent->name, len);
                        watcher->h.write(0, msg, static_cast<uint32_t>(sizeof(msg)), nullptr, 0);
                    }
                    status -= dirent->size;
                    ptr = reinterpret_cast<void*>(
                            static_cast<uintptr_t>(dirent->size) +
                            reinterpret_cast<uintptr_t>(ptr));
                }
            }
        }

        // Send VFS_WATCH_EVT_IDLE to signify that readdir has completed
        if (cmd->mask & VFS_WATCH_MASK_IDLE) {
            uint8_t msg[sizeof(vfs_watch_msg_t)];
            vfs_watch_msg_t* vmsg = reinterpret_cast<vfs_watch_msg_t*>(msg);
            vmsg->event = static_cast<uint8_t>(VFS_WATCH_EVT_IDLE);
            vmsg->len = 0;
            watcher->h.write(0, msg, static_cast<uint32_t>(sizeof(msg)), nullptr, 0);
        }
    }

    mxtl::AutoLock lock(&lock_);
    watch_list_.push_back(mxtl::move(watcher));
    return MX_OK;
}

void WatcherContainer::Notify(const char* name, size_t len, unsigned event) {
    if (len > VFS_WATCH_NAME_MAX) {
        return;
    }

    mxtl::AutoLock lock(&lock_);

    if (watch_list_.is_empty()) {
        return;
    }

    uint8_t msg[sizeof(vfs_watch_msg_t) + len];
    vfs_watch_msg_t* vmsg = reinterpret_cast<vfs_watch_msg_t*>(msg);
    vmsg->event = static_cast<uint8_t>(event);
    vmsg->len = static_cast<uint8_t>(len);
    memcpy(vmsg->name, name, len);

    for (auto it = watch_list_.begin(); it != watch_list_.end();) {
        if (!(it->mask & VFS_WATCH_EVT_MASK(event))) {
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
