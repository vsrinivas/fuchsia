// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef _MAGMA_DEVICE_H_
#define _MAGMA_DEVICE_H_

#include "magma.h"
#include "magma_buffer.h"
#include "magma_system.h"
#include <libdrm_intel_gen.h>

#include <map>
#include <stdint.h>

class MagmaConnection : public magma_connection {
public:
    static MagmaConnection* Open(uint32_t device_handle, int batch_size);
    ~MagmaConnection();

    magma_system_connection* sys_connection() { return sys_connection_; }

    uint64_t max_relocs() { return max_relocs_; }
    uint32_t GetDeviceId() { return magma_system_get_device_id(sys_connection_); }

    bool Init(uint64_t batch_size);

    MagmaBuffer* AllocBufferObject(const char* name, uint64_t size, uint32_t align,
                                   uint32_t tiling_mode, uint32_t stride);

    bool CreateContext(uint32_t* context_id)
    {
        return magma_system_create_context(sys_connection_, context_id);
    }

    bool ExecuteBuffer(MagmaBuffer* buffer, int context_id, uint32_t batch_len, uint32_t flags);

    static MagmaConnection* cast(magma_connection* device)
    {
        DASSERT(device);
        DASSERT(device->magic_ == kMagic);
        return static_cast<MagmaConnection*>(device);
    }

private:
    MagmaConnection(magma_system_connection* sys_connection);

    magma_system_connection* sys_connection_;
    LibdrmIntelGen* libdrm_;

    static const uint32_t kMagic = 0x636f6e6e; // "conn" (Connection)

    uint64_t max_relocs_{};
};

#endif // _MAGMA_DEVICE_H_
