// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "platform_sysmem_connection.h"

#include <fuchsia/sysmem/cpp/fidl.h>
#include <lib/fdio/io.h>
#include <lib/fdio/util.h>
#include <lib/zx/channel.h>

#include "magma_util/macros.h"

namespace magma {

class ZirconPlatformSysmemConnection : public PlatformSysmemConnection {

public:
    ZirconPlatformSysmemConnection(fuchsia::sysmem::AllocatorSyncPtr allocator)
        : sysmem_allocator_(std::move(allocator))
    {
    }

    magma_status_t AllocateBuffer(uint32_t flags, size_t size,
                                  std::unique_ptr<PlatformBuffer>* buffer_out) override
    {
        return AllocateTexture(flags, MAGMA_FORMAT_R8G8B8A8, magma::round_up(size, PAGE_SIZE) / 4,
                               1, buffer_out, nullptr);
    }

    magma_status_t
    AllocateTexture(uint32_t flags, uint32_t format, uint32_t width, uint32_t height,
                    std::unique_ptr<PlatformBuffer>* buffer_out,
                    std::unique_ptr<BufferDescription>* buffer_description_out) override
    {

        if (format != MAGMA_FORMAT_R8G8B8A8) {
            return DRET_MSG(MAGMA_STATUS_INVALID_ARGS, "Invalid format: %d",
                            static_cast<uint32_t>(format));
        }

        fuchsia::sysmem::ImageSpec image_spec;
        image_spec.min_width = width;
        image_spec.min_height = height;
        image_spec.pixel_format.type = fuchsia::sysmem::PixelFormatType::R8G8B8A8;
        fuchsia::sysmem::BufferSpec spec;
        spec.set_image(image_spec);
        fuchsia::sysmem::BufferUsage usage;
        usage.vulkan = fuchsia::sysmem::vulkanUsageTransientAttachment |
                       fuchsia::sysmem::vulkanUsageStencilAttachment |
                       fuchsia::sysmem::vulkanUsageInputAttachment |
                       fuchsia::sysmem::vulkanUsageColorAttachment |
                       fuchsia::sysmem::vulkanUsageTransferSrc |
                       fuchsia::sysmem::vulkanUsageTransferDst |
                       fuchsia::sysmem::vulkanUsageStorage | fuchsia::sysmem::vulkanUsageSampled;
        if (flags & MAGMA_SYSMEM_FLAG_PROTECTED) {
            usage.video = fuchsia::sysmem::videoUsageHwProtected;
        }
        if (flags & MAGMA_SYSMEM_FLAG_DISPLAY) {
            usage.display = fuchsia::sysmem::displayUsageLayer;
        }
        zx_status_t status = ZX_OK;
        fuchsia::sysmem::BufferCollectionInfo info;
        zx_status_t status2 = sysmem_allocator_->AllocateCollection(
            1, std::move(spec), std::move(usage), &status, &info);
        if (status != ZX_OK || status2 != ZX_OK) {
            return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "Failed to allocate buffer: %d %d", status,
                            status2);
        }

        if (!info.vmos[0]) {
            return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "Allocated buffer had no vmo");
        }

        if (buffer_description_out) {
            if (!info.format.is_image()) {
                return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "Allocated buffer was not image");
            }
            *buffer_description_out = std::make_unique<BufferDescription>();
            const fuchsia::sysmem::ImageFormat format = info.format.image();
            for (uint32_t i = 0; i < format.planes.size(); i++) {
                (*buffer_description_out)->planes[i].bytes_per_row = format.planes[i].bytes_per_row;
                (*buffer_description_out)->planes[i].byte_offset = format.planes[i].byte_offset;
            }
        }

        *buffer_out = PlatformBuffer::Import(info.vmos[0].release());
        if (!buffer_out) {
            return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "PlatformBuffer::Import failed");
        }

        return MAGMA_STATUS_OK;
    }

private:
    fuchsia::sysmem::AllocatorSyncPtr sysmem_allocator_;
};

// static
std::unique_ptr<PlatformSysmemConnection> PlatformSysmemConnection::Create()
{
    fuchsia::sysmem::AllocatorSyncPtr sysmem_allocator;
    auto interface_request = sysmem_allocator.NewRequest();
    zx_status_t status = fdio_service_connect("/svc/fuchsia.sysmem.Allocator",
                                              interface_request.TakeChannel().release());
    if (status != ZX_OK) {
        return DRETP(nullptr, "Failed to connect to sysmem allocator");
    }

    return std::make_unique<ZirconPlatformSysmemConnection>(std::move(sysmem_allocator));
}

} // namespace magma
