// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#ifndef __Fuchsia__
#error "Fuchsia-only header"
#endif

#include <fbl/intrusive_double_list.h>
#include <fbl/macros.h>
#include <fbl/mutex.h>
#include <fbl/unique_ptr.h>
#include <fs/vfs.h>
#include <magenta/device/vfs.h>
#include <mx/channel.h>

namespace fs {

// Implements directory watching , holding a list of watchers
class WatcherContainer {
public:
    WatcherContainer();
    ~WatcherContainer();

    mx_status_t WatchDir(mx::channel* out);
    mx_status_t WatchDirV2(Vfs* vfs, Vnode* vn, const vfs_watch_dir_t* cmd);

    // Notifies all VnodeWatchers in the watch list, if their mask
    // indicates they are interested in the incoming event.
    void Notify(const char* name, size_t len, unsigned event);
private:
    DISALLOW_COPY_ASSIGN_AND_MOVE(WatcherContainer);

    // A simple structure which holds a channel to a watching client,
    // as well as a mask of signals they are interested in hearing about.
    struct VnodeWatcher : public fbl::DoublyLinkedListable<fbl::unique_ptr<VnodeWatcher>> {
        VnodeWatcher(mx::channel h, uint32_t mask);
        ~VnodeWatcher();

        mx::channel h;
        uint32_t mask;
    };

    fbl::Mutex lock_;
    fbl::DoublyLinkedList<fbl::unique_ptr<VnodeWatcher>> watch_list_ __TA_GUARDED(lock_);
};

}
