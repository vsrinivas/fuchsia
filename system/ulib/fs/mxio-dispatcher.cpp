// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>
#include <stdint.h>

#include <magenta/types.h>
#include <mxalloc/new.h>
#include <mxtl/unique_ptr.h>
#include <mxio/dispatcher.h>
#include <fs/mxio-dispatcher.h>
#include <fs/vfs.h>

namespace fs {

mx_status_t MxioDispatcher::Create(mxtl::unique_ptr<fs::Dispatcher>* out) {
    AllocChecker ac;
    mxtl::unique_ptr<MxioDispatcher> d(new (&ac) MxioDispatcher());
    if (!ac.check()) {
        return ERR_NO_MEMORY;
    }
    mx_status_t status = mxio_dispatcher_create(&d->dispatcher_, mxrio_handler);
    if (status != NO_ERROR) {
        return status;
    }
    mxio_dispatcher_start(d->dispatcher_, "mxio-dispatcher");
    *out = mxtl::move(d);
    return NO_ERROR;
}

mx_status_t MxioDispatcher::AddVFSHandler(mx_handle_t h, void* cb, void* iostate) {
    return mxio_dispatcher_add(dispatcher_, h, cb, iostate);
}

MxioDispatcher::MxioDispatcher() {}

} // namespace fs
