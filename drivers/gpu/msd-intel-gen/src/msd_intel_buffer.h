// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MSD_BUFFER_H
#define MSD_BUFFER_H

#include "magma_util/macros.h"
#include "magma_util/platform_buffer.h"
#include "msd.h"
#include <memory>

class MsdIntelBuffer : public msd_buffer {
public:
    static MsdIntelBuffer* Create(msd_platform_buffer* platform_buffer_token);
    static MsdIntelBuffer* Create(uint64_t size);

    static MsdIntelBuffer* cast(msd_buffer* buf)
    {
        DASSERT(buf);
        DASSERT(buf->magic_ == kMagic);
        return static_cast<MsdIntelBuffer*>(buf);
    }

    magma::PlatformBuffer* platform_buffer()
    {
        DASSERT(platform_buf_);
        return platform_buf_.get();
    }

private:
    MsdIntelBuffer(std::unique_ptr<magma::PlatformBuffer> platform_buf);

    std::unique_ptr<magma::PlatformBuffer> platform_buf_;
    static const uint32_t kMagic = 0x62756666; // "buff"
};

#endif // MSD_BUFFER_H
