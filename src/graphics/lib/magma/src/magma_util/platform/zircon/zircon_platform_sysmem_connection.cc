// Copyright 2018 The Fuchsia Authors. All rights reserved.ee
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/sysmem/llcpp/fidl.h>
#include <lib/fidl/llcpp/memory.h>
#include <lib/image-format-llcpp/image-format-llcpp.h>
#include <lib/zx/channel.h>

#include <limits>

#include "magma_common_defs.h"
#include "magma_util/macros.h"
#include "platform_sysmem_connection.h"
#include "platform_thread.h"

using magma::Status;

namespace magma_sysmem {

class ZirconPlatformBufferDescription : public PlatformBufferDescription {
 public:
  ZirconPlatformBufferDescription(uint32_t buffer_count,
                                  llcpp::fuchsia::sysmem::SingleBufferSettings settings)
      : buffer_count_(buffer_count), settings_(settings) {}
  ~ZirconPlatformBufferDescription() override {}

  bool IsValid() {
    switch (settings_.buffer_settings.coherency_domain) {
      case llcpp::fuchsia::sysmem::CoherencyDomain::RAM:
      case llcpp::fuchsia::sysmem::CoherencyDomain::CPU:
      case llcpp::fuchsia::sysmem::CoherencyDomain::INACCESSIBLE:
        break;

      default:
        return DRETF(false, "Unsupported coherency domain: %d",
                     settings_.buffer_settings.coherency_domain);
    }
    return true;
  }

  bool is_secure() const override { return settings_.buffer_settings.is_secure; }

  uint32_t count() const override { return buffer_count_; }
  bool has_format_modifier() const override {
    return settings_.image_format_constraints.pixel_format.has_format_modifier;
  }
  uint64_t format_modifier() const override {
    return settings_.image_format_constraints.pixel_format.format_modifier.value;
  }
  uint32_t coherency_domain() const override {
    switch (settings_.buffer_settings.coherency_domain) {
      case llcpp::fuchsia::sysmem::CoherencyDomain::RAM:
        return MAGMA_COHERENCY_DOMAIN_RAM;

      case llcpp::fuchsia::sysmem::CoherencyDomain::CPU:
        return MAGMA_COHERENCY_DOMAIN_CPU;

      case llcpp::fuchsia::sysmem::CoherencyDomain::INACCESSIBLE:
        return MAGMA_COHERENCY_DOMAIN_INACCESSIBLE;

      default:
        // Checked by IsValid()
        DASSERT(false);
        return MAGMA_COHERENCY_DOMAIN_CPU;
    }
  }

  bool GetColorSpace(ColorSpace* color_space_out) override {
    if (!settings_.has_image_format_constraints) {
      return false;
    }
    // Only report first colorspace for now.
    if (settings_.image_format_constraints.color_spaces_count < 1)
      return false;
    switch (settings_.image_format_constraints.color_space[0].type) {
      case llcpp::fuchsia::sysmem::ColorSpaceType::REC601_NTSC:
        *color_space_out = kColorSpaceRec601Ntsc;
        return true;
      case llcpp::fuchsia::sysmem::ColorSpaceType::REC601_NTSC_FULL_RANGE:
        *color_space_out = kColorSpaceRec601NtscFullRange;
        return true;
      case llcpp::fuchsia::sysmem::ColorSpaceType::REC601_PAL:
        *color_space_out = kColorSpaceRec601Pal;
        return true;
      case llcpp::fuchsia::sysmem::ColorSpaceType::REC601_PAL_FULL_RANGE:
        *color_space_out = kColorSpaceRec601PalFullRange;
        return true;
      case llcpp::fuchsia::sysmem::ColorSpaceType::REC709:
        *color_space_out = kColorSpaceRec709;
        return true;
      case llcpp::fuchsia::sysmem::ColorSpaceType::REC2020:
        *color_space_out = kColorSpaceRec2020;
        return true;
      case llcpp::fuchsia::sysmem::ColorSpaceType::SRGB:
        *color_space_out = kColorSpaceSrgb;
        return true;
      default:
        return false;
    }
  }

  bool GetPlanes(uint64_t width, uint64_t height, magma_image_plane_t* planes_out) const override {
    if (!settings_.has_image_format_constraints) {
      return false;
    }

    for (uint32_t i = 0; i < MAGMA_MAX_IMAGE_PLANES; ++i) {
      planes_out[i].byte_offset = 0;
      planes_out[i].bytes_per_row = 0;
    }

    std::optional<llcpp::fuchsia::sysmem::ImageFormat_2> image_format =
        image_format::ConstraintsToFormat(settings_.image_format_constraints, width, height);
    if (!image_format) {
      return DRETF(false, "Image format not valid");
    }
    for (uint32_t plane = 0; plane < MAGMA_MAX_IMAGE_PLANES; ++plane) {
      uint64_t offset;
      bool plane_valid = image_format::GetPlaneByteOffset(*image_format, plane, &offset);
      if (!plane_valid) {
        planes_out[plane].byte_offset = 0;
      } else {
        planes_out[plane].byte_offset = offset;
      }
      uint32_t row_bytes;
      if (image_format::GetPlaneRowBytes(*image_format, plane, &row_bytes)) {
        planes_out[plane].bytes_per_row = row_bytes;
      } else {
        planes_out[plane].bytes_per_row = 0;
      }
    }
    return true;
  }

 private:
  uint32_t buffer_count_;
  llcpp::fuchsia::sysmem::SingleBufferSettings settings_;
};

class ZirconPlatformBufferConstraints : public PlatformBufferConstraints {
 public:
  virtual ~ZirconPlatformBufferConstraints() {}

  ZirconPlatformBufferConstraints(const magma_buffer_format_constraints_t* constraints) {
    constraints_.min_buffer_count = constraints->count;
    // Ignore input usage
    llcpp::fuchsia::sysmem::BufferUsage usage;
    usage.vulkan = llcpp::fuchsia::sysmem::vulkanUsageTransientAttachment |
                   llcpp::fuchsia::sysmem::vulkanUsageStencilAttachment |
                   llcpp::fuchsia::sysmem::vulkanUsageInputAttachment |
                   llcpp::fuchsia::sysmem::vulkanUsageColorAttachment |
                   llcpp::fuchsia::sysmem::vulkanUsageTransferSrc |
                   llcpp::fuchsia::sysmem::vulkanUsageTransferDst |
                   llcpp::fuchsia::sysmem::vulkanUsageStorage |
                   llcpp::fuchsia::sysmem::vulkanUsageSampled;
    constraints_.usage = usage;
    constraints_.has_buffer_memory_constraints = true;
    // No buffer constraints, except those passed directly through from the client. These two
    // are for whether this memory should be protected (e.g. usable for DRM content, the precise
    // definition depending on the system).
    constraints_.buffer_memory_constraints.secure_required = constraints->secure_required;
    // This must be true when secure_required is true.
    constraints_.buffer_memory_constraints.inaccessible_domain_supported =
        constraints->secure_permitted;

    constraints_.buffer_memory_constraints.ram_domain_supported = constraints->ram_domain_supported;
    constraints_.buffer_memory_constraints.cpu_domain_supported = constraints->cpu_domain_supported;
    constraints_.buffer_memory_constraints.min_size_bytes = constraints->min_size_bytes;
  }

  Status SetImageFormatConstraints(
      uint32_t index, const magma_image_format_constraints_t* format_constraints) override {
    if (index >= constraints_.image_format_constraints.size())
      return DRET(MAGMA_STATUS_INVALID_ARGS);
    if (index > constraints_.image_format_constraints_count)
      return DRET_MSG(MAGMA_STATUS_INVALID_ARGS, "Format constraint gaps not allowed");

    constraints_.image_format_constraints_count =
        std::max(constraints_.image_format_constraints_count, index + 1);
    auto& constraints = constraints_.image_format_constraints[index];
    // Initialize to default, since the array constructor initializes to 0
    // normally.
    constraints = llcpp::fuchsia::sysmem::ImageFormatConstraints();
    constraints.min_coded_width = 0u;
    constraints.max_coded_width = 16384;
    constraints.min_coded_height = 0u;
    constraints.max_coded_height = 16384;
    constraints.min_bytes_per_row = format_constraints->min_bytes_per_row;
    constraints.required_max_coded_width = format_constraints->width;
    constraints.required_max_coded_height = format_constraints->height;
    constraints.max_bytes_per_row =
        std::numeric_limits<decltype(constraints.max_bytes_per_row)>::max();

    bool is_yuv = false;
    switch (format_constraints->image_format) {
      case MAGMA_FORMAT_R8G8B8A8:
        constraints.pixel_format.type = llcpp::fuchsia::sysmem::PixelFormatType::R8G8B8A8;
        break;
      case MAGMA_FORMAT_BGRA32:
        constraints.pixel_format.type = llcpp::fuchsia::sysmem::PixelFormatType::BGRA32;
        break;
      case MAGMA_FORMAT_NV12:
        constraints.pixel_format.type = llcpp::fuchsia::sysmem::PixelFormatType::NV12;
        is_yuv = true;
        break;
      case MAGMA_FORMAT_I420:
        constraints.pixel_format.type = llcpp::fuchsia::sysmem::PixelFormatType::I420;
        is_yuv = true;
        break;
      default:
        return DRET_MSG(MAGMA_STATUS_INVALID_ARGS, "Invalid format: %d",
                        format_constraints->image_format);
    }
    if (is_yuv) {
      uint32_t color_space_count = 0;
      // This is the full list of formats currently supported by
      // VkSamplerYcbcrModelConversion and VkSamplerYcbcrRange as of vulkan 1.1,
      // restricted to 8-bit-per-component formats.
      constraints.color_space[color_space_count++].type =
          llcpp::fuchsia::sysmem::ColorSpaceType::REC601_NTSC;
      constraints.color_space[color_space_count++].type =
          llcpp::fuchsia::sysmem::ColorSpaceType::REC601_NTSC_FULL_RANGE;
      constraints.color_space[color_space_count++].type =
          llcpp::fuchsia::sysmem::ColorSpaceType::REC601_PAL;
      constraints.color_space[color_space_count++].type =
          llcpp::fuchsia::sysmem::ColorSpaceType::REC601_PAL_FULL_RANGE;
      constraints.color_space[color_space_count++].type =
          llcpp::fuchsia::sysmem::ColorSpaceType::REC709;
      constraints.color_spaces_count = color_space_count;
    } else {
      uint32_t color_space_count = 0;
      constraints.color_space[color_space_count++].type =
          llcpp::fuchsia::sysmem::ColorSpaceType::SRGB;
      constraints.color_spaces_count = color_space_count;
    }

    constraints.pixel_format.has_format_modifier = true;
    if (!format_constraints->has_format_modifier) {
      constraints.pixel_format.format_modifier.value =
          llcpp::fuchsia::sysmem::FORMAT_MODIFIER_LINEAR;
    } else {
      constraints.pixel_format.format_modifier.value = format_constraints->format_modifier;
    }
    constraints.layers = format_constraints->layers;
    constraints.bytes_per_row_divisor = format_constraints->bytes_per_row_divisor;
    return MAGMA_STATUS_OK;
  }
  llcpp::fuchsia::sysmem::BufferCollectionConstraints constraints() { return constraints_; }

 private:
  llcpp::fuchsia::sysmem::BufferCollectionConstraints constraints_ = {};
};

class ZirconPlatformBufferCollection : public PlatformBufferCollection {
 public:
  ~ZirconPlatformBufferCollection() override {
    if (collection_)
      collection_->Close();
  }

  Status Bind(llcpp::fuchsia::sysmem::Allocator::SyncClient& allocator, uint32_t token_handle) {
    DASSERT(!collection_);
    zx::channel h1;
    zx::channel h2;
    zx_status_t status = zx::channel::create(0, &h1, &h2);
    if (status != ZX_OK)
      return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "Failed to create channels: %d", status);

    status = allocator.BindSharedCollection(zx::channel(token_handle), std::move(h2)).status();
    if (status != ZX_OK)
      return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "Internal error: %d", status);

    collection_ =
        std::make_unique<llcpp::fuchsia::sysmem::BufferCollection::SyncClient>(std::move(h1));

    collection_->SetDebugClientInfo(
        fidl::unowned_str(magma::PlatformProcessHelper::GetCurrentProcessName()),
        magma::PlatformProcessHelper::GetCurrentProcessId());

    return MAGMA_STATUS_OK;
  }

  Status SetConstraints(PlatformBufferConstraints* constraints) override {
    auto llcpp_constraints =
        static_cast<ZirconPlatformBufferConstraints*>(constraints)->constraints();

    const char* buffer_name = llcpp_constraints.buffer_memory_constraints.secure_required
                                  ? "MagmaProtectedSysmemShared"
                                  : "MagmaUnprotectedSysmemShared";
    // These names are very generic, so set a low priority so it's easy to override them.
    constexpr uint32_t kVulkanPriority = 5;
    zx_status_t status =
        collection_->SetName(kVulkanPriority, fidl::unowned_str(buffer_name, strlen(buffer_name)))
            .status();
    if (status != ZX_OK) {
      return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "Error setting name: %d", status);
    }

    status = collection_->SetConstraints(true, std::move(llcpp_constraints)).status();
    if (status != ZX_OK) {
      return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "Error setting constraints: %d", status);
    }
    return MAGMA_STATUS_OK;
  }

  Status GetBufferDescription(
      std::unique_ptr<PlatformBufferDescription>* description_out) override {
    auto result = collection_->WaitForBuffersAllocated();
    if (result.status() != ZX_OK)
      return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "Failed wait for allocation: %d",
                      result.status());

    llcpp::fuchsia::sysmem::BufferCollection::WaitForBuffersAllocatedResponse* response =
        result.Unwrap();

    if (response->status != ZX_OK)
      return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "WaitForBuffersAllocated failed: %d",
                      response->status);

    // Buffer settings passed by value
    auto description = std::make_unique<ZirconPlatformBufferDescription>(
        response->buffer_collection_info.buffer_count, response->buffer_collection_info.settings);
    if (!description->IsValid())
      return DRET(MAGMA_STATUS_INTERNAL_ERROR);

    *description_out = std::move(description);
    return MAGMA_STATUS_OK;
  }

  Status GetBufferHandle(uint32_t index, uint32_t* handle_out, uint32_t* offset_out) override {
    auto result = collection_->WaitForBuffersAllocated();
    if (result.status() != ZX_OK)
      return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "Failed wait for allocation: %d",
                      result.status());

    llcpp::fuchsia::sysmem::BufferCollection::WaitForBuffersAllocatedResponse* response =
        result.Unwrap();

    if (response->status != ZX_OK)
      return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "WaitForBuffersAllocated failed: %d",
                      response->status);

    if (response->buffer_collection_info.buffer_count < index) {
      return DRET(MAGMA_STATUS_INVALID_ARGS);
    }

    *handle_out = response->buffer_collection_info.buffers[index].vmo.release();
    *offset_out = response->buffer_collection_info.buffers[index].vmo_usable_start;
    return MAGMA_STATUS_OK;
  }

 private:
  std::unique_ptr<llcpp::fuchsia::sysmem::BufferCollection::SyncClient> collection_;
};

class ZirconPlatformSysmemConnection : public PlatformSysmemConnection {
 public:
  ZirconPlatformSysmemConnection(llcpp::fuchsia::sysmem::Allocator::SyncClient allocator)
      : sysmem_allocator_(std::move(allocator)) {}

  magma_status_t AllocateBuffer(uint32_t flags, size_t size,
                                std::unique_ptr<magma::PlatformBuffer>* buffer_out) override {
    llcpp::fuchsia::sysmem::BufferUsage usage;
    usage.vulkan = llcpp::fuchsia::sysmem::vulkanUsageTransientAttachment |
                   llcpp::fuchsia::sysmem::vulkanUsageStencilAttachment |
                   llcpp::fuchsia::sysmem::vulkanUsageInputAttachment |
                   llcpp::fuchsia::sysmem::vulkanUsageColorAttachment |
                   llcpp::fuchsia::sysmem::vulkanUsageTransferSrc |
                   llcpp::fuchsia::sysmem::vulkanUsageTransferDst |
                   llcpp::fuchsia::sysmem::vulkanUsageStorage |
                   llcpp::fuchsia::sysmem::vulkanUsageSampled;
    if (flags & MAGMA_SYSMEM_FLAG_PROTECTED) {
      usage.video = llcpp::fuchsia::sysmem::videoUsageHwProtected;
    }
    if (flags & MAGMA_SYSMEM_FLAG_DISPLAY) {
      usage.display = llcpp::fuchsia::sysmem::displayUsageLayer;
    }

    llcpp::fuchsia::sysmem::BufferCollectionConstraints constraints;
    constraints.usage = usage;
    constraints.min_buffer_count_for_camping = 1;
    constraints.has_buffer_memory_constraints = true;
    constraints.buffer_memory_constraints.min_size_bytes = size;
    // It's always ok to support inaccessible domain, though this does imply that CPU access will
    // potentially not be possible.
    constraints.buffer_memory_constraints.inaccessible_domain_supported = true;
    if (flags & MAGMA_SYSMEM_FLAG_PROTECTED) {
      constraints.buffer_memory_constraints.secure_required = true;
      // This defaults to true so we have to set it to false, since it's not allowed to specify
      // secure_required and cpu_domain_supported at the same time.
      constraints.buffer_memory_constraints.cpu_domain_supported = false;
      // This must also be false if secure_required is true.
      DASSERT(!constraints.buffer_memory_constraints.ram_domain_supported);
    }
    constraints.image_format_constraints_count = 0;

    std::string buffer_name =
        (flags & MAGMA_SYSMEM_FLAG_PROTECTED) ? "MagmaProtectedSysmem" : "MagmaUnprotectedSysmem";
    if (flags & MAGMA_SYSMEM_FLAG_FOR_CLIENT) {
      // Signal that the memory was allocated for a vkAllocateMemory that the client asked for
      // directly.
      buffer_name += "ForClient";
    }
    llcpp::fuchsia::sysmem::BufferCollectionInfo_2 info;
    magma_status_t result = AllocateBufferCollection(constraints, buffer_name, &info);
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

  Status CreateBufferCollectionToken(uint32_t* handle_out) override {
    zx::channel h1;
    zx::channel h2;
    zx_status_t status = zx::channel::create(0, &h1, &h2);
    if (status != ZX_OK)
      return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "Failed to create channels: %d", status);

    auto result = sysmem_allocator_.AllocateSharedCollection(std::move(h2));
    if (result.status() != ZX_OK) {
      return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "AllocateSharedCollection failed: %d",
                      result.status());
    }

    *handle_out = h1.release();
    return MAGMA_STATUS_OK;
  }

  Status ImportBufferCollection(
      uint32_t handle, std::unique_ptr<PlatformBufferCollection>* collection_out) override {
    auto collection = std::make_unique<ZirconPlatformBufferCollection>();
    Status status = collection->Bind(sysmem_allocator_, handle);
    if (!status.ok()) {
      return DRET(status.get());
    }

    *collection_out = std::move(collection);
    return MAGMA_STATUS_OK;
  }

  Status CreateBufferConstraints(
      const magma_buffer_format_constraints_t* constraints,
      std::unique_ptr<PlatformBufferConstraints>* constraints_out) override {
    *constraints_out = std::make_unique<ZirconPlatformBufferConstraints>(constraints);
    return MAGMA_STATUS_OK;
  }

 private:
  magma_status_t AllocateBufferCollection(
      const llcpp::fuchsia::sysmem::BufferCollectionConstraints& constraints, std::string name,
      llcpp::fuchsia::sysmem::BufferCollectionInfo_2* info_out) {
    zx::channel h1;
    zx::channel h2;
    zx_status_t status = zx::channel::create(0, &h1, &h2);
    if (status != ZX_OK)
      return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "Failed to create channels: %d", status);

    status = sysmem_allocator_.AllocateNonSharedCollection(std::move(h2)).status();
    if (status != ZX_OK)
      return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "Failed to allocate buffer: %d", status);

    llcpp::fuchsia::sysmem::BufferCollection::SyncClient collection(std::move(h1));

    collection.SetDebugClientInfo(
        fidl::unowned_str(magma::PlatformProcessHelper::GetCurrentProcessName()),
        magma::PlatformProcessHelper::GetCurrentProcessId());

    if (!name.empty()) {
      collection.SetName(10, fidl::unowned_str(name));
    }
    status = collection.SetConstraints(true, std::move(constraints)).status();
    if (status != ZX_OK)
      return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "Failed to set constraints: %d", status);

    auto result = collection.WaitForBuffersAllocated();

    // Ignore failure - this just prevents unnecessary logged errors.
    collection.Close();

    if (result.status() != ZX_OK)
      return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "Failed wait for allocation: %d",
                      result.status());

    llcpp::fuchsia::sysmem::BufferCollection::WaitForBuffersAllocatedResponse* response =
        result.Unwrap();

    if (response->status != ZX_OK)
      return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "Failed wait for allocation: %d",
                      response->status);

    *info_out = std::move(response->buffer_collection_info);
    return MAGMA_STATUS_OK;
  }

  llcpp::fuchsia::sysmem::Allocator::SyncClient sysmem_allocator_;
};

// static
std::unique_ptr<PlatformSysmemConnection> PlatformSysmemConnection::Import(uint32_t handle) {
  llcpp::fuchsia::sysmem::Allocator::SyncClient sysmem_allocator((zx::channel(handle)));
  return std::make_unique<ZirconPlatformSysmemConnection>(std::move(sysmem_allocator));
}

// static
magma_status_t PlatformSysmemConnection::DecodeBufferDescription(
    const uint8_t* image_data, uint64_t image_data_size,
    std::unique_ptr<PlatformBufferDescription>* buffer_description_out) {
  std::vector<uint8_t> copy_message(image_data, image_data + image_data_size);
  const char* error = nullptr;
  static_assert(llcpp::fuchsia::sysmem::SingleBufferSettings::MaxNumHandles == 0,
                "Can't decode a buffer with handles");
  zx_status_t status = fidl_decode(llcpp::fuchsia::sysmem::SingleBufferSettings::Type,
                                   copy_message.data(), copy_message.size(), nullptr, 0, &error);

  if (status != ZX_OK)
    return DRET_MSG(MAGMA_STATUS_INVALID_ARGS, "Invalid SingleBufferSettings: %d %s", status,
                    error);

  const auto& buffer_settings =
      *reinterpret_cast<const llcpp::fuchsia::sysmem::SingleBufferSettings*>(copy_message.data());

  if (!buffer_settings.has_image_format_constraints) {
    return DRET_MSG(MAGMA_STATUS_INVALID_ARGS, "Buffer is not image");
  }

  // Buffer settings passed by value
  auto description = std::make_unique<ZirconPlatformBufferDescription>(1u, buffer_settings);
  if (!description->IsValid())
    return DRET(MAGMA_STATUS_INTERNAL_ERROR);

  *buffer_description_out = std::move(description);
  return MAGMA_STATUS_OK;
}

}  // namespace magma_sysmem
