// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>
#include <stdint.h>

#include <magenta/types.h>
#include <fbl/alloc_checker.h>
#include <fbl/unique_ptr.h>
#include <mxio/dispatcher.h>
#include <fs/mxio-dispatcher.h>
#include <fs/vfs.h>

namespace fs {

mx_status_t MxioDispatcher::Create(fbl::unique_ptr<fs::MxioDispatcher>* out) {
    fbl::AllocChecker ac;
    fbl::unique_ptr<MxioDispatcher> d(new (&ac) MxioDispatcher());
    if (!ac.check()) {
        return MX_ERR_NO_MEMORY;
    }
    mx_status_t status = mxio_dispatcher_create(&d->dispatcher_, mxrio_handler);
    if (status != MX_OK) {
        return status;
    }
    *out = fbl::move(d);
    return MX_OK;
}

mx_status_t MxioDispatcher::StartThread() {
    return mxio_dispatcher_start(dispatcher_, "libfs-mxio-dispatcher");
}

void MxioDispatcher::RunOnCurrentThread() {
    mxio_dispatcher_run(dispatcher_);
}

mx_status_t MxioDispatcher::AddVFSHandler(mx::channel channel, vfs_dispatcher_cb_t cb, void* iostate) {
    return mxio_dispatcher_add(dispatcher_, channel.release(), (void*) cb, iostate);
}

MxioDispatcher::MxioDispatcher() {}

} // namespace fs
