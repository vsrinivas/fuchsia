// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef _MAGMA_CONNECTION_H_
#define _MAGMA_CONNECTION_H_

#include "magma.h"
#include "magma_buffer.h"
#include "magma_context.h"
#include "magma_system.h"
#include "magma_util/macros.h"

#include <map>
#include <stdint.h>

class MagmaConnection : public magma_connection {
public:
    static MagmaConnection* Open(int32_t fd, int batch_size);
    ~MagmaConnection();

    magma_system_connection* sys_connection() { return sys_connection_; }

    bool Init(uint64_t batch_size);

    MagmaBuffer* AllocBufferObject(const char* name, uint64_t size, uint32_t align,
                                   uint32_t tiling_mode, uint32_t stride);

    bool ExportBufferObject(MagmaBuffer* buffer, uint32_t* buffer_handle_out);

    MagmaContext* CreateContext()
    {
        uint32_t context_id;
        magma_system_create_context(sys_connection_, &context_id);

        return new MagmaContext(this, context_id);
    }

    bool DestroyContext(MagmaContext* context)
    {
        magma_system_destroy_context(sys_connection_, context->context_id());
        return true;
    }

    uint64_t batch_size() { return batch_size_; }

    static MagmaConnection* cast(magma_connection* device)
    {
        DASSERT(device);
        DASSERT(device->magic_ == kMagic);
        return static_cast<MagmaConnection*>(device);
    }

private:
    MagmaConnection(magma_system_connection* sys_connection);

    magma_system_connection* sys_connection_;

    static const uint32_t kMagic = 0x636f6e6e; // "conn" (Connection)

    uint64_t batch_size_{};
};

#endif // _MAGMA_CONNECTION_H_
