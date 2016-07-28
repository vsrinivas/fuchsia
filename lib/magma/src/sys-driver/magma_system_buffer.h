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

#ifndef _MAGMA_SYSTEM_BUFFER_H_
#define _MAGMA_SYSTEM_BUFFER_H_

#include "magma_util/platform_buffer.h"
#include "msd.h"

#include <functional>
#include <memory>

struct MagmaSystemDevice;

typedef std::unique_ptr<msd_buffer, decltype(&msd_buffer_destroy)> msd_buffer_unique_ptr_t;

class MagmaSystemBuffer {
public:
    static std::unique_ptr<MagmaSystemBuffer> Create(uint64_t size);
    ~MagmaSystemBuffer() {}

    uint64_t size() { return platform_buf_->size(); }
    uint32_t handle() { return platform_buf_->handle(); }

    // note: this does not relinquish ownership of the PlatformBuffer
    PlatformBuffer* platform_buffer() { return platform_buf_.get(); }

private:
    MagmaSystemBuffer(std::unique_ptr<PlatformBuffer> platform_buf,
                      msd_buffer_unique_ptr_t msd_buf);
    std::unique_ptr<PlatformBuffer> platform_buf_;
    msd_buffer_unique_ptr_t msd_buf_;
};

#endif //_MAGMA_SYSTEM_BUFFER_H_