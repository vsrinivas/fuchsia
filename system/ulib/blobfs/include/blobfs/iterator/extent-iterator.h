// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <blobfs/format.h>
#include <zircon/types.h>

namespace blobfs {

// Describes an interface which may be utilized to look up nodes.
class NodeFinder {
public:
    virtual ~NodeFinder() = default;

    // Given an index, return a pointer to the requested node.
    //
    // TODO(smklein): Return a zx_status_t to allow for invalid |ino| values.
    virtual NewInode* GetNode(uint32_t ino) = 0;
};

} // namespace blobfs
