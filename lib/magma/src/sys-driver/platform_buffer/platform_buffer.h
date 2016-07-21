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

#ifndef PLATFORM_BUFFER_H
#define PLATFORM_BUFFER_H

#include <magma_util/dlog.h>
#include <platform_buffer_abi.h>

class PlatformBuffer {
public:
    static PlatformBuffer* Create(uint64_t size);
    static PlatformBuffer* Create(PlatformBufferToken* token);

    ~PlatformBuffer();

    uint64_t size() { return size_; }

    uint32_t handle() { return handle_; }

    int Map(void** addr_out);

    int Unmap();

    int GetBackingStore(BackingStore* backing_store);

    PlatformBufferToken* token() { return token_; }

    PlatformBuffer(const PlatformBuffer&) = delete;
    void operator=(const PlatformBuffer&) = delete;

private:
    PlatformBuffer(PlatformBufferToken* token, uint64_t size, uint32_t handle);

    PlatformBufferToken* token_;
    uint64_t size_;
    uint32_t handle_;
};

#endif // PLATFORM_BUFFER_H
