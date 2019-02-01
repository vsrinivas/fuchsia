// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MAGMA_SRC_MAGMA_UTIL_PLATFORM_PLATFORM_SYSMEM_CONNECTION_H_
#define GARNET_LIB_MAGMA_SRC_MAGMA_UTIL_PLATFORM_PLATFORM_SYSMEM_CONNECTION_H_

#include "magma.h"
#include "magma_sysmem.h"
#include "magma_util/status.h"
#include "platform_buffer.h"

#include <memory>

namespace magma {

class PlatformSysmemConnection {
public:
    struct BufferDescription {
        magma_image_plane_t planes[MAGMA_MAX_IMAGE_PLANES];
    };

    virtual ~PlatformSysmemConnection() {}

    static std::unique_ptr<PlatformSysmemConnection> Create();

    virtual magma_status_t AllocateBuffer(uint32_t flags, size_t size,
                                          std::unique_ptr<PlatformBuffer>* buffer_out) = 0;

    virtual magma_status_t
    AllocateTexture(uint32_t flags, uint32_t format, uint32_t width, uint32_t height,
                    std::unique_ptr<PlatformBuffer>* buffer_out,
                    std::unique_ptr<BufferDescription>* buffer_description_out) = 0;
};

} // namespace magma

#endif // GARNET_LIB_MAGMA_SRC_MAGMA_UTIL_PLATFORM_PLATFORM_SYSMEM_CONNECTION_H_
