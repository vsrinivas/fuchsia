// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/flatland/renderer/vk_renderer.h"

#include <fuchsia/sysmem/cpp/fidl.h>
#include <lib/async/cpp/wait.h>
#include <lib/async/default.h>
#include <zircon/errors.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include "src/ui/lib/escher/escher.h"
#include "src/ui/lib/escher/flatland/rectangle_compositor.h"
#include "src/ui/lib/escher/forward_declarations.h"
#include "src/ui/lib/escher/impl/naive_image.h"
#include "src/ui/lib/escher/impl/vulkan_utils.h"
#include "src/ui/lib/escher/renderer/batch_gpu_uploader.h"
#include "src/ui/lib/escher/renderer/render_funcs.h"
#include "src/ui/lib/escher/renderer/sampler_cache.h"
#include "src/ui/lib/escher/resources/resource_recycler.h"
#include "src/ui/lib/escher/third_party/granite/vk/command_buffer.h"
#include "src/ui/lib/escher/util/fuchsia_utils.h"
#include "src/ui/lib/escher/util/image_utils.h"
#include "src/ui/lib/escher/util/trace_macros.h"

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <vulkan/vulkan.hpp>

namespace {

using allocation::BufferCollectionUsage;

// Highest priority format first.
const std::vector<vk::Format> kPreferredImageFormats = {
    vk::Format::eR8G8B8A8Srgb, vk::Format::eB8G8R8A8Srgb, vk::Format::eG8B8R83Plane420Unorm,
    vk::Format::eG8B8R82Plane420Unorm};

const vk::Filter kDefaultFilter = vk::Filter::eNearest;

// Black color to replace protected content when we aren't in protected mode, i.e. Screenshots.
const glm::vec4 kProtectedReplacementColorInRGBA = glm::vec4(0, 0, 0, 1);

// Returns the corresponding Vulkan image format to use given the provided
// Zircon image format.
// TODO(fxbug.dev/71410): Remove all references to zx_pixel_format_t.
static vk::Format ConvertToVkFormat(zx_pixel_format_t pixel_format) {
  switch (pixel_format) {
    // These two Zircon formats correspond to the Sysmem BGRA32 format.
    case ZX_PIXEL_FORMAT_RGB_x888:
    case ZX_PIXEL_FORMAT_ARGB_8888:
      return vk::Format::eB8G8R8A8Srgb;
    // These two Zircon formats correspond to the Sysmem R8G8B8A8 format.
    case ZX_PIXEL_FORMAT_BGR_888x:
    case ZX_PIXEL_FORMAT_ABGR_8888:
      return vk::Format::eR8G8B8A8Srgb;
    case ZX_PIXEL_FORMAT_NV12:
      return vk::Format::eG8B8R82Plane420Unorm;
  }
  FX_CHECK(false) << "Unsupported Zircon pixel format: " << pixel_format;
  return vk::Format::eUndefined;
}

// Create a default 1x1 texture for solid color renderables which are not associated
// with an image.
static escher::TexturePtr CreateWhiteTexture(escher::Escher* escher,
                                             escher::BatchGpuUploader* gpu_uploader) {
  FX_DCHECK(escher);
  uint8_t channels[4];
  channels[0] = channels[1] = channels[2] = channels[3] = 255;
  auto image = escher->NewRgbaImage(gpu_uploader, 1, 1, channels);
  return escher->NewTexture(std::move(image), vk::Filter::eNearest);
}

static escher::TexturePtr CreateDepthTexture(escher::Escher* escher,
                                             const escher::ImagePtr& output_image) {
  escher::TexturePtr depth_buffer;
  escher::RenderFuncs::ObtainDepthTexture(
      escher, output_image->use_protected_memory(), output_image->info(),
      escher->device()->caps().GetMatchingDepthStencilFormat().value, depth_buffer);
  return depth_buffer;
}

constexpr float clamp(float v, float lo, float hi) { return (v < lo) ? lo : (hi < v) ? hi : v; }

static std::vector<escher::Rectangle2D> GetNormalizedUvRects(
    const std::vector<flatland::ImageRect>& rectangles,
    const std::vector<flatland::ImageMetadata>& images) {
  FX_DCHECK(rectangles.size() == images.size());

  std::vector<escher::Rectangle2D> normalized_rects;

  for (unsigned int i = 0; i < rectangles.size(); i++) {
    const auto& rect = rectangles[i];
    const auto& image = images[i];
    const auto& texel_uvs = rectangles[i].texel_uvs;
    const auto& orientation = rectangles[i].orientation;
    float w = image.width;
    float h = image.height;
    FX_DCHECK(w >= 0.f && h >= 0.f);

    // Reorder and normalize the texel UVs. Normalization is based on the width and height of the
    // image that is sampled from. Reordering is based on orientation. The texel UVs are listed
    // in clockwise-order starting at the top-left corner of the texture. They need to be reordered
    // so that they are listed in clockwise-order and the UV that maps to the top-left corner of the
    // escher::Rectangle2D is listed first. For instance, if the rectangle is rotated 90_CCW, the
    // first texel UV of the ImageRect, at index 0, is at index 3 in the escher::Rectangle2D.
    std::array<glm::vec2, 4> normalized_uvs;
    // |fuchsia::ui::composition::Orientation| is an enum value in the range [1, 4].
    int starting_index = static_cast<int>(orientation) - 1;
    for (int j = 0; j < 4; j++) {
      const int index = (starting_index + j) % 4;
      // Clamp values to ensure they are normalized to the range [0, 1].
      normalized_uvs[j] =
          glm::vec2(clamp(texel_uvs[index].x, 0, w) / w, clamp(texel_uvs[index].y, 0, h) / h);
    }

    normalized_rects.push_back({rect.origin, rect.extent, normalized_uvs});
  }

  return normalized_rects;
}

}  // anonymous namespace

namespace flatland {

VkRenderer::VkRenderer(escher::EscherWeakPtr escher)
    : escher_(std::move(escher)), compositor_(escher::RectangleCompositor(escher_)) {
  auto gpu_uploader = escher::BatchGpuUploader::New(escher_, /*frame_trace_number*/ 0);
  FX_DCHECK(gpu_uploader);

  texture_map_[allocation::kInvalidImageId] = CreateWhiteTexture(escher_.get(), gpu_uploader.get());
  gpu_uploader->Submit();

  {
    TRACE_DURATION("gfx", "VkRenderer::Initialize");
    WaitIdle();
  }
}

VkRenderer::~VkRenderer() {
  auto vk_device = escher_->vk_device();
  auto vk_loader = escher_->device()->dispatch_loader();
  for (auto& [_, collection] : texture_collections_) {
    vk_device.destroyBufferCollectionFUCHSIA(collection.vk_collection, nullptr, vk_loader);
  }
  for (auto& [_, collection] : render_target_collections_) {
    vk_device.destroyBufferCollectionFUCHSIA(collection.vk_collection, nullptr, vk_loader);
  }
  render_target_collections_.clear();
  for (auto& [_, collection] : readback_collections_) {
    vk_device.destroyBufferCollectionFUCHSIA(collection.vk_collection, nullptr, vk_loader);
  }
  readback_collections_.clear();
}

bool VkRenderer::ImportBufferCollection(
    GlobalBufferCollectionId collection_id, fuchsia::sysmem::Allocator_Sync* sysmem_allocator,
    fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token,
    BufferCollectionUsage usage, std::optional<fuchsia::math::SizeU> size) {
  TRACE_DURATION("gfx", "flatland::VkRenderer::ImportBufferCollection");

  auto vk_device = escher_->vk_device();
  auto vk_loader = escher_->device()->dispatch_loader();
  FX_DCHECK(vk_device);
  FX_DCHECK(collection_id != allocation::kInvalidId);

  // Check for a null token here before we try to duplicate it to get the
  // vulkan token.
  if (!token.is_valid()) {
    FX_LOGS(WARNING) << "Token is invalid.";
    return false;
  }

  // Bind the buffer collection token to get the local token. Valid tokens can always be bound.
  fuchsia::sysmem::BufferCollectionTokenSyncPtr local_token = token.BindSync();
  fuchsia::sysmem::BufferCollectionTokenSyncPtr vulkan_token;
  // TODO(fxbug.dev/51213): See if this can become asynchronous.
  zx_status_t status =
      local_token->Duplicate(std::numeric_limits<uint32_t>::max(), vulkan_token.NewRequest());
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Cannot duplicate token: " << zx_status_get_string(status)
                   << "; The client may have invalidated the token.";
    return false;
  }

  // Create the sysmem collection.
  fuchsia::sysmem::BufferCollectionSyncPtr buffer_collection;
  vk::ImageUsageFlags image_usage;
  {
    // Use local token to create a BufferCollection and then sync. We can trust
    // |buffer_collection->Sync()| to tell us if we have a bad or malicious channel. So if this call
    // passes, then we know we have a valid BufferCollection.
    zx_status_t status = sysmem_allocator->BindSharedCollection(std::move(local_token),
                                                                buffer_collection.NewRequest());
    if (status != ZX_OK) {
      FX_LOGS(ERROR) << "Could not bind buffer collection: " << zx_status_get_string(status);
      return false;
    }

    status = buffer_collection->Sync();
    if (status != ZX_OK) {
      FX_LOGS(ERROR) << "Could not sync buffer collection: " << zx_status_get_string(status);
      return false;
    }

    // Use a name with a priority that's greater than the vulkan implementation, but less than
    // what any client would use.
    const char* image_name;
    switch (usage) {
      case BufferCollectionUsage::kRenderTarget:
        image_name = "FlatlandRenderTargetMemory";
        image_usage = escher::RectangleCompositor::kRenderTargetUsageFlags |
                      vk::ImageUsageFlagBits::eTransferSrc;
        break;
      case BufferCollectionUsage::kReadback:
        image_name = "FlatlandReadbackMemory";
        image_usage = vk::ImageUsageFlagBits::eTransferDst;
        break;
      case BufferCollectionUsage::kClientImage:
        image_usage = escher::RectangleCompositor::kTextureUsageFlags;
        image_name = "FlatlandImageMemory";
        break;
    }

    buffer_collection->SetName(10u, image_name);
    status = buffer_collection->SetConstraints(false /* has_constraints */,
                                               fuchsia::sysmem::BufferCollectionConstraints());
    if (status != ZX_OK) {
      FX_LOGS(ERROR) << "Cannot set constraints for " << image_name << ": "
                     << zx_status_get_string(status)
                     << "; The client may have invalidated the token.";
      return false;
    }
  }

  // Create the vk collection.
  vk::BufferCollectionFUCHSIA collection;
  {
    std::vector<vk::ImageFormatConstraintsInfoFUCHSIA> create_infos;
    for (const auto& format : kPreferredImageFormats) {
      vk::ImageCreateInfo create_info =
          escher::RectangleCompositor::GetDefaultImageConstraints(format, image_usage);
      if (size.has_value() && size.value().width && size.value().height) {
        create_info.extent = vk::Extent3D{size.value().width, size.value().height, 1};
      }

      create_infos.push_back(escher::GetDefaultImageFormatConstraintsInfo(create_info));
    }

    vk::ImageConstraintsInfoFUCHSIA image_constraints_info;
    image_constraints_info.setFormatConstraints(create_infos)
        .setFlags(escher_->allow_protected_memory()
                      ? vk::ImageConstraintsInfoFlagBitsFUCHSIA::eProtectedOptional
                      : vk::ImageConstraintsInfoFlagsFUCHSIA{})
        .setBufferCollectionConstraints(
            vk::BufferCollectionConstraintsInfoFUCHSIA().setMinBufferCount(1u));

    // Create the collection and set its constraints.
    vk::BufferCollectionCreateInfoFUCHSIA buffer_collection_create_info;
    buffer_collection_create_info.collectionToken = vulkan_token.Unbind().TakeChannel().release();
    collection = escher::ESCHER_CHECKED_VK_RESULT(
        vk_device.createBufferCollectionFUCHSIA(buffer_collection_create_info, nullptr, vk_loader));
    auto vk_result = vk_device.setBufferCollectionImageConstraintsFUCHSIA(
        collection, image_constraints_info, vk_loader);
    if (vk_result != vk::Result::eSuccess) {
      FX_LOGS(ERROR) << "Cannot set vulkan constraints: " << vk::to_string(vk_result)
                     << "; The client may have invalidated the token.";
      return false;
    }
  }

  // Multiple threads may be attempting to read/write from |collections_|
  // so we lock this function here.
  // TODO(fxbug.dev/44335): Convert this to a lock-free structure.
  std::unique_lock<std::mutex> lock(mutex_);
  std::unordered_map<GlobalBufferCollectionId, CollectionData>* collections =
      UsageToCollection(usage);

  auto [_, emplace_success] = collections->emplace(
      std::make_pair(collection_id, CollectionData{.collection = std::move(buffer_collection),
                                                   .vk_collection = std::move(collection)}));
  if (!emplace_success) {
    FX_LOGS(WARNING) << "Could not store buffer collection, because an entry already existed for "
                     << collection_id;
    return false;
  }

  return true;
}

void VkRenderer::ReleaseBufferCollection(GlobalBufferCollectionId collection_id,
                                         BufferCollectionUsage usage) {
  TRACE_DURATION("gfx", "flatland::VkRenderer::ReleaseBufferCollection");
  // Multiple threads may be attempting to read/write from the various maps,
  // lock this function here.
  // TODO(fxbug.dev/44335): Convert this to a lock-free structure.
  std::unique_lock<std::mutex> lock(mutex_);
  std::unordered_map<GlobalBufferCollectionId, CollectionData>* collections =
      UsageToCollection(usage);
  auto collection_itr = collections->find(collection_id);

  // If the collection is not in the map, then there's nothing to do.
  if (collection_itr == collections->end()) {
    FX_LOGS(WARNING) << "Attempting to release a non-existent buffer collection.";
    return;
  }

  auto vk_device = escher_->vk_device();
  auto vk_loader = escher_->device()->dispatch_loader();
  vk_device.destroyBufferCollectionFUCHSIA(collection_itr->second.vk_collection, nullptr,
                                           vk_loader);

  zx_status_t status = collection_itr->second.collection->Close();
  // AttachToken failure causes ZX_ERR_PEER_CLOSED.
  if (status != ZX_OK && status != ZX_ERR_PEER_CLOSED) {
    FX_LOGS(ERROR) << "Error when closing buffer collection: " << zx_status_get_string(status);
  }

  collections->erase(collection_id);
}

bool VkRenderer::ImportBufferImage(const allocation::ImageMetadata& metadata,
                                   BufferCollectionUsage usage) {
  TRACE_DURATION("gfx", "flatland::VkRenderer::ImportBufferImage");

  std::unique_lock<std::mutex> lock(mutex_);

  // The metadata can't have an invalid collection id.
  if (metadata.collection_id == allocation::kInvalidId) {
    FX_LOGS(WARNING) << "Image has invalid collection id.";
    return false;
  }

  // The metadata can't have an invalid identifier.
  if (metadata.identifier == allocation::kInvalidImageId) {
    FX_LOGS(WARNING) << "Image has invalid identifier.";
    return false;
  }

  // Check we have valid dimensions.
  if (metadata.width == 0 || metadata.height == 0) {
    FX_LOGS(WARNING) << "Image has invalid dimensions: "
                     << "(" << metadata.width << ", " << metadata.height << ").";
    return false;
  }

  // Make sure that the collection that will back this image's memory
  // is actually registered with the renderer.
  std::unordered_map<GlobalBufferCollectionId, CollectionData>* collections =
      UsageToCollection(usage);
  auto collection_itr = collections->find(metadata.collection_id);
  if (collection_itr == collections->end()) {
    FX_LOGS(WARNING) << "Collection with id " << metadata.collection_id << " does not exist.";
    return false;
  }

  // Check to see if the buffers are allocated and return false if not.
  zx_status_t allocation_status = ZX_OK;
  zx_status_t status = collection_itr->second.collection->CheckBuffersAllocated(&allocation_status);
  if (status != ZX_OK) {
    FX_LOGS(WARNING) << "Collection was not allocated (FIDL status: "
                     << zx_status_get_string(status) << ").";
  } else if (allocation_status != ZX_OK) {
    FX_LOGS(WARNING) << "Collection was not allocated (allocation status: "
                     << zx_status_get_string(status) << ").";
    return false;
  }

  // Make sure we're not reusing the same image identifier for the specific usage..
  switch (usage) {
    case BufferCollectionUsage::kRenderTarget:
      if (render_target_map_.find(metadata.identifier) != render_target_map_.end()) {
        FX_LOGS(WARNING) << "An image with this identifier already exists.";
        return false;
      }
      break;
    case BufferCollectionUsage::kReadback:
      if (readback_image_map_.find(metadata.identifier) != readback_image_map_.end()) {
        FX_LOGS(WARNING) << "An image with this identifier already exists.";
        return false;
      }
      break;
    default:
      if (texture_map_.find(metadata.identifier) != texture_map_.end()) {
        FX_LOGS(WARNING) << "An image with this identifier already exists.";
        return false;
      }
      break;
  }

  switch (usage) {
    case BufferCollectionUsage::kRenderTarget: {
      auto readback_collection_itr = readback_collections_.find(metadata.collection_id);
      const bool needs_readback = readback_collection_itr != readback_collections_.end();

      const vk::ImageUsageFlags kRenderTargetAsReadbackSourceUsageFlags =
          escher::RectangleCompositor::kRenderTargetUsageFlags |
          vk::ImageUsageFlagBits::eTransferSrc;
      auto image =
          ExtractImage(metadata, collection_itr->second.vk_collection,
                       needs_readback ? kRenderTargetAsReadbackSourceUsageFlags
                                      : escher::RectangleCompositor::kRenderTargetUsageFlags);
      if (!image) {
        FX_LOGS(ERROR) << "Could not extract render target.";
        return false;
      }

      image->set_swapchain_layout(vk::ImageLayout::eColorAttachmentOptimal);
      render_target_map_[metadata.identifier] = image;
      depth_target_map_[metadata.identifier] = CreateDepthTexture(escher_.get(), image);
      pending_render_targets_.insert(metadata.identifier);
      break;
    }
    case BufferCollectionUsage::kReadback: {
      auto readback_collection_itr = readback_collections_.find(metadata.collection_id);
      // Check to see if the buffers are allocated and return false if not.
      zx_status_t allocation_status = ZX_OK;
      zx_status_t status =
          readback_collection_itr->second.collection->CheckBuffersAllocated(&allocation_status);
      if (status != ZX_OK || allocation_status != ZX_OK) {
        FX_LOGS(ERROR) << "Readback collection was not allocated: " << zx_status_get_string(status)
                       << " ;alloc: " << zx_status_get_string(allocation_status);
        return false;
      }

      escher::ImagePtr readback_image =
          ExtractImage(metadata, readback_collection_itr->second.vk_collection,
                       vk::ImageUsageFlagBits::eTransferDst);
      if (!readback_image) {
        FX_LOGS(ERROR) << "Could not extract readback image.";
        return false;
      }
      readback_image_map_[metadata.identifier] = readback_image;
      break;
    }
    case BufferCollectionUsage::kClientImage: {
      auto texture = ExtractTexture(metadata, collection_itr->second.vk_collection);
      if (!texture) {
        FX_LOGS(ERROR) << "Could not extract client texture image.";
        return false;
      }
      texture_map_[metadata.identifier] = texture;
      pending_textures_.insert(metadata.identifier);
      break;
    }
  }
  return true;
}

void VkRenderer::ReleaseBufferImage(allocation::GlobalImageId image_id) {
  TRACE_DURATION("gfx", "flatland::VkRenderer::ReleaseBufferImage");

  std::unique_lock<std::mutex> lock(mutex_);

  if (texture_map_.find(image_id) != texture_map_.end()) {
    texture_map_.erase(image_id);
    pending_textures_.erase(image_id);
  } else if (render_target_map_.find(image_id) != render_target_map_.end()) {
    render_target_map_.erase(image_id);
    depth_target_map_.erase(image_id);
    readback_image_map_.erase(image_id);
    pending_render_targets_.erase(image_id);
  }
}

escher::ImagePtr VkRenderer::ExtractImage(const allocation::ImageMetadata& metadata,
                                          vk::BufferCollectionFUCHSIA collection,
                                          vk::ImageUsageFlags usage, bool readback) {
  TRACE_DURATION("gfx", "VkRenderer::ExtractImage");
  auto vk_device = escher_->vk_device();
  auto vk_loader = escher_->device()->dispatch_loader();

  // Grab the collection Properties from Vulkan.
  // TODO(fxbug.dev/102299): Add unittests to cover the case where sysmem client
  // token gets invalidated when importing images.
  auto properties_rv = vk_device.getBufferCollectionPropertiesFUCHSIA(collection, vk_loader);
  if (properties_rv.result != vk::Result::eSuccess) {
    FX_LOGS(WARNING) << "Cannot get buffer collection properties; the token may "
                        "have been invalidated.";
    return nullptr;
  }
  auto properties = properties_rv.value;

  // Check the provided index against actually allocated number of buffers.
  if (properties.bufferCount <= metadata.vmo_index) {
    FX_LOGS(ERROR) << "Specified vmo index is out of bounds: " << metadata.vmo_index;
    return nullptr;
  }

  // Check if allocated buffers are backed by protected memory.
  const bool is_protected =
      (escher_->vk_physical_device()
           .getMemoryProperties()
           .memoryTypes[escher::CountTrailingZeros(properties.memoryTypeBits)]
           .propertyFlags &
       vk::MemoryPropertyFlagBits::eProtected) == vk::MemoryPropertyFlagBits::eProtected;

  // Setup the create info Fuchsia extension.
  vk::BufferCollectionImageCreateInfoFUCHSIA collection_image_info;
  collection_image_info.collection = collection;
  collection_image_info.index = metadata.vmo_index;

  // Setup the create info.
  FX_DCHECK(properties.createInfoIndex < std::size(kPreferredImageFormats));
  auto pixel_format = kPreferredImageFormats[properties.createInfoIndex];
  vk::ImageCreateInfo create_info =
      escher::RectangleCompositor::GetDefaultImageConstraints(pixel_format, usage);
  create_info.extent = vk::Extent3D{metadata.width, metadata.height, 1};
  create_info.setPNext(&collection_image_info);
  if (is_protected) {
    create_info.flags = vk::ImageCreateFlagBits::eProtected;
  }

  // Create the VK Image, return nullptr if this fails.
  auto image_result = vk_device.createImage(create_info);
  if (image_result.result != vk::Result::eSuccess) {
    FX_LOGS(ERROR) << "VkCreateImage failed: " << vk::to_string(image_result.result);
    return nullptr;
  }

  // Now we have to allocate VK memory for the image. This memory is going to come from
  // the imported buffer collection's vmo.
  auto memory_requirements = vk_device.getImageMemoryRequirements(image_result.value);
  uint32_t memory_type_index =
      escher::CountTrailingZeros(memory_requirements.memoryTypeBits & properties.memoryTypeBits);
  vk::StructureChain<vk::MemoryAllocateInfo, vk::ImportMemoryBufferCollectionFUCHSIA,
                     vk::MemoryDedicatedAllocateInfoKHR>
      alloc_info(vk::MemoryAllocateInfo()
                     .setAllocationSize(memory_requirements.size)
                     .setMemoryTypeIndex(memory_type_index),
                 vk::ImportMemoryBufferCollectionFUCHSIA()
                     .setCollection(collection)
                     .setIndex(metadata.vmo_index),
                 vk::MemoryDedicatedAllocateInfoKHR().setImage(image_result.value));
  vk::DeviceMemory memory = nullptr;
  vk::Result err =
      vk_device.allocateMemory(&alloc_info.get<vk::MemoryAllocateInfo>(), nullptr, &memory);
  if (err != vk::Result::eSuccess) {
    FX_LOGS(ERROR) << "Could not successfully allocate memory.";
    return nullptr;
  }

  // Have escher manager the memory since this is the required format for creating
  // an Escher image. Also we can now check if the total memory size is great enough
  // for the image memory requirements. If it's not big enough, the client likely
  // requested an image size that is larger than the maximum image size allowed by
  // the sysmem collection constraints.
  auto gpu_mem =
      escher::GpuMem::AdoptVkMemory(vk_device, vk::DeviceMemory(memory), memory_requirements.size,
                                    /*needs_mapped_ptr*/ false);
  if (memory_requirements.size > gpu_mem->size()) {
    FX_LOGS(ERROR) << "Memory requirements for image exceed available memory: "
                   << memory_requirements.size << " " << gpu_mem->size();
    return nullptr;
  }

  // Create and return an escher image.
  escher::ImageInfo escher_image_info;
  escher_image_info.format = create_info.format;
  escher_image_info.width = create_info.extent.width;
  escher_image_info.height = create_info.extent.height;
  escher_image_info.usage = create_info.usage;
  escher_image_info.memory_flags = readback ? vk::MemoryPropertyFlagBits::eHostCoherent
                                            : vk::MemoryPropertyFlagBits::eDeviceLocal;
  if (create_info.flags & vk::ImageCreateFlagBits::eProtected) {
    escher_image_info.memory_flags = vk::MemoryPropertyFlagBits::eProtected;
  }
  escher_image_info.is_external = true;
  escher_image_info.color_space = escher::FromSysmemColorSpace(
      static_cast<fuchsia::sysmem::ColorSpaceType>(properties.sysmemColorSpaceIndex.colorSpace));
  return escher::impl::NaiveImage::AdoptVkImage(escher_->resource_recycler(), escher_image_info,
                                                image_result.value, std::move(gpu_mem),
                                                create_info.initialLayout);
}

escher::TexturePtr VkRenderer::ExtractTexture(const allocation::ImageMetadata& metadata,
                                              vk::BufferCollectionFUCHSIA collection) {
  auto image = ExtractImage(metadata, collection, escher::RectangleCompositor::kTextureUsageFlags);
  if (!image) {
    FX_LOGS(ERROR) << "Image for texture was nullptr.";
    return nullptr;
  }

  escher::SamplerPtr sampler = escher::image_utils::IsYuvFormat(image->format())
                                   ? escher_->sampler_cache()->ObtainYuvSampler(
                                         image->format(), kDefaultFilter, image->color_space())
                                   : escher_->sampler_cache()->ObtainSampler(kDefaultFilter);
  FX_DCHECK(escher::image_utils::IsYuvFormat(image->format()) ? sampler->is_immutable()
                                                              : !sampler->is_immutable());
  auto texture = fxl::MakeRefCounted<escher::Texture>(escher_->resource_recycler(), sampler, image);
  return texture;
}

void VkRenderer::Render(const ImageMetadata& render_target,
                        const std::vector<ImageRect>& rectangles,
                        const std::vector<ImageMetadata>& images,
                        const std::vector<zx::event>& release_fences, bool apply_color_conversion) {
  TRACE_DURATION("gfx", "VkRenderer::Render");

  FX_DCHECK(rectangles.size() == images.size())
      << "# rects: " << rectangles.size() << " and #images: " << images.size();

  // Copy over the texture and render target data to local containers that do not need
  // to be accessed via a lock. We're just doing a shallow copy via the copy assignment
  // operator since the texture and render target data is just referenced through pointers.
  // We manually unlock the lock after copying over the data.
  std::unique_lock<std::mutex> lock(mutex_);
  const auto local_texture_map = texture_map_;
  const auto local_render_target_map = render_target_map_;
  const auto local_depth_target_map = depth_target_map_;
  const auto local_readback_image_map = readback_image_map_;

  // After moving, the original containers are emptied.
  const auto local_pending_textures = std::move(pending_textures_);
  const auto local_pending_render_targets = std::move(pending_render_targets_);
  pending_textures_.clear();
  pending_render_targets_.clear();
  lock.unlock();

  // If the |render_target| is protected, we should switch to a protected escher::Frame. Otherwise,
  // we should ensure that there is no protected content in |images|.
  FX_DCHECK(local_render_target_map.find(render_target.identifier) !=
            local_render_target_map.end());
  const bool render_in_protected_mode =
      local_render_target_map.at(render_target.identifier)->use_protected_memory();

  // Escher's frame class acts as a command buffer manager that we use to create a
  // command buffer and submit it to the device queue once we are done.
  auto frame = escher_->NewFrame(
      "flatland::VkRenderer", ++frame_number_, /*enable_gpu_logging=*/false,
      /*requested_type=*/escher::CommandBuffer::Type::kGraphics, render_in_protected_mode);
  auto command_buffer = frame->cmds();

  // Transition pending images to their correct layout
  // TODO(fxbug.dev/52196): The way we are transitioning image layouts here and in the rest of
  // scenic is incorrect for "external" images. It just happens to be working by luck on our current
  // hardware.
  for (auto texture_id : local_pending_textures) {
    FX_DCHECK(local_texture_map.find(texture_id) != local_texture_map.end());
    const auto texture = local_texture_map.at(texture_id);
    command_buffer->impl()->TransitionImageLayout(texture->image(), vk::ImageLayout::eUndefined,
                                                  vk::ImageLayout::eShaderReadOnlyOptimal);
  }
  for (auto target_id : local_pending_render_targets) {
    FX_DCHECK(local_render_target_map.find(target_id) != local_render_target_map.end());
    const auto target = local_render_target_map.at(target_id);
    command_buffer->impl()->TransitionImageLayout(
        target, vk::ImageLayout::eUndefined, vk::ImageLayout::eColorAttachmentOptimal,
        VK_QUEUE_FAMILY_FOREIGN_EXT, escher_->device()->vk_main_queue_family());
  }

  std::vector<const escher::TexturePtr> textures;
  std::vector<escher::RectangleCompositor::ColorData> color_data;
  for (const auto& image : images) {
    auto texture_ptr = local_texture_map.at(image.identifier);

    // When we are not in protected mode, replace any protected content with black solid color.
    if (!render_in_protected_mode && texture_ptr->image()->use_protected_memory()) {
      textures.emplace_back(local_texture_map.at(allocation::kInvalidImageId));
      color_data.emplace_back(escher::RectangleCompositor::ColorData(
          kProtectedReplacementColorInRGBA, /*is_opaque=*/true));
      continue;
    }

    // Pass the texture into the above vector to keep it alive outside of this loop.
    textures.emplace_back(texture_ptr);

    glm::vec4 multiply(image.multiply_color[0], image.multiply_color[1], image.multiply_color[2],
                       image.multiply_color[3]);
    color_data.emplace_back(escher::RectangleCompositor::ColorData(
        multiply, image.blend_mode == fuchsia::ui::composition::BlendMode::SRC));
  }

  // Grab the output image and use it to generate a depth texture. The depth texture needs to
  // be the same width and height as the output image.
  const auto output_image = local_render_target_map.at(render_target.identifier);
  const auto depth_texture = local_depth_target_map.at(render_target.identifier);

  // Transition to eColorAttachmentOptimal for rendering.  Note the src queue family is FOREIGN,
  // since we assume that this image was previously presented to the display controller.
  auto render_image_layout = vk::ImageLayout::eColorAttachmentOptimal;
  command_buffer->impl()->TransitionImageLayout(output_image, vk::ImageLayout::eUndefined,
                                                render_image_layout, VK_QUEUE_FAMILY_FOREIGN_EXT,
                                                escher_->device()->vk_main_queue_family());

  const auto normalized_rects = GetNormalizedUvRects(rectangles, images);

  // Now the compositor can finally draw.
  compositor_.DrawBatch(command_buffer, normalized_rects, textures, color_data, output_image,
                        depth_texture, apply_color_conversion);

  const auto readback_image_it = local_readback_image_map.find(render_target.identifier);
  // Copy to the readback image if there is a readback image.
  if (readback_image_it != local_readback_image_map.end()) {
    BlitRenderTarget(command_buffer, output_image, &render_image_layout, readback_image_it->second,
                     render_target);
  }

  // Having drawn, we transition to eGeneral on the FOREIGN target queue, so that we can present the
  // the image to the display controller.
  command_buffer->impl()->TransitionImageLayout(
      output_image, render_image_layout, vk::ImageLayout::eGeneral,
      escher_->device()->vk_main_queue_family(), VK_QUEUE_FAMILY_FOREIGN_EXT);

  // Create vk::semaphores from the zx::events.
  std::vector<escher::SemaphorePtr> semaphores;
  for (auto& fence_original : release_fences) {
    // Since the original fences are passed in by const reference, we
    // duplicate them here so that the duped fences can be moved into
    // the create info struct of the semaphore.
    zx::event fence_copy;
    auto status = fence_original.duplicate(ZX_RIGHT_SAME_RIGHTS, &fence_copy);
    FX_DCHECK(status == ZX_OK);

    auto sema = escher::Semaphore::New(escher_->vk_device());
    vk::ImportSemaphoreZirconHandleInfoFUCHSIA info;
    info.semaphore = sema->vk_semaphore();
    info.zirconHandle = fence_copy.release();
    info.handleType = vk::ExternalSemaphoreHandleTypeFlagBits::eZirconEventFUCHSIA;

    auto result = escher_->vk_device().importSemaphoreZirconHandleFUCHSIA(
        info, escher_->device()->dispatch_loader());
    FX_DCHECK(result == vk::Result::eSuccess);

    // Create a flow event that ends in the magma system driver.
    zx::event semaphore_event = GetEventForSemaphore(escher_->device(), sema);
    zx_info_handle_basic_t koid_info;
    status = semaphore_event.get_info(ZX_INFO_HANDLE_BASIC, &koid_info, sizeof(koid_info), nullptr,
                                      nullptr);
    TRACE_FLOW_BEGIN("gfx", "semaphore", koid_info.koid);

    semaphores.emplace_back(sema);

    // TODO(fxbug.dev/93069): Semaphore lifetime should be guaranteed by Escher. This wait is a
    // workaround for the issue where we destroy semaphores before they are signalled.
    auto wait = std::make_shared<async::WaitOnce>(fence_original.get(), ZX_EVENT_SIGNALED,
                                                  ZX_WAIT_ASYNC_TIMESTAMP);
    zx_status_t wait_status =
        wait->Begin(async_get_default_dispatcher(),
                    [copy_ref = wait, sema](async_dispatcher_t*, async::WaitOnce*, zx_status_t,
                                            const zx_packet_signal_t*) {
                      // Let these fall out of scope.
                    });
  }

  // Submit the commands and wait for them to finish.
  frame->EndFrame(semaphores, nullptr);
}

void VkRenderer::SetColorConversionValues(const std::array<float, 9>& coefficients,
                                          const std::array<float, 3>& preoffsets,
                                          const std::array<float, 3>& postoffsets) {
  float values[16] = {coefficients[0],
                      coefficients[1],
                      coefficients[2],
                      0,
                      coefficients[3],
                      coefficients[4],
                      coefficients[5],
                      0,
                      coefficients[6],
                      coefficients[7],
                      coefficients[8],
                      0,
                      0,
                      0,
                      0,
                      1};
  glm::mat4 glm_matrix = glm::make_mat4(values);
  glm::vec4 glm_preoffsets(preoffsets[0], preoffsets[1], preoffsets[2], 0.0);
  glm::vec4 glm_postoffsets(postoffsets[0], postoffsets[1], postoffsets[2], 0.0);
  compositor_.SetColorConversionParams({glm_matrix, glm_preoffsets, glm_postoffsets});
}

zx_pixel_format_t VkRenderer::ChoosePreferredPixelFormat(
    const std::vector<zx_pixel_format_t>& available_formats) const {
  for (auto preferred_format : kPreferredImageFormats) {
    for (zx_pixel_format_t format : available_formats) {
      vk::Format vk_format = ConvertToVkFormat(format);
      if (vk_format == preferred_format) {
        return format;
      }
    }
  }
  FX_DCHECK(false) << "Preferred format is not available.";
  return ZX_PIXEL_FORMAT_NONE;
}

bool VkRenderer::SupportsRenderInProtected() const { return escher_->allow_protected_memory(); }

bool VkRenderer::RequiresRenderInProtected(
    const std::vector<allocation::ImageMetadata>& images) const {
  std::unique_lock<std::mutex> lock(mutex_);
  const auto local_texture_map = texture_map_;
  lock.unlock();

  for (const auto& image : images) {
    FX_DCHECK(local_texture_map.find(image.identifier) != local_texture_map.end());
    if (local_texture_map.at(image.identifier)->image()->use_protected_memory()) {
      return true;
    }
  }
  return false;
}

void VkRenderer::WaitIdle() { escher_->vk_device().waitIdle(); }

void VkRenderer::BlitRenderTarget(escher::CommandBuffer* command_buffer,
                                  escher::ImagePtr source_image,
                                  vk::ImageLayout* source_image_layout, escher::ImagePtr dest_image,
                                  const ImageMetadata& metadata) {
  command_buffer->TransitionImageLayout(source_image, *source_image_layout,
                                        vk::ImageLayout::eTransferSrcOptimal);
  *source_image_layout = vk::ImageLayout::eTransferSrcOptimal;
  command_buffer->TransitionImageLayout(dest_image, vk::ImageLayout::eUndefined,
                                        vk::ImageLayout::eTransferDstOptimal);
  command_buffer->Blit(
      source_image, vk::Offset2D(0, 0), vk::Extent2D(metadata.width, metadata.height), dest_image,
      vk::Offset2D(0, 0), vk::Extent2D(metadata.width, metadata.height), kDefaultFilter);
}

std::unordered_map<GlobalBufferCollectionId, VkRenderer::CollectionData>*
VkRenderer::UsageToCollection(BufferCollectionUsage usage) {
  switch (usage) {
    case BufferCollectionUsage::kRenderTarget:
      return &render_target_collections_;
      break;
    case BufferCollectionUsage::kReadback:
      return &readback_collections_;
      break;
    default:
      return &texture_collections_;
  }
}

}  // namespace flatland
