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

class PlatformBufferConstraints {
public:
    virtual ~PlatformBufferConstraints() {}

    virtual Status
    SetImageFormatConstraints(uint32_t index,
                              const magma_image_format_constraints_t* format_constraints) = 0;
};

class PlatformBufferCollection {
public:
    virtual ~PlatformBufferCollection() {}

    virtual Status SetConstraints(PlatformBufferConstraints* constraints) = 0;
};

class PlatformSysmemConnection {
public:
    struct BufferDescription {
        magma_image_plane_t planes[MAGMA_MAX_IMAGE_PLANES] = {};
    };

    virtual ~PlatformSysmemConnection() {}

    static std::unique_ptr<PlatformSysmemConnection> Create();

    static magma_status_t
    DecodeBufferDescription(const uint8_t* image_data, uint64_t image_data_size,
                            std::unique_ptr<BufferDescription>* buffer_description_out);

    virtual magma_status_t AllocateBuffer(uint32_t flags, size_t size,
                                          std::unique_ptr<PlatformBuffer>* buffer_out) = 0;

    virtual magma_status_t
    AllocateTexture(uint32_t flags, uint32_t format, uint32_t width, uint32_t height,
                    std::unique_ptr<PlatformBuffer>* buffer_out,
                    std::unique_ptr<BufferDescription>* buffer_description_out) = 0;

    virtual Status CreateBufferCollectionToken(uint32_t* handle_out) = 0;
    virtual Status
    ImportBufferCollection(uint32_t handle,
                           std::unique_ptr<PlatformBufferCollection>* collection_out) = 0;

    virtual Status
    CreateBufferConstraints(const magma_buffer_format_constraints_t* constraints,
                            std::unique_ptr<PlatformBufferConstraints>* constraints_out) = 0;
};

} // namespace magma

#endif // GARNET_LIB_MAGMA_SRC_MAGMA_UTIL_PLATFORM_PLATFORM_SYSMEM_CONNECTION_H_
