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
    ZirconPlatformSysmemConnection(fuchsia::sysmem::Allocator2SyncPtr allocator)
        : sysmem_allocator_(std::move(allocator))
    {
    }

    magma_status_t AllocateBuffer(uint32_t flags, size_t size,
                                  std::unique_ptr<PlatformBuffer>* buffer_out) override
    {
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

        fuchsia::sysmem::BufferCollectionConstraints constraints;
        constraints.usage = usage;
        constraints.min_buffer_count_for_camping = 1;
        constraints.has_buffer_memory_constraints = true;
        constraints.buffer_memory_constraints.min_size_bytes = size;
        if (flags & MAGMA_SYSMEM_FLAG_PROTECTED) {
            constraints.buffer_memory_constraints.secure_required = true;
            constraints.buffer_memory_constraints.secure_permitted = true;
        }
        constraints.image_format_constraints_count = 0;

        fuchsia::sysmem::BufferCollectionInfo_2 info;
        magma_status_t result = AllocateBufferCollection(constraints, &info);
        if (result != MAGMA_STATUS_OK)
            return DRET(result);

        if (info.buffer_count != 1) {
            return DRET(MAGMA_STATUS_INTERNAL_ERROR);
        }

        if (!info.buffers[0].vmo) {
            return DRET(MAGMA_STATUS_INTERNAL_ERROR);
        }

        *buffer_out = PlatformBuffer::Import(info.buffers[0].vmo.release());
        if (!buffer_out) {
            return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "PlatformBuffer::Import failed");
        }

        return MAGMA_STATUS_OK;
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
        fuchsia::sysmem::BufferCollectionConstraints constraints;
        constraints.usage = usage;
        constraints.min_buffer_count_for_camping = 1;
        constraints.has_buffer_memory_constraints = true;
        if (flags & MAGMA_SYSMEM_FLAG_PROTECTED) {
            constraints.buffer_memory_constraints.secure_required = true;
            constraints.buffer_memory_constraints.secure_permitted = true;
        }
        if (flags & MAGMA_SYSMEM_FLAG_DISPLAY) {
            // For now, assume using amlogic display.
            // TODO(ZX-3355) Send token to display connection.
            constraints.buffer_memory_constraints.physically_contiguous_required = true;
        }
        constraints.image_format_constraints_count = 1;
        auto& format_constraints = constraints.image_format_constraints[0];
        format_constraints = fuchsia::sysmem::ImageFormatConstraints();
        format_constraints.color_spaces_count = 1;
        format_constraints.color_space[0].type = fuchsia::sysmem::ColorSpaceType::SRGB;
        format_constraints.min_coded_width = width;
        format_constraints.max_coded_width = width;
        format_constraints.min_coded_height = height;
        format_constraints.max_coded_height = height;
        format_constraints.min_bytes_per_row = 0;
        format_constraints.max_bytes_per_row = 0xffffffff;
        if (flags & MAGMA_SYSMEM_FLAG_DISPLAY) {
            // For now, assume using amlogic display
            // TODO(ZX-3355) Send token to display connection.
            format_constraints.bytes_per_row_divisor = 32;
        }
        format_constraints.pixel_format.type = fuchsia::sysmem::PixelFormatType::R8G8B8A8;
        format_constraints.layers = 1;
        fuchsia::sysmem::BufferCollectionInfo_2 info;
        magma_status_t result = AllocateBufferCollection(constraints, &info);
        if (result != MAGMA_STATUS_OK)
            return DRET(result);

        if (info.buffer_count != 1) {
            return DRET(MAGMA_STATUS_INTERNAL_ERROR);
        }

        if (!info.buffers[0].vmo) {
            return DRET(MAGMA_STATUS_INTERNAL_ERROR);
        }

        if (buffer_description_out) {
            if (!info.settings.has_image_format_constraints) {
                return DRET(MAGMA_STATUS_INTERNAL_ERROR);
            }
            *buffer_description_out = std::make_unique<BufferDescription>();
            (*buffer_description_out)->planes[0].bytes_per_row =
                info.settings.image_format_constraints.min_bytes_per_row;
            (*buffer_description_out)->planes[0].byte_offset = 0;
        }

        *buffer_out = PlatformBuffer::Import(info.buffers[0].vmo.release());
        if (!buffer_out) {
            return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "PlatformBuffer::Import failed");
        }

        return MAGMA_STATUS_OK;
    }

private:
    magma_status_t
    AllocateBufferCollection(const fuchsia::sysmem::BufferCollectionConstraints& constraints,
                             fuchsia::sysmem::BufferCollectionInfo_2* info_out)
    {
        fuchsia::sysmem::BufferCollectionSyncPtr collection;
        zx_status_t status =
            sysmem_allocator_->AllocateNonSharedCollection(collection.NewRequest());
        if (status != ZX_OK) {
            return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "Failed to allocate buffer: %d", status);
        }

        status = collection->SetConstraints(true, std::move(constraints));

        if (status != ZX_OK) {
            return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "Failed to set constraints: %d", status);
        }

        zx_status_t status2;
        status = collection->WaitForBuffersAllocated(&status2, info_out);

        // Ignore failure - this just prevents unnecessary logged errors.
        collection->Close();

        if (status != ZX_OK || status2 != ZX_OK) {
            return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "Failed wait for allocation: %d %d",
                            status, status2);
        }

        return MAGMA_STATUS_OK;
    }

    fuchsia::sysmem::Allocator2SyncPtr sysmem_allocator_;
};

// static
std::unique_ptr<PlatformSysmemConnection> PlatformSysmemConnection::Create()
{
    fuchsia::sysmem::DriverConnectorSyncPtr connector;
    auto interface_request = connector.NewRequest();
    zx_status_t status =
        fdio_service_connect("/dev/class/sysmem/000", interface_request.TakeChannel().release());
    if (status != ZX_OK) {
        return DRETP(nullptr, "Failed to connect to sysmem driver, status %d", status);
    }
    fuchsia::sysmem::Allocator2SyncPtr sysmem_allocator;
    status = connector->Connect(sysmem_allocator.NewRequest());
    if (status != ZX_OK) {
        return DRETP(nullptr, "Failed to connect to sysmem allocator, status %d", status);
    }
    return std::make_unique<ZirconPlatformSysmemConnection>(std::move(sysmem_allocator));
}

} // namespace magma
