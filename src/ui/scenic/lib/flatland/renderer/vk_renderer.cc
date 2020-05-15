// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/flatland/renderer/vk_renderer.h"

#include "src/ui/lib/escher/escher.h"
#include "src/ui/lib/escher/impl/vulkan_utils.h"
#include "src/ui/lib/escher/renderer/render_funcs.h"
#include "src/ui/lib/escher/util/image_utils.h"

namespace escher {
namespace {

TexturePtr CreateDepthTexture(Escher* escher, const ImagePtr& output_image) {
  TexturePtr depth_buffer;
  RenderFuncs::ObtainDepthTexture(
      escher, output_image->use_protected_memory(), output_image->info(),
      escher->device()->caps().GetMatchingDepthStencilFormat().value, depth_buffer);
  return depth_buffer;
}

}  // anonymous namespace
}  // namespace escher

namespace flatland {

VkRenderer::VkRenderer(std::unique_ptr<escher::Escher> escher)
    : escher_(std::move(escher)), compositor_(escher::RectangleCompositor(escher_.get())) {}

VkRenderer::~VkRenderer() {
  auto vk_device = escher_->vk_device();
  auto vk_loader = escher_->device()->dispatch_loader();
  for (auto& [_, vk_collection] : vk_collection_map_) {
    vk_device.destroyBufferCollectionFUCHSIA(vk_collection, nullptr, vk_loader);
  }
}

GlobalBufferCollectionId VkRenderer::RegisterRenderTargetCollection(
    fuchsia::sysmem::Allocator_Sync* sysmem_allocator,
    fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token) {
  return RegisterCollection(sysmem_allocator, std::move(token),
                            escher::RectangleCompositor::kRenderTargetUsageFlags);
}

GlobalBufferCollectionId VkRenderer::RegisterTextureCollection(
    fuchsia::sysmem::Allocator_Sync* sysmem_allocator,
    fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token) {
  return RegisterCollection(sysmem_allocator, std::move(token),
                            escher::RectangleCompositor::kTextureUsageFlags);
}

GlobalBufferCollectionId VkRenderer::RegisterCollection(
    fuchsia::sysmem::Allocator_Sync* sysmem_allocator,
    fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token,
    vk::ImageUsageFlags usage) {
  auto vk_device = escher_->vk_device();
  auto vk_loader = escher_->device()->dispatch_loader();
  FX_DCHECK(vk_device);

  // Check for a null token here before we try to duplicate it to get the
  // vulkan token.
  if (!token.is_valid()) {
    FX_LOGS(ERROR) << "Token is invalid.";
    return kInvalidId;
  }

  auto vk_constraints =
      escher::RectangleCompositor::GetDefaultImageConstraints(vk::Format::eB8G8R8A8Unorm, usage);

  // Create a duped vulkan token.
  fuchsia::sysmem::BufferCollectionTokenSyncPtr vulkan_token;
  {
    // TODO(51213): See if this can become asynchronous.
    fuchsia::sysmem::BufferCollectionTokenSyncPtr sync_token = token.BindSync();
    zx_status_t status =
        sync_token->Duplicate(std::numeric_limits<uint32_t>::max(), vulkan_token.NewRequest());
    FX_DCHECK(status == ZX_OK);

    // Reassign the channel to the non-sync interface handle.
    token = sync_token.Unbind();
  }

  // Create the sysmem buffer collection. We do this before creating the vulkan collection below,
  // since New() checks if the incoming token is of the wrong type/malicious.
  auto result = BufferCollectionInfo::New(sysmem_allocator, std::move(token));
  if (result.is_error()) {
    FX_LOGS(ERROR) << "Unable to register collection.";
    return kInvalidId;
  }

  // Create the vk_collection and set its constraints.
  vk::BufferCollectionFUCHSIA vk_collection;
  {
    vk::BufferCollectionCreateInfoFUCHSIA buffer_collection_create_info;
    buffer_collection_create_info.collectionToken = vulkan_token.Unbind().TakeChannel().release();
    vk_collection = escher::ESCHER_CHECKED_VK_RESULT(
        vk_device.createBufferCollectionFUCHSIA(buffer_collection_create_info, nullptr, vk_loader));
    auto vk_result =
        vk_device.setBufferCollectionConstraintsFUCHSIA(vk_collection, vk_constraints, vk_loader);
    FX_DCHECK(vk_result == vk::Result::eSuccess);
  }

  // Atomically increment the id generator and create a new identifier for the
  // current buffer collection.
  GlobalBufferCollectionId identifier = id_generator_++;

  // Multiple threads may be attempting to read/write from |collection_map_| so we
  // lock this function here.
  // TODO(44335): Convert this to a lock-free structure.
  std::unique_lock<std::mutex> lock(lock_);
  collection_map_[identifier] = std::move(result.value());
  vk_collection_map_[identifier] = std::move(vk_collection);
  return identifier;
}

std::optional<BufferCollectionMetadata> VkRenderer::Validate(
    GlobalBufferCollectionId collection_id) {
  // TODO(44335): Convert this to a lock-free structure. This is trickier than in the other
  // cases for this class since we are mutating the buffer collection in this call. So we can
  // only convert this to a lock free structure if the elements in the map are changed to be values
  // only, or if we can guarantee that mutations on the elements only occur in a single thread.
  std::unique_lock<std::mutex> lock(lock_);
  auto collection_itr = collection_map_.find(collection_id);

  // If the collection is not in the map, then it can't be validated.
  if (collection_itr == collection_map_.end()) {
    return std::nullopt;
  }

  // If there is already metadata, we can just return it instead of checking the allocation
  // status again. Once a buffer is allocated it won't be stop being allocated.
  auto metadata_itr = collection_metadata_map_.find(collection_id);
  if (metadata_itr != collection_metadata_map_.end()) {
    return metadata_itr->second;
  }

  // The collection should be allocated (i.e. all constraints set).
  auto& collection = collection_itr->second;
  if (!collection.BuffersAreAllocated()) {
    return std::nullopt;
  }

  // If the collection is in the map, and it's allocated, then we can return meta-data regarding
  // vmos and image constraints to the client.
  const auto& sysmem_info = collection.GetSysmemInfo();
  BufferCollectionMetadata result;
  result.vmo_count = sysmem_info.buffer_count;
  result.image_constraints = sysmem_info.settings.image_format_constraints;
  collection_metadata_map_[collection_id] = result;
  return result;
}

escher::ImagePtr VkRenderer::ExtractRenderTarget(escher::CommandBuffer* command_buffer,
                                                 ImageMetadata metadata) {
  return ExtractImage(command_buffer, metadata,
                      escher::RectangleCompositor::kRenderTargetUsageFlags,
                      vk::ImageLayout::eColorAttachmentOptimal);
}

escher::TexturePtr VkRenderer::ExtractTexture(escher::CommandBuffer* command_buffer,
                                              ImageMetadata metadata) {
  auto image =
      ExtractImage(command_buffer, metadata, escher::RectangleCompositor::kTextureUsageFlags,
                   vk::ImageLayout::eShaderReadOnlyOptimal);
  auto texture = escher::Texture::New(escher_->resource_recycler(), image, vk::Filter::eNearest);
  FX_DCHECK(texture);
  return texture;
}

escher::ImagePtr VkRenderer::ExtractImage(escher::CommandBuffer* command_buffer,
                                          ImageMetadata metadata, vk::ImageUsageFlags usage,
                                          vk::ImageLayout layout) {
  auto vk_device = escher_->vk_device();
  auto vk_loader = escher_->device()->dispatch_loader();

  GpuImageInfo gpu_info;
  {
    // TODO(44335): Convert this to a lock-free structure.
    std::unique_lock<std::mutex> lock(lock_);
    auto collection_itr = collection_map_.find(metadata.collection_id);
    FX_DCHECK(collection_itr != collection_map_.end());
    auto& collection = collection_itr->second;

    auto vk_itr = vk_collection_map_.find(metadata.collection_id);
    FX_DCHECK(vk_itr != vk_collection_map_.end());
    auto vk_collection = vk_itr->second;

    // Create the GPU info from the server side collection.
    gpu_info = GpuImageInfo::New(vk_device, vk_loader, collection.GetSysmemInfo(), vk_collection,
                                 metadata.vmo_idx);
    FX_DCHECK(gpu_info.GetGpuMem());
  }

  // Create and return an escher image.
  auto image_ptr = escher::image_utils::NewImage(
      vk_device, gpu_info.NewVkImageCreateInfo(metadata.width, metadata.height, usage),
      gpu_info.GetGpuMem(), escher_->resource_recycler());
  FX_DCHECK(image_ptr);

  // Transition the image to the provided layout.
  // TODO(52196): The way we are transitioning image layouts here and in the rest of scenic is
  // incorrect for "external" images. It just happens to be working by luck on our current hardware.
  command_buffer->impl()->TransitionImageLayout(image_ptr, vk::ImageLayout::eUndefined, layout);

  return image_ptr;
}

void VkRenderer::Render(const ImageMetadata& render_target,
                        const std::vector<RenderableMetadata>& renderables) {
  // Escher's frame class acts as a command buffer manager that we use to create a
  // command buffer and submit it to the device queue once we are done.
  auto frame = escher_->NewFrame("flatland::VkRenderer", ++frame_number_);
  auto command_buffer = frame->cmds();

  std::vector<escher::TexturePtr> textures;
  std::vector<escher::RectangleRenderable> rectangles;
  for (const auto& renderable : renderables) {
    // Pass the texture into the above vector to keep it alive outside of this loop.
    auto texture = ExtractTexture(command_buffer, renderable.image);
    textures.push_back(texture);

    // Create escher rectangle renderable. This function will CHECK if the matrix has an
    // invalid rotation that is not a multiple of 90 degrees and DCHECK any other issue,
    // such as having uv coordinates that are not within the range [0,1].
    const auto rectangle =
        escher::RectangleRenderable::Create(renderable.matrix, renderable.uv_coords, texture.get(),
                                            renderable.multiply_color, renderable.is_transparent);
    rectangles.push_back(rectangle);
  }

  // Grab the output image and use it to generate a depth texture. The depth texture needs to
  // be the same width and height as the output image.
  auto output_image = ExtractRenderTarget(command_buffer, render_target);
  escher::TexturePtr depth_texture = escher::CreateDepthTexture(escher_.get(), output_image);

  // Now the compositor can finally draw.
  compositor_.DrawBatch(command_buffer, rectangles, output_image, depth_texture);

  // Submit the commands and wait for them to finish.
  frame->EndFrame(escher::SemaphorePtr(), nullptr);
  escher_->vk_device().waitIdle();
}

}  // namespace flatland
