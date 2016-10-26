// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef _MAGMA_SYSTEM_CONNECTION_H_
#define _MAGMA_SYSTEM_CONNECTION_H_

#include "magma_system_buffer.h"
#include "magma_system_buffer_manager.h"
#include "magma_system_context.h"
#include "magma_util/macros.h"
#include "magma_util/platform/platform_connection.h"
#include "msd.h"

#include <memory>
#include <unordered_map>

using msd_connection_unique_ptr_t =
    std::unique_ptr<msd_connection, decltype(&msd_connection_close)>;

static inline msd_connection_unique_ptr_t MsdConnectionUniquePtr(msd_connection* conn)
{
    return msd_connection_unique_ptr_t(conn, &msd_connection_close);
}

class MagmaSystemConnection : public MagmaSystemBufferManager,
                              private MagmaSystemContext::Owner,
                              public magma::PlatformConnection::Delegate {
public:
    class Owner : virtual public MagmaSystemBufferManager::Owner {
    public:
        virtual uint32_t GetDeviceId() = 0;
    };

    MagmaSystemConnection(Owner* owner, msd_connection_unique_ptr_t msd_connection);

    bool ImportBuffer(uint32_t handle, uint64_t* id_out) override
    {
        return MagmaSystemBufferManager::ImportBuffer(handle, id_out);
    }

    bool ReleaseBuffer(uint64_t id) override { return MagmaSystemBufferManager::ReleaseBuffer(id); }

    bool ExecuteCommandBuffer(magma_system_command_buffer* command_buffer,
                              uint32_t context_id) override;

    bool CreateContext(uint32_t context_id) override;
    bool DestroyContext(uint32_t context_id) override;
    MagmaSystemContext* LookupContext(uint32_t context_id);

    bool WaitRendering(uint64_t buffer_id) override;

    uint32_t GetDeviceId() { return owner_->GetDeviceId(); }

    msd_connection* msd_connection() { return msd_connection_.get(); }

private:
    Owner* owner_;
    msd_connection_unique_ptr_t msd_connection_;
    std::unordered_map<uint32_t, std::unique_ptr<MagmaSystemContext>> context_map_;

    // MagmaSystemContext::Owner
    std::shared_ptr<MagmaSystemBuffer> LookupBufferForContext(uint64_t id) override
    {
        return LookupBuffer(id);
    }
};

#endif //_MAGMA_SYSTEM_CONNECTION_H_
