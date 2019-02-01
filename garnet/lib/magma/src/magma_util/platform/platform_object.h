// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PLATFORM_OBJECT_H
#define PLATFORM_OBJECT_H

#include <stdint.h>

namespace magma {

class PlatformObject {
public:
    enum Type { SEMAPHORE = 10 };

    // returns a unique, immutable id for the underlying object
    virtual uint64_t id() = 0;

    // on success, duplicate of the underlying handle which is owned by the caller
    virtual bool duplicate_handle(uint32_t* handle_out) = 0;

    // Returns the id for the given handle
    static bool IdFromHandle(uint32_t handle, uint64_t* id_out);
};

} // namespace magma

#endif // PLATFORM_OBJECT_H
