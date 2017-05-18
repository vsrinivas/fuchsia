// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdlib.h>
#include <stdint.h>

#include <magenta/types.h>
#include <mxtl/unique_ptr.h>
#include <mxio/dispatcher.h>
#include <fs/vfs.h>

#include "dispatcher.h"

namespace fs {

// MxioDispatcher wraps the C-based single-threaded mxio dispatcher.
class MxioDispatcher final : public fs::Dispatcher {
public:
    DISALLOW_COPY_ASSIGN_AND_MOVE(MxioDispatcher);
    static mx_status_t Create(mxtl::unique_ptr<fs::Dispatcher>* out);
    mx_status_t AddVFSHandler(mx_handle_t h, void* cb, void* iostate);
private:
    MxioDispatcher();
    mxio_dispatcher_t* dispatcher_;
};

} // namespace fs
