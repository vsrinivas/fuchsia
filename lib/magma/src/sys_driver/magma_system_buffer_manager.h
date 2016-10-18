// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef _MAGMA_SYSTEM_BUFFER_MANAGER_H_
#define _MAGMA_SYSTEM_BUFFER_MANAGER_H_

#include "magma_system_buffer.h"

#include <memory>
#include <unordered_map>

class MagmaSystemBufferManager {
public:
    class Owner {
    public:
        // Create a buffer for |handle| or, if one already exists, give me that
        virtual std::shared_ptr<MagmaSystemBuffer> GetBufferForHandle(uint32_t handle) = 0;
        // Notifies the owner that this BufferManager no longer cares about the buffer with |id|
        virtual void ReleaseBuffer(uint64_t id) = 0;
    };
    MagmaSystemBufferManager(Owner* owner);

    // Create a buffer from the handle and add it to the map,
    // on success |id_out| contains the id to be used to query the map
    bool ImportBuffer(uint32_t handle, uint64_t* id_out);
    // Attempts to locate a buffer by |id| in the buffer map and return it.
    // Returns nullptr if the buffer is not found
    std::shared_ptr<MagmaSystemBuffer> LookupBuffer(uint64_t id);
    // This removes the reference to the shared_ptr in the map
    // other instances remain valid until deleted
    // Returns false if no buffer with the given |id| exists in the map
    bool ReleaseBuffer(uint64_t id);

private:
    Owner* owner_;
    std::unordered_map<uint64_t, std::shared_ptr<MagmaSystemBuffer>> buffer_map_;
};

#endif //_MAGMA_SYSTEM_BUFFER_MANAGER_H_