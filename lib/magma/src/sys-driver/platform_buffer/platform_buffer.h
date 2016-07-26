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

#include "magma_util/dlog.h"
#include "msd_platform_buffer.h"
#include <memory>

class PlatformBuffer {
public:
    // Returned token is owned by the returned unique_ptr and will become invalid when the
    // is released unique_ptr.
    static std::unique_ptr<PlatformBuffer> Create(uint64_t size, msd_platform_buffer** token_out);
    static std::unique_ptr<PlatformBuffer> Create(uint64_t size);
    static std::unique_ptr<PlatformBuffer> Create(msd_platform_buffer* token);

    ~PlatformBuffer();

    uint64_t size() { return size_; }
    uint32_t handle() { return handle_; }

    // Refer to c abi docs
    bool MapCpu(void** addr_out);
    bool UnmapCpu();

    bool PinPages(uint32_t* num_pages_out);
    bool UnpinPages();

    bool MapPageCpu(uint32_t page_index, void** addr_out);
    bool UnmapPageCpu(uint32_t page_index);

    bool MapPageBus(uint32_t page_index, uint64_t* addr_out);
    bool UnmapPageBus(uint32_t page_index);

    PlatformBuffer(const PlatformBuffer&) = delete;
    void operator=(const PlatformBuffer&) = delete;

private:
    PlatformBuffer(msd_platform_buffer* token, uint64_t size, uint32_t handle);

    msd_platform_buffer* token_;
    uint64_t size_;
    uint32_t handle_;
};

#endif // PLATFORM_BUFFER_H
