// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef _MAGMA_SYSTEM_CONNECTION_H_
#define _MAGMA_SYSTEM_CONNECTION_H_

#include "magma_system.h"
#include "magma_system_buffer.h"
#include "magma_system_context.h"
#include "magma_util/macros.h"
#include "msd.h"

#include <map>
#include <memory>

using msd_connection_unique_ptr_t =
    std::unique_ptr<msd_connection, decltype(&msd_connection_close)>;

static inline msd_connection_unique_ptr_t MsdConnectionUniquePtr(msd_connection* conn)
{
    return msd_connection_unique_ptr_t(conn, &msd_connection_close);
}

class MagmaSystemConnection : public magma_system_connection, private MagmaSystemContext::Owner {
public:
    class Owner {
    public:
        virtual uint32_t GetDeviceId() = 0;
    };

    MagmaSystemConnection(Owner* owner, msd_connection_unique_ptr_t msd_connection);

    // Allocates a buffer of at least the requested size and adds it to the
    // buffer map. Returns a shared_ptr to the allocated buffer on success
    // or nullptr on allocation failure
    std::shared_ptr<MagmaSystemBuffer> AllocateBuffer(uint64_t size);
    // Attempts to locate a buffer by handle in the buffer map and return it.
    // Returns nullptr if the buffer is not found
    std::shared_ptr<MagmaSystemBuffer> LookupBuffer(uint32_t handle);
    // This removes the reference to the shared_ptr in the map
    // other instances remain valid until deleted
    // Returns false if no buffer with the given handle exists in the map
    bool FreeBuffer(uint32_t handle);

    bool CreateContext(uint32_t* context_id_out);
    bool DestroyContext(uint32_t context_id);
    MagmaSystemContext* LookupContext(uint32_t context_id);

    uint32_t GetDeviceId() { return owner_->GetDeviceId(); }

    msd_connection* msd_connection() { return msd_connection_.get(); }

    static MagmaSystemConnection* cast(magma_system_connection* connection)
    {
        DASSERT(connection);
        DASSERT(connection->magic_ == kMagic);
        return static_cast<MagmaSystemConnection*>(connection);
    }

private:
    Owner* owner_;
    msd_connection_unique_ptr_t msd_connection_;
    std::map<uint32_t, std::shared_ptr<MagmaSystemBuffer>> buffer_map_;
    std::map<uint32_t, std::unique_ptr<MagmaSystemContext>> context_map_;
    uint32_t next_context_id_{};

    // MagmaSystemContext::Owner
    std::shared_ptr<MagmaSystemBuffer> LookupBufferForContext(uint32_t handle) override
    {
        return LookupBuffer(handle);
    }

    static const uint32_t kMagic = 0x636f6e6e; // "conn" (Connection)
};

#endif //_MAGMA_SYSTEM_CONNECTION_H_
