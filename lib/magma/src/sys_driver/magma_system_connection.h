// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef _MAGMA_SYSTEM_CONNECTION_H_
#define _MAGMA_SYSTEM_CONNECTION_H_

#include "magma_system_buffer.h"
#include "magma_system_context.h"
#include "magma_util/macros.h"
#include "magma_util/platform/platform_connection.h"
#include "msd.h"

#include <memory>
#include <unordered_map>

using msd_connection_unique_ptr_t =
    std::unique_ptr<msd_connection_t, decltype(&msd_connection_close)>;

static inline msd_connection_unique_ptr_t MsdConnectionUniquePtr(msd_connection_t* conn)
{
    return msd_connection_unique_ptr_t(conn, &msd_connection_close);
}

class MagmaSystemDevice;

class MagmaSystemConnection : private MagmaSystemContext::Owner,
                              public magma::PlatformConnection::Delegate {
public:
    MagmaSystemConnection(std::weak_ptr<MagmaSystemDevice> device,
                          msd_connection_unique_ptr_t msd_connection_t, uint32_t capabilities);

    ~MagmaSystemConnection() override;

    // Create a buffer from the handle and add it to the map,
    // on success |id_out| contains the id to be used to query the map
    bool ImportBuffer(uint32_t handle, uint64_t* id_out) override;
    // This removes the reference to the shared_ptr in the map
    // other instances remain valid until deleted
    // Returns false if no buffer with the given |id| exists in the map
    bool ReleaseBuffer(uint64_t id) override;

    bool ImportObject(uint32_t handle, magma::PlatformObject::Type object_type) override;
    bool ReleaseObject(uint64_t object_id, magma::PlatformObject::Type object_type) override;

    // Attempts to locate a buffer by |id| in the buffer map and return it.
    // Returns nullptr if the buffer is not found
    std::shared_ptr<MagmaSystemBuffer> LookupBuffer(uint64_t id);

    // Returns the msd_semaphore for the given |id| if present in the semaphore map.
    std::shared_ptr<MagmaSystemSemaphore> LookupSemaphore(uint64_t id);

    magma::Status ExecuteCommandBuffer(uint32_t command_buffer_handle,
                                       uint32_t context_id) override;

    bool CreateContext(uint32_t context_id) override;
    bool DestroyContext(uint32_t context_id) override;
    MagmaSystemContext* LookupContext(uint32_t context_id);

    magma::Status WaitRendering(uint64_t buffer_id) override;

    uint32_t GetDeviceId();

    msd_connection_t* msd_connection() { return msd_connection_.get(); }

    magma::Status PageFlip(uint64_t id, uint32_t wait_semaphore_count,
                           uint32_t signal_semaphore_count, uint64_t* semaphore_ids) override;

private:
    // MagmaSystemContext::Owner
    std::shared_ptr<MagmaSystemBuffer> LookupBufferForContext(uint64_t id) override
    {
        return LookupBuffer(id);
    }
    std::shared_ptr<MagmaSystemSemaphore> LookupSemaphoreForContext(uint64_t id) override
    {
        return LookupSemaphore(id);
    }

    std::weak_ptr<MagmaSystemDevice> device_;
    msd_connection_unique_ptr_t msd_connection_;
    std::unordered_map<uint32_t, std::unique_ptr<MagmaSystemContext>> context_map_;
    std::unordered_map<uint64_t, std::shared_ptr<MagmaSystemBuffer>> buffer_map_;
    std::unordered_map<uint64_t, std::shared_ptr<MagmaSystemSemaphore>> semaphore_map_;

    bool has_display_capability_;
    bool has_render_capability_;
};

#endif //_MAGMA_SYSTEM_CONNECTION_H_
