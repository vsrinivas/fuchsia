// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "magma_system.h"
#include "magma_util/dlog.h"
#include "magma_util/macros.h"
#include "platform_buffer.h"

#include <errno.h>
#include <unordered_map>

class MockConnection : public magma_system_connection {
public:
    void Insert(uint32_t buffer_id, std::unique_ptr<magma::PlatformBuffer> buffer)
    {
        buffer_map_[buffer_id] = std::move(buffer);
    }

    void Erase(uint32_t buffer_id) { buffer_map_.erase(buffer_id); }

    bool Map(uint32_t buffer_id, void** addr_out)
    {
        auto iter = buffer_map_.find(buffer_id);
        if (iter == buffer_map_.end())
            return DRETF(false, "buffer not found");
        if (!iter->second->MapCpu(addr_out))
            return DRETF(false, "MapCpu failed");
        return true;
    }

    bool Unmap(uint32_t buffer_id)
    {
        auto iter = buffer_map_.find(buffer_id);
        if (iter == buffer_map_.end())
            return DRETF(false, "buffer not found");
        if (!iter->second->UnmapCpu())
            return DRETF(false, "UnmapCpu failed");
        return true;
    }

    uint32_t next_context_id() { return next_context_id_++; }

private:
    std::unordered_map<uint32_t, std::unique_ptr<magma::PlatformBuffer>> buffer_map_;
    uint32_t next_context_id_;
};

magma_system_connection* magma_system_open(int32_t fd, uint32_t capabilities)
{
    return new MockConnection();
}

void magma_system_close(magma_system_connection* connection)
{
    delete static_cast<MockConnection*>(connection);
}

int32_t magma_system_get_error(magma_system_connection* connection) { return 0; }

// Returns the device id.  0 is an invalid device id.
uint32_t magma_system_get_device_id(int32_t fd) { return 0x1916; }

void magma_system_create_context(magma_system_connection* connection, uint32_t* context_id_out)
{
    *context_id_out = static_cast<MockConnection*>(connection)->next_context_id();
}

void magma_system_destroy_context(magma_system_connection* connection, uint32_t context_id) {}

int32_t magma_system_alloc(magma_system_connection* connection, uint64_t size, uint64_t* size_out,
                           uint32_t* buffer_id_out)
{
    auto buffer = magma::PlatformBuffer::Create(size);
    uint32_t id = buffer->id();
    static_cast<MockConnection*>(connection)->Insert(id, std::move(buffer));
    *buffer_id_out = id;
    *size_out = size;
    return 0;
}

void magma_system_free(magma_system_connection* connection, uint32_t buffer_id) 
{
    static_cast<MockConnection*>(connection)->Erase(buffer_id);
}

int32_t magma_system_map(magma_system_connection* connection, uint32_t buffer_id, void** addr_out)
{
    if (!static_cast<MockConnection*>(connection)->Map(buffer_id, addr_out))
        return -EINVAL;
    return 0;
}

int32_t magma_system_unmap(magma_system_connection* connection, uint32_t buffer_id, void* addr)
{
    if (!static_cast<MockConnection*>(connection)->Unmap(buffer_id))
        return -EINVAL;
    return 0;
}

void magma_system_set_tiling_mode(magma_system_connection* connection, uint32_t buffer_id,
                                  uint32_t tiling_mode)
{
}

void magma_system_set_domain(magma_system_connection* connection, uint32_t buffer_id,
                             uint32_t read_domains, uint32_t write_domain)
{
}

void magma_system_submit_command_buffer(struct magma_system_connection* connection,
                                        uint32_t command_buffer_id, uint32_t context_id)
{
}

void magma_system_wait_rendering(magma_system_connection* connection, uint32_t buffer_id) {}

int32_t magma_system_export(magma_system_connection* connection, uint32_t buffer_id,
                            uint32_t* buffer_handle_out)
{
    return 0;
}

void magma_system_display_page_flip(magma_system_connection* connection, uint32_t buffer_id,
                                    magma_system_pageflip_callback_t callback, void* data)
{
  callback(0, data);
}
