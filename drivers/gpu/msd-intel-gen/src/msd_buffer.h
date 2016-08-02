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

#ifndef MSD_BUFFER_H
#define MSD_BUFFER_H

#include "magma_util/macros.h"
#include "magma_util/platform_buffer.h"
#include "msd.h"
#include <memory>

class MsdBuffer : public msd_buffer {
public:
    static MsdBuffer* Create(msd_platform_buffer* platform_buffer_token);

    static MsdBuffer* cast(msd_buffer* buf)
    {
        DASSERT(buf);
        DASSERT(buf->magic_ == kMagic);
        return static_cast<MsdBuffer*>(buf);
    }

private:
    MsdBuffer(std::unique_ptr<PlatformBuffer> platform_buf);

    std::unique_ptr<PlatformBuffer> platform_buf_;
    static const uint32_t kMagic = 0x62756666; // "buff"
};

#endif // MSD_BUFFER_H
