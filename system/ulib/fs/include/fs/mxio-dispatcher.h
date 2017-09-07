// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdlib.h>
#include <stdint.h>

#include <magenta/types.h>
#include <fbl/unique_ptr.h>
#include <mxio/dispatcher.h>
#include <fs/vfs.h>

#include "dispatcher.h"

namespace fs {

// MxioDispatcher wraps the C-based single-threaded mxio dispatcher.
class MxioDispatcher final : public fs::Dispatcher {
public:
    // Creates the dispatcher.
    // This should eventually be followed by a call to Start() or Run() depending
    // on which thread the dispatcher should run on.
    static mx_status_t Create(fbl::unique_ptr<fs::MxioDispatcher>* out);

    // Starts the dispatcher on a new thread.
    mx_status_t StartThread();

    // Runs the dispatcher on the current thread.
    void RunOnCurrentThread();

    mx_status_t AddVFSHandler(mx::channel channel, vfs_dispatcher_cb_t cb, void* iostate) override;

private:
    MxioDispatcher();

    mxio_dispatcher_t* dispatcher_;

    DISALLOW_COPY_ASSIGN_AND_MOVE(MxioDispatcher);
};

} // namespace fs
