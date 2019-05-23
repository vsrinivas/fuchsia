// Copyright 2018 The Fuchsia Authors. All rights reserved.ee
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "magma_common_defs.h"
#include "platform_sysmem_connection.h"

#include <fuchsia/sysmem/cpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/io.h>
#include <lib/zx/channel.h>

#include <limits>

#include "magma_util/macros.h"

using magma::Status;

namespace magma_sysmem {
class ZirconPlatformBufferConstraints : public PlatformBufferConstraints {
public:
    virtual ~ZirconPlatformBufferConstraints() {}

    ZirconPlatformBufferConstraints(const magma_buffer_format_constraints_t* constraints)
    {
        constraints_.min_buffer_count_for_camping = constraints->count;
        // Ignore input usage
        fuchsia::sysmem::BufferUsage usage;
        usage.vulkan = fuchsia::sysmem::vulkanUsageTransientAttachment |
                       fuchsia::sysmem::vulkanUsageStencilAttachment |
                       fuchsia::sysmem::vulkanUsageInputAttachment |
                       fuchsia::sysmem::vulkanUsageColorAttachment |
                       fuchsia::sysmem::vulkanUsageTransferSrc |
                       fuchsia::sysmem::vulkanUsageTransferDst |
                       fuchsia::sysmem::vulkanUsageStorage | fuchsia::sysmem::vulkanUsageSampled;
        constraints_.usage = usage;
        constraints_.has_buffer_memory_constraints = true;
        // No buffer constraints, except those passed directly through from the client. These two
        // are for whether this memory should be protected (e.g. usable for DRM content, the precise
        // definition depending on the system).
        constraints_.buffer_memory_constraints.secure_required = constraints->secure_required;
        constraints_.buffer_memory_constraints.ram_domain_supported =
            constraints->ram_domain_supported;
        constraints_.buffer_memory_constraints.cpu_domain_supported =
            constraints->cpu_domain_supported;
    }

    Status
    SetImageFormatConstraints(uint32_t index,
                              const magma_image_format_constraints_t* format_constraints) override
    {
        if (index >= constraints_.image_format_constraints.size())
            return DRET(MAGMA_STATUS_INVALID_ARGS);
        if (index > constraints_.image_format_constraints_count)
            return DRET_MSG(MAGMA_STATUS_INVALID_ARGS, "Format constraint gaps not allowed");

        constraints_.image_format_constraints_count =
            std::max(constraints_.image_format_constraints_count, index + 1);
        auto& constraints = constraints_.image_format_constraints[index];
        // Initialize to default, since the array constructor initializes to 0
        // normally.
        constraints = fuchsia::sysmem::ImageFormatConstraints();
        constraints.color_spaces_count = 1;
        constraints.min_coded_width = format_constraints->width;
        constraints.max_coded_width = format_constraints->width;
        constraints.min_coded_height = format_constraints->height;
        constraints.max_coded_height = format_constraints->height;
        constraints.min_bytes_per_row = format_constraints->min_bytes_per_row;
        constraints.max_bytes_per_row =
            std::numeric_limits<decltype(constraints.max_bytes_per_row)>::max();

        switch (format_constraints->image_format) {
            case MAGMA_FORMAT_R8G8B8A8:
                constraints.pixel_format.type = fuchsia::sysmem::PixelFormatType::R8G8B8A8;
                constraints.color_space[0].type = fuchsia::sysmem::ColorSpaceType::SRGB;
                break;
            case MAGMA_FORMAT_BGRA32:
                constraints.pixel_format.type = fuchsia::sysmem::PixelFormatType::BGRA32;
                constraints.color_space[0].type = fuchsia::sysmem::ColorSpaceType::SRGB;
                break;
            case MAGMA_FORMAT_NV12:
                constraints.pixel_format.type = fuchsia::sysmem::PixelFormatType::NV12;
                constraints.color_space[0].type = fuchsia::sysmem::ColorSpaceType::REC709;
                break;
            default:
                return DRET_MSG(MAGMA_STATUS_INVALID_ARGS, "Invalid format: %d",
                                format_constraints->image_format);
        }
        constraints.pixel_format.has_format_modifier = format_constraints->has_format_modifier;
        constraints.pixel_format.format_modifier.value = format_constraints->format_modifier;
        constraints.layers = format_constraints->layers;
        constraints.bytes_per_row_divisor = format_constraints->bytes_per_row_divisor;
        return MAGMA_STATUS_OK;
    }
    fuchsia::sysmem::BufferCollectionConstraints constraints() { return constraints_; }

private:
    fuchsia::sysmem::BufferCollectionConstraints constraints_ = {};
};

static Status
InitializeDescriptionFromSettings(const fuchsia::sysmem::SingleBufferSettings& settings,
                                  PlatformBufferDescription* description_out)
{
    switch (settings.buffer_settings.coherency_domain) {
        case fuchsia::sysmem::CoherencyDomain::Ram:
            description_out->coherency_domain = MAGMA_COHERENCY_DOMAIN_RAM;
            break;

        case fuchsia::sysmem::CoherencyDomain::Cpu:
            description_out->coherency_domain = MAGMA_COHERENCY_DOMAIN_CPU;
            break;

        default:
            return DRET_MSG(MAGMA_STATUS_INVALID_ARGS, "Unsupported coherency domain: %d",
                            settings.buffer_settings.coherency_domain);
    }

    description_out->is_secure = settings.buffer_settings.is_secure;
    description_out->has_format_modifier =
        settings.image_format_constraints.pixel_format.has_format_modifier;
    description_out->format_modifier =
        settings.image_format_constraints.pixel_format.format_modifier.value;
    description_out->planes[0].bytes_per_row =
        magma::round_up(settings.image_format_constraints.min_bytes_per_row,
                        settings.image_format_constraints.bytes_per_row_divisor);
    description_out->planes[0].byte_offset = 0;
    if (settings.image_format_constraints.pixel_format.type ==
        fuchsia::sysmem::PixelFormatType::NV12) {
        // Planes are assumed to be tightly-packed for now.
        description_out->planes[1].bytes_per_row = description_out->planes[0].bytes_per_row;
        description_out->planes[1].byte_offset = description_out->planes[0].bytes_per_row *
                                                 settings.image_format_constraints.min_coded_height;
    } else if (settings.image_format_constraints.pixel_format.type !=
                   fuchsia::sysmem::PixelFormatType::BGRA32 &&
               settings.image_format_constraints.pixel_format.type !=
                   fuchsia::sysmem::PixelFormatType::R8G8B8A8) {
        // Sysmem should have given a format that was listed as supported.
        DASSERT(false);
    }
    return MAGMA_STATUS_OK;
}

class ZirconPlatformBufferCollection : public PlatformBufferCollection {
public:
    ~ZirconPlatformBufferCollection() override
    {
        if (collection_.is_bound())
            collection_->Close();
    }

    Status Bind(const fuchsia::sysmem::AllocatorSyncPtr& allocator, uint32_t handle)
    {
        fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token((zx::channel(handle)));
        zx_status_t status =
            allocator->BindSharedCollection(std::move(token), collection_.NewRequest());
        if (status != ZX_OK)
            return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "Internal error: %d", status);
        return MAGMA_STATUS_OK;
    }

    Status SetConstraints(PlatformBufferConstraints* constraints) override
    {
        zx_status_t status = collection_->SetConstraints(
            true, static_cast<ZirconPlatformBufferConstraints*>(constraints)->constraints());
        if (status != ZX_OK) {
            return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "Error setting constraints: %d", status);
        }
        return MAGMA_STATUS_OK;
    }

    Status
    GetBufferDescription(std::unique_ptr<PlatformBufferDescription>* description_out) override
    {
        fuchsia::sysmem::BufferCollectionInfo_2 info;
        zx_status_t status2;
        zx_status_t status = collection_->WaitForBuffersAllocated(&status2, &info);

        if (status != ZX_OK || status2 != ZX_OK) {
            return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "Failed wait for allocation: %d %d",
                            status, status2);
        }

        if (!info.settings.has_image_format_constraints) {
            return DRET(MAGMA_STATUS_INTERNAL_ERROR);
        }
        auto description = std::make_unique<PlatformBufferDescription>();
        Status magma_status = InitializeDescriptionFromSettings(info.settings, description.get());
        description->count = info.buffer_count;
        if (!magma_status.ok()) {
            return DRET(magma_status.get());
        }
        *description_out = std::move(description);
        return MAGMA_STATUS_OK;
    }

    Status GetBufferHandle(uint32_t index, uint32_t* handle_out, uint32_t* offset_out) override
    {
        fuchsia::sysmem::BufferCollectionInfo_2 info;
        zx_status_t status2;
        zx_status_t status = collection_->WaitForBuffersAllocated(&status2, &info);

        if (status != ZX_OK || status2 != ZX_OK) {
            return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "Failed wait for allocation: %d %d",
                            status, status2);
        }

        if (info.buffer_count < index) {
            return DRET(MAGMA_STATUS_INVALID_ARGS);
        }
        *handle_out = info.buffers[index].vmo.release();
        *offset_out = info.buffers[index].vmo_usable_start;
        return MAGMA_STATUS_OK;
    }

private:
    fuchsia::sysmem::BufferCollectionSyncPtr collection_;
};

class ZirconPlatformSysmemConnection : public PlatformSysmemConnection {

public:
    ZirconPlatformSysmemConnection(fuchsia::sysmem::AllocatorSyncPtr allocator)
        : sysmem_allocator_(std::move(allocator))
    {
    }

    magma_status_t AllocateBuffer(uint32_t flags, size_t size,
                                  std::unique_ptr<magma::PlatformBuffer>* buffer_out) override
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

        *buffer_out = magma::PlatformBuffer::Import(info.buffers[0].vmo.release());
        if (!buffer_out) {
            return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "PlatformBuffer::Import failed");
        }

        return MAGMA_STATUS_OK;
    }

    Status CreateBufferCollectionToken(uint32_t* handle_out) override
    {
        fuchsia::sysmem::BufferCollectionTokenSyncPtr token;
        zx_status_t status = sysmem_allocator_->AllocateSharedCollection(token.NewRequest());
        if (status != ZX_OK) {
            return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "AllocateSharedCollection failed: %d",
                            status);
        }
        *handle_out = token.Unbind().TakeChannel().release();
        return MAGMA_STATUS_OK;
    }

    Status
    ImportBufferCollection(uint32_t handle,
                           std::unique_ptr<PlatformBufferCollection>* collection_out) override
    {
        auto collection = std::make_unique<ZirconPlatformBufferCollection>();
        Status status = collection->Bind(sysmem_allocator_, handle);
        if (!status.ok()) {
            return DRET(status.get());
        }

        *collection_out = std::move(collection);
        return MAGMA_STATUS_OK;
    }

    Status
    CreateBufferConstraints(const magma_buffer_format_constraints_t* constraints,
                            std::unique_ptr<PlatformBufferConstraints>* constraints_out) override
    {
        *constraints_out = std::make_unique<ZirconPlatformBufferConstraints>(constraints);
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

    fuchsia::sysmem::AllocatorSyncPtr sysmem_allocator_;
};

// static
std::unique_ptr<PlatformSysmemConnection> PlatformSysmemConnection::Create()
{
    fuchsia::sysmem::AllocatorSyncPtr sysmem_allocator;
    zx_status_t status = fdio_service_connect(
        "/svc/fuchsia.sysmem.Allocator", sysmem_allocator.NewRequest().TakeChannel().release());
    if (status != ZX_OK) {
        return DRETP(nullptr, "Failed to connect to sysmem service, status %d", status);
    }
    return std::make_unique<ZirconPlatformSysmemConnection>(std::move(sysmem_allocator));
}

// static
magma_status_t PlatformSysmemConnection::DecodeBufferDescription(
    const uint8_t* image_data, uint64_t image_data_size,
    std::unique_ptr<PlatformBufferDescription>* buffer_description_out)
{
    std::vector<uint8_t> copy_message(image_data, image_data + image_data_size);
    fidl::Message msg(fidl::BytePart(copy_message.data(), image_data_size, image_data_size),
                      fidl::HandlePart());
    const char* err_msg = nullptr;
    zx_status_t status = msg.Decode(fuchsia::sysmem::SingleBufferSettings::FidlType, &err_msg);
    if (status != ZX_OK) {
        return DRET_MSG(MAGMA_STATUS_INVALID_ARGS, "Invalid SingleBufferSettings: %d %s", status,
                        err_msg);
    }
    fidl::Decoder decoder(std::move(msg));
    fuchsia::sysmem::SingleBufferSettings buffer_settings;
    fuchsia::sysmem::SingleBufferSettings::Decode(&decoder, &buffer_settings, 0);

    if (!buffer_settings.has_image_format_constraints) {
        return DRET_MSG(MAGMA_STATUS_INVALID_ARGS, "Buffer is not image");
    }

    auto description = std::make_unique<PlatformBufferDescription>();
    description->count = 1u;
    Status magma_status = InitializeDescriptionFromSettings(buffer_settings, description.get());
    if (!magma_status.ok()) {
        return DRET(magma_status.get());
    }

    *buffer_description_out = std::move(description);
    return MAGMA_STATUS_OK;
}

} // namespace magma_sysmem
