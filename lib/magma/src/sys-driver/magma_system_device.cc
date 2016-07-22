// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "magma_system_device.h"
#include "magma_util/macros.h"

std::shared_ptr<MagmaSystemBuffer> MagmaSystemDevice::AllocateBuffer(uint64_t size)
{
    msd_platform_buffer* token;

    std::unique_ptr<PlatformBuffer> platform_buffer(PlatformBuffer::Create(size, &token));
    if (!platform_buffer)
        return DRETP(nullptr, "Failed to create PlatformBuffer");

    std::shared_ptr<MagmaSystemBuffer> buffer(
        new MagmaSystemBuffer(std::move(platform_buffer), token));
    if (!buffer)
        return DRETP(nullptr, "Failed to create MagmaSystemBuffer");

    buffer_map_.insert(std::make_pair(buffer->handle(), buffer));

    return buffer;
}

bool MagmaSystemDevice::FreeBuffer(uint32_t handle)
{
    auto iter = buffer_map_.find(handle);
    if (iter == buffer_map_.end())
        return false;

    buffer_map_.erase(iter);
    return true;
}

std::shared_ptr<MagmaSystemBuffer> MagmaSystemDevice::LookupBuffer(uint32_t handle)
{
    auto iter = buffer_map_.find(handle);
    if (iter == buffer_map_.end())
        return nullptr;

    return iter->second;
}

uint32_t MagmaSystemDevice::GetDeviceId() { return msd_device_get_id(arch()); }
