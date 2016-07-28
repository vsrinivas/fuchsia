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

#include "magma_system_buffer.h"
#include "magma_system_device.h"
#include "magma_util/macros.h"

MagmaSystemBuffer::MagmaSystemBuffer(std::unique_ptr<PlatformBuffer> platform_buf,
                                     msd_buffer_unique_ptr_t msd_buf)
    : platform_buf_(std::move(platform_buf)), msd_buf_(std::move(msd_buf))
{
}

std::unique_ptr<MagmaSystemBuffer> MagmaSystemBuffer::Create(uint64_t size)
{
    msd_platform_buffer* token;

    std::unique_ptr<PlatformBuffer> platform_buffer(PlatformBuffer::Create(size, &token));
    if (!platform_buffer)
        return DRETP(nullptr, "Failed to create PlatformBuffer");

    msd_buffer_unique_ptr_t msd_buf(msd_buffer_import(token), &msd_buffer_destroy);
    if (!msd_buf)
        return DRETP(nullptr,
                     "Failed to import newly allocated buffer into the MSD Implementation");

    return std::unique_ptr<MagmaSystemBuffer>(
        new MagmaSystemBuffer(std::move(platform_buffer), std::move(msd_buf)));
}
