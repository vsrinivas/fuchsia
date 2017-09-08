// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdlib.h>
#include <stdint.h>

#include <magenta/types.h>
#include <mx/channel.h>
#include <fbl/ref_counted.h>
#include <mxio/remoteio.h>

namespace fs {

using vfs_dispatcher_cb_t = mxrio_cb_t;

// Dispatcher describes the interface that the VFS layer uses when
// interacting with a dispatcher. Filesystems which intend to be
// dispatcher-independent should only interact with dispatchers
// through this interface.
class Dispatcher {
public:
    virtual ~Dispatcher() {};

    // Add a new object to be handled to the dispatcher.
    // The dispatcher will read from 'channel', and pass the
    // message to the dispatcher callback 'cb'.
    virtual mx_status_t AddVFSHandler(mx::channel channel, vfs_dispatcher_cb_t cb, void* iostate) = 0;
};

} // namespace fs
