// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdlib.h>
#include <stdint.h>

#include <magenta/types.h>
#include <mxtl/ref_counted.h>
#include <mxio/dispatcher.h>

namespace fs {

// Dispatcher describes the interface that the VFS layer uses when
// interacting with a dispatcher. Filesystems which intend to be
// dispatcher-independent should only interact with dispatchers
// through this interface.
class Dispatcher {
public:
    virtual ~Dispatcher() {};

    // Add a new object to be handled to the dispatcher
    // TODO(smklein): Avoid using 'void*' arguments wherever possible.
    virtual mx_status_t AddVFSHandler(mx_handle_t h, void* cb, void* iostate) = 0;
};

} // namespace fs
