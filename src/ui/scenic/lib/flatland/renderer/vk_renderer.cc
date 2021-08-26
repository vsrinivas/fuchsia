// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/flatland/renderer/vk_renderer.h"

#include "src/ui/lib/escher/escher.h"
#include "src/ui/lib/escher/impl/naive_image.h"
#include "src/ui/lib/escher/impl/vulkan_utils.h"
#include "src/ui/lib/escher/renderer/render_funcs.h"
#include "src/ui/lib/escher/renderer/sampler_cache.h"
#include "src/ui/lib/escher/resources/resource_recycler.h"
#include "src/ui/lib/escher/util/fuchsia_utils.h"
#include "src/ui/lib/escher/util/image_utils.h"
#include "src/ui/lib/escher/util/trace_macros.h"

namespace {

// Highest priority format first.
const std::vector<vk::Format> kPreferredImageFormats = {
    vk::Format::eR8G8B8A8Srgb, vk::Format::eB8G8R8A8Srgb, vk::Format::eG8B8R83Plane420Unorm,
    vk::Format::eG8B8R82Plane420Unorm};

const vk::Filter kDefaultFilter = vk::Filter::eNearest;

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

static escher::TexturePtr CreateDepthTexture(escher::Escher* escher,
                                             const escher::ImagePtr& output_image) {
  escher::TexturePtr depth_buffer;
  escher::RenderFuncs::ObtainDepthTexture(
      escher, output_image->use_protected_memory(), output_image->info(),
      escher->device()->caps().GetMatchingDepthStencilFormat().value, depth_buffer);
  return depth_buffer;
}

}  // anonymous namespace

namespace flatland {

VkRenderer::VkRenderer(escher::EscherWeakPtr escher)
    : escher_(std::move(escher)), compositor_(escher::RectangleCompositor(escher_.get())) {}

VkRenderer::~VkRenderer() {
  auto vk_device = escher_->vk_device();
  auto vk_loader = escher_->device()->dispatch_loader();
  for (auto& [_, collection] : collections_) {
    vk_device.destroyBufferCollectionFUCHSIAX(collection.vk_collection, nullptr, vk_loader);
  }
  collections_.clear();
}

bool VkRenderer::ImportBufferCollection(
    GlobalBufferCollectionId collection_id, fuchsia::sysmem::Allocator_Sync* sysmem_allocator,
    fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token) {
  return RegisterCollection(collection_id, sysmem_allocator, std::move(token),
                            escher::RectangleCompositor::kTextureUsageFlags);
}

void VkRenderer::ReleaseBufferCollection(GlobalBufferCollectionId collection_id) {
  // Multiple threads may be attempting to read/write from the various maps,
  // lock this function here.
  // TODO(fxbug.dev/44335): Convert this to a lock-free structure.
  std::unique_lock<std::mutex> lock(lock_);

  auto collection_itr = collections_.find(collection_id);

  // If the collection is not in the map, then there's nothing to do.
  if (collection_itr == collections_.end()) {
    FX_LOGS(WARNING) << "Attempting to release a non-existent buffer collection.";
    return;
  }

  auto vk_device = escher_->vk_device();
  auto vk_loader = escher_->device()->dispatch_loader();
  vk_device.destroyBufferCollectionFUCHSIAX(collection_itr->second.vk_collection, nullptr,
                                            vk_loader);
  collections_.erase(collection_id);
}

bool VkRenderer::RegisterCollection(
    GlobalBufferCollectionId collection_id, fuchsia::sysmem::Allocator_Sync* sysmem_allocator,
    fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token,
    vk::ImageUsageFlags usage) {
  TRACE_DURATION("gfx", "VkRenderer::RegisterCollection");
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
  FX_DCHECK(status == ZX_OK);

  // Create the sysmem collection.
  fuchsia::sysmem::BufferCollectionSyncPtr buffer_collection;
  {
    // Use local token to create a BufferCollection and then sync. We can trust
    // |buffer_collection->Sync()| to tell us if we have a bad or malicious channel. So if this call
    // passes, then we know we have a valid BufferCollection.
    sysmem_allocator->BindSharedCollection(std::move(local_token), buffer_collection.NewRequest());
    zx_status_t status = buffer_collection->Sync();
    if (status != ZX_OK) {
      FX_LOGS(ERROR) << "Could not bind buffer collection. Status: " << status;
      return false;
    }

    // Use a name with a priority that's greater than the vulkan implementation, but less than
    // what any client would use.
    buffer_collection->SetName(10u, "FlatlandImageMemory");
    status = buffer_collection->SetConstraints(false /* has_constraints */,
                                               fuchsia::sysmem::BufferCollectionConstraints());
    FX_DCHECK(status == ZX_OK);
  }

  // Create the vk collection.
  vk::BufferCollectionFUCHSIAX collection;
  {
    std::vector<vk::ImageCreateInfo> create_infos;
    for (const auto& format : kPreferredImageFormats) {
      create_infos.push_back(
          escher::RectangleCompositor::GetDefaultImageConstraints(format, usage));
    }

    vk::ImageConstraintsInfoFUCHSIAX image_constraints_info;
    image_constraints_info.createInfoCount = create_infos.size();
    image_constraints_info.pCreateInfos = create_infos.data();
    image_constraints_info.pFormatConstraints = nullptr;
    image_constraints_info.pNext = nullptr;
    image_constraints_info.minBufferCount = 1;
    image_constraints_info.minBufferCountForDedicatedSlack = 0;
    image_constraints_info.minBufferCountForSharedSlack = 0;
    if (escher_->allow_protected_memory())
      image_constraints_info.flags = vk::ImageConstraintsInfoFlagBitsFUCHSIAX::eProtectedOptional;

    // Create the collection and set its constraints.
    vk::BufferCollectionCreateInfoFUCHSIAX buffer_collection_create_info;
    buffer_collection_create_info.collectionToken = vulkan_token.Unbind().TakeChannel().release();
    collection = escher::ESCHER_CHECKED_VK_RESULT(vk_device.createBufferCollectionFUCHSIAX(
        buffer_collection_create_info, nullptr, vk_loader));
    auto vk_result = vk_device.setBufferCollectionImageConstraintsFUCHSIAX(
        collection, image_constraints_info, vk_loader);
    FX_DCHECK(vk_result == vk::Result::eSuccess);
  }

  // Multiple threads may be attempting to read/write from |collections_|
  // so we lock this function here.
  // TODO(fxbug.dev/44335): Convert this to a lock-free structure.
  std::unique_lock<std::mutex> lock(lock_);
  collections_[collection_id] = {
      .collection = std::move(buffer_collection),
      .vk_collection = std::move(collection),
      .is_render_target = (usage == escher::RectangleCompositor::kRenderTargetUsageFlags)};
  return true;
}

bool VkRenderer::ImportBufferImage(const allocation::ImageMetadata& metadata) {
  std::unique_lock<std::mutex> lock(lock_);

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
  auto collection_itr = collections_.find(metadata.collection_id);
  if (collection_itr == collections_.end()) {
    FX_LOGS(WARNING) << "Collection with id " << metadata.collection_id << " does not exist.";
    return false;
  }

  // Check to see if the buffers are allocated and return false if not.
  zx_status_t allocation_status = ZX_OK;
  zx_status_t status = collection_itr->second.collection->CheckBuffersAllocated(&allocation_status);
  if (status != ZX_OK || allocation_status != ZX_OK) {
    FX_LOGS(WARNING) << "Collection was not allocated.";
    return false;
  }

  // Make sure we're not reusing the same image identifier.
  if (texture_map_.find(metadata.identifier) != texture_map_.end() ||
      render_target_map_.find(metadata.identifier) != render_target_map_.end()) {
    FX_LOGS(WARNING) << "An image with this identifier already exists.";
    return false;
  }

  if (collection_itr->second.is_render_target) {
    auto image = ExtractImage(metadata, collection_itr->second.vk_collection,
                              escher::RectangleCompositor::kRenderTargetUsageFlags);
    if (!image) {
      FX_LOGS(ERROR) << "Could not extract render target.";
      return false;
    }

    image->set_swapchain_layout(vk::ImageLayout::eColorAttachmentOptimal);
    render_target_map_[metadata.identifier] = image;
    depth_target_map_[metadata.identifier] = CreateDepthTexture(escher_.get(), image);
    pending_render_targets_.insert(metadata.identifier);
  } else {
    auto texture = ExtractTexture(metadata, collection_itr->second.vk_collection);
    if (!texture) {
      FX_LOGS(ERROR) << "Could not extract client texture image.";
      return false;
    }
    texture_map_[metadata.identifier] = texture;
    pending_textures_.insert(metadata.identifier);
  }
  return true;
}

void VkRenderer::ReleaseBufferImage(allocation::GlobalImageId image_id) {
  std::unique_lock<std::mutex> lock(lock_);
  if (texture_map_.find(image_id) != texture_map_.end()) {
    texture_map_.erase(image_id);
  } else if (render_target_map_.find(image_id) != render_target_map_.end()) {
    render_target_map_.erase(image_id);
    depth_target_map_.erase(image_id);
  }
}

escher::ImagePtr VkRenderer::ExtractImage(const allocation::ImageMetadata& metadata,
                                          vk::BufferCollectionFUCHSIAX collection,
                                          vk::ImageUsageFlags usage) {
  TRACE_DURATION("gfx", "VkRenderer::ExtractImage");
  auto vk_device = escher_->vk_device();
  auto vk_loader = escher_->device()->dispatch_loader();

  // Grab the collection Properties from Vulkan.
  auto properties = escher::ESCHER_CHECKED_VK_RESULT(
      vk_device.getBufferCollectionProperties2FUCHSIAX(collection, vk_loader));

  // Check the provided index against actually allocated number of buffers.
  if (properties.bufferCount <= metadata.vmo_index) {
    FX_LOGS(ERROR) << "Specified vmo index is out of bounds: " << metadata.vmo_index;
    return nullptr;
  }

  // Check if allocated buffers are backed by protected memory.
  bool is_protected =
      (escher_->vk_physical_device()
           .getMemoryProperties()
           .memoryTypes[escher::CountTrailingZeros(properties.memoryTypeBits)]
           .propertyFlags &
       vk::MemoryPropertyFlagBits::eProtected) == vk::MemoryPropertyFlagBits::eProtected;

  // Setup the create info Fuchsia extension.
  vk::BufferCollectionImageCreateInfoFUCHSIAX collection_image_info;
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
  vk::StructureChain<vk::MemoryAllocateInfo, vk::ImportMemoryBufferCollectionFUCHSIAX,
                     vk::MemoryDedicatedAllocateInfoKHR>
      alloc_info(vk::MemoryAllocateInfo()
                     .setAllocationSize(memory_requirements.size)
                     .setMemoryTypeIndex(memory_type_index),
                 vk::ImportMemoryBufferCollectionFUCHSIAX()
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
  escher_image_info.memory_flags = vk::MemoryPropertyFlagBits::eDeviceLocal;
  if (create_info.flags & vk::ImageCreateFlagBits::eProtected) {
    escher_image_info.memory_flags = vk::MemoryPropertyFlagBits::eProtected;
  }
  escher_image_info.is_external = true;
  return escher::impl::NaiveImage::AdoptVkImage(escher_->resource_recycler(), escher_image_info,
                                                image_result.value, std::move(gpu_mem),
                                                create_info.initialLayout);
}

bool VkRenderer::RegisterRenderTargetCollection(
    GlobalBufferCollectionId collection_id, fuchsia::sysmem::Allocator_Sync* sysmem_allocator,
    fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token) {
  return RegisterCollection(collection_id, sysmem_allocator, std::move(token),
                            escher::RectangleCompositor::kRenderTargetUsageFlags);
}

void VkRenderer::DeregisterRenderTargetCollection(GlobalBufferCollectionId collection_id) {
  ReleaseBufferCollection(collection_id);
}

escher::TexturePtr VkRenderer::ExtractTexture(const allocation::ImageMetadata& metadata,
                                              vk::BufferCollectionFUCHSIAX collection) {
  auto image = ExtractImage(metadata, collection, escher::RectangleCompositor::kTextureUsageFlags);
  if (!image) {
    FX_LOGS(ERROR) << "Image for texture was nullptr.";
    return nullptr;
  }

  escher::SamplerPtr sampler =
      escher::image_utils::IsYuvFormat(image->format())
          ? escher_->sampler_cache()->ObtainYuvSampler(image->format(), kDefaultFilter)
          : escher_->sampler_cache()->ObtainSampler(kDefaultFilter);
  FX_DCHECK(escher::image_utils::IsYuvFormat(image->format()) ? sampler->is_immutable()
                                                              : !sampler->is_immutable());
  auto texture = fxl::MakeRefCounted<escher::Texture>(escher_->resource_recycler(), sampler, image);
  return texture;
}

void VkRenderer::Render(const ImageMetadata& render_target,
                        const std::vector<Rectangle2D>& rectangles,
                        const std::vector<ImageMetadata>& images,
                        const std::vector<zx::event>& release_fences) {
  TRACE_DURATION("gfx", "VkRenderer::Render");

  FX_DCHECK(rectangles.size() == images.size());

  // Copy over the texture and render target data to local containers that do not need
  // to be accessed via a lock. We're just doing a shallow copy via the copy assignment
  // operator since the texture and render target data is just referenced through pointers.
  // We manually unlock the lock after copying over the data.
  std::unique_lock<std::mutex> lock(lock_);
  const auto local_texture_map = texture_map_;
  const auto local_render_target_map = render_target_map_;
  const auto local_depth_target_map = depth_target_map_;

  // After moving, the original containers are emptied.
  const auto local_pending_textures = std::move(pending_textures_);
  const auto local_pending_render_targets = std::move(pending_render_targets_);
  lock.unlock();

  // If we have any |images| protected, we should switch to a protected escher::Frame and
  // |render_target| should also be protected.
  bool has_protected_images = false;
  for (const auto& image : images) {
    FX_DCHECK(local_texture_map.find(image.identifier) != local_texture_map.end());
    if (local_texture_map.at(image.identifier)->image()->use_protected_memory()) {
      has_protected_images = true;
      break;
    }
  }
  FX_DCHECK(local_render_target_map.find(render_target.identifier) !=
            local_render_target_map.end());
  FX_DCHECK(!has_protected_images ||
            local_render_target_map.at(render_target.identifier)->use_protected_memory());

  // Escher's frame class acts as a command buffer manager that we use to create a
  // command buffer and submit it to the device queue once we are done.
  auto frame = escher_->NewFrame(
      "flatland::VkRenderer", ++frame_number_, /*enable_gpu_logging=*/false,
      /*requested_type=*/escher::CommandBuffer::Type::kGraphics, has_protected_images);
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
    command_buffer->impl()->TransitionImageLayout(target, vk::ImageLayout::eUndefined,
                                                  vk::ImageLayout::eColorAttachmentOptimal);
  }

  std::vector<const escher::TexturePtr> textures;
  std::vector<escher::RectangleCompositor::ColorData> color_data;
  for (const auto& image : images) {
    // Pass the texture into the above vector to keep it alive outside of this loop.
    textures.emplace_back(local_texture_map.at(image.identifier));

    glm::vec4 multiply(image.multiply_color[0], image.multiply_color[1], image.multiply_color[2],
                       image.multiply_color[3]);
    color_data.emplace_back(escher::RectangleCompositor::ColorData(multiply, image.is_opaque));
  }

  // Grab the output image and use it to generate a depth texture. The depth texture needs to
  // be the same width and height as the output image.
  const auto output_image = local_render_target_map.at(render_target.identifier);
  const auto depth_texture = local_depth_target_map.at(render_target.identifier);

  // Now the compositor can finally draw.
  compositor_.DrawBatch(command_buffer, rectangles, textures, color_data, output_image,
                        depth_texture);

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
  }

  // Submit the commands and wait for them to finish.
  frame->EndFrame(semaphores, nullptr);
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

void VkRenderer::WaitIdle() { escher_->vk_device().waitIdle(); }

}  // namespace flatland
