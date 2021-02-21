// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/flatland/renderer/vk_renderer.h"

#include <zircon/pixelformat.h>

#include "src/ui/lib/escher/escher.h"
#include "src/ui/lib/escher/impl/vulkan_utils.h"
#include "src/ui/lib/escher/renderer/render_funcs.h"
#include "src/ui/lib/escher/util/image_utils.h"
#include "src/ui/lib/escher/util/trace_macros.h"

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

VkRenderer::VkRenderer(escher::EscherWeakPtr escher)
    : escher_(std::move(escher)), compositor_(escher::RectangleCompositor(escher_.get())) {}

VkRenderer::~VkRenderer() {
  auto vk_device = escher_->vk_device();
  auto vk_loader = escher_->device()->dispatch_loader();
  for (auto& [_, vk_collection] : vk_collection_map_) {
    vk_device.destroyBufferCollectionFUCHSIA(vk_collection, nullptr, vk_loader);
  }
}

bool VkRenderer::ImportBufferCollection(
    sysmem_util::GlobalBufferCollectionId collection_id,
    fuchsia::sysmem::Allocator_Sync* sysmem_allocator,
    fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token) {
  return RegisterCollection(collection_id, sysmem_allocator, std::move(token),
                            escher::RectangleCompositor::kTextureUsageFlags);
}

void VkRenderer::ReleaseBufferCollection(sysmem_util::GlobalBufferCollectionId collection_id) {
  // Multiple threads may be attempting to read/write from the various maps,
  // lock this function here.
  // TODO(fxbug.dev/44335): Convert this to a lock-free structure.
  std::unique_lock<std::mutex> lock(lock_);

  auto collection_itr = collection_map_.find(collection_id);

  // If the collection is not in the map, then there's nothing to do.
  if (collection_itr == collection_map_.end()) {
    return;
  }

  // Erase the sysmem collection from the map.
  collection_map_.erase(collection_id);

  // Grab the vulkan collection and DCHECK since there should always be
  // a vk collection to correspond with the buffer collection. We then
  // delete it using the vk device.
  auto vk_itr = vk_collection_map_.find(collection_id);
  FX_DCHECK(vk_itr != vk_collection_map_.end());
  auto vk_device = escher_->vk_device();
  auto vk_loader = escher_->device()->dispatch_loader();
  vk_device.destroyBufferCollectionFUCHSIA(vk_itr->second, nullptr, vk_loader);
  vk_collection_map_.erase(collection_id);
}

bool VkRenderer::RegisterCollection(
    sysmem_util::GlobalBufferCollectionId collection_id,
    fuchsia::sysmem::Allocator_Sync* sysmem_allocator,
    fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token,
    vk::ImageUsageFlags usage) {
  TRACE_DURATION("flatland", "VkRenderer::RegisterCollection");
  auto vk_device = escher_->vk_device();
  auto vk_loader = escher_->device()->dispatch_loader();
  FX_DCHECK(vk_device);
  FX_DCHECK(collection_id != sysmem_util::kInvalidId);

  // Check for a null token here before we try to duplicate it to get the
  // vulkan token.
  if (!token.is_valid()) {
    FX_LOGS(ERROR) << "Token is invalid.";
    return sysmem_util::kInvalidId;
  }

  auto vk_constraints =
      escher::RectangleCompositor::GetDefaultImageConstraints(vk::Format::eB8G8R8A8Unorm, usage);

  // Create a duped vulkan token.
  fuchsia::sysmem::BufferCollectionTokenSyncPtr vulkan_token;
  {
    // TODO(fxbug.dev/51213): See if this can become asynchronous.
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
    return false;
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

  // Multiple threads may be attempting to read/write from |collection_map_| so we
  // lock this function here.
  // TODO(fxbug.dev/44335): Convert this to a lock-free structure.
  std::unique_lock<std::mutex> lock(lock_);
  collection_map_[collection_id] = std::move(result.value());
  vk_collection_map_[collection_id] = std::move(vk_collection);
  return true;
}

bool VkRenderer::ImportBufferImage(const ImageMetadata& metadata) {
  std::unique_lock<std::mutex> lock(lock_);
  const auto& collection_itr = collection_map_.find(metadata.collection_id);
  if (collection_itr == collection_map_.end()) {
    FX_LOGS(ERROR) << "Collection with id " << metadata.collection_id << " does not exist.";
    return false;
  }

  auto& collection = collection_itr->second;
  if (!collection.BuffersAreAllocated()) {
    FX_LOGS(ERROR) << "Buffers for collection " << metadata.collection_id
                   << " have not been allocated.";
    return false;
  }

  const auto& sysmem_info = collection.GetSysmemInfo();
  const auto vmo_count = sysmem_info.buffer_count;
  auto image_constraints = sysmem_info.settings.image_format_constraints;

  if (texture_map_.find(metadata.identifier) != texture_map_.end() ||
      render_target_map_.find(metadata.identifier) != render_target_map_.end()) {
    FX_LOGS(ERROR) << "An image with this identifier already exists.";
    return false;
  }

  if (metadata.vmo_index >= vmo_count) {
    FX_LOGS(ERROR) << "CreateImage failed, vmo_index " << metadata.vmo_index
                   << " must be less than vmo_count " << vmo_count;
    return false;
  }

  if (metadata.width < image_constraints.min_coded_width ||
      metadata.width > image_constraints.max_coded_width) {
    FX_LOGS(ERROR) << "CreateImage failed, width " << metadata.width
                   << " is not within valid range [" << image_constraints.min_coded_width << ","
                   << image_constraints.max_coded_width << "]";
    return false;
  }

  if (metadata.height < image_constraints.min_coded_height ||
      metadata.height > image_constraints.max_coded_height) {
    FX_LOGS(ERROR) << "CreateImage failed, height " << metadata.height
                   << " is not within valid range [" << image_constraints.min_coded_height << ","
                   << image_constraints.max_coded_height << "]";
    return false;
  }

  if (metadata.is_render_target) {
    auto image = ExtractImage(metadata, escher::RectangleCompositor::kRenderTargetUsageFlags);
    image->set_swapchain_layout(vk::ImageLayout::eColorAttachmentOptimal);
    render_target_map_[metadata.identifier] = image;
    depth_target_map_[metadata.identifier] = escher::CreateDepthTexture(escher_.get(), image);
    pending_render_targets_.insert(metadata.identifier);
  } else {
    texture_map_[metadata.identifier] = ExtractTexture(metadata);
    pending_textures_.insert(metadata.identifier);
  }
  return true;
}

void VkRenderer::ReleaseBufferImage(sysmem_util::GlobalImageId image_id) {
  std::unique_lock<std::mutex> lock(lock_);
  if (texture_map_.find(image_id) != texture_map_.end()) {
    texture_map_.erase(image_id);
  } else if (render_target_map_.find(image_id) != render_target_map_.end()) {
    render_target_map_.erase(image_id);
    depth_target_map_.erase(image_id);
  }
}

escher::ImagePtr VkRenderer::ExtractImage(ImageMetadata metadata, vk::ImageUsageFlags usage) {
  TRACE_DURATION("flatland", "VkRenderer::ExtractImage");
  auto vk_device = escher_->vk_device();
  auto vk_loader = escher_->device()->dispatch_loader();

  GpuImageInfo gpu_info;
  auto collection_itr = collection_map_.find(metadata.collection_id);
  FX_DCHECK(collection_itr != collection_map_.end());
  auto& collection = collection_itr->second;

  auto vk_itr = vk_collection_map_.find(metadata.collection_id);
  FX_DCHECK(vk_itr != vk_collection_map_.end());
  auto vk_collection = vk_itr->second;

  // Create the GPU info from the server side collection.
  gpu_info = GpuImageInfo::New(vk_device, vk_loader, collection.GetSysmemInfo(), vk_collection,
                               metadata.vmo_index);
  FX_DCHECK(gpu_info.GetGpuMem());

  // Create and return an escher image.
  auto image_ptr = escher::image_utils::NewImage(
      vk_device, gpu_info.NewVkImageCreateInfo(metadata.width, metadata.height, usage),
      gpu_info.GetGpuMem(), escher_->resource_recycler());
  FX_DCHECK(image_ptr);
  return image_ptr;
}

bool VkRenderer::RegisterRenderTargetCollection(
    sysmem_util::GlobalBufferCollectionId collection_id,
    fuchsia::sysmem::Allocator_Sync* sysmem_allocator,
    fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token) {
  return RegisterCollection(collection_id, sysmem_allocator, std::move(token),
                            escher::RectangleCompositor::kRenderTargetUsageFlags);
}

void VkRenderer::DeregisterRenderTargetCollection(
    sysmem_util::GlobalBufferCollectionId collection_id) {
  ReleaseBufferCollection(collection_id);
}

escher::TexturePtr VkRenderer::ExtractTexture(ImageMetadata metadata) {
  auto image = ExtractImage(metadata, escher::RectangleCompositor::kTextureUsageFlags);
  auto texture = escher::Texture::New(escher_->resource_recycler(), image, vk::Filter::eNearest);
  FX_DCHECK(texture);
  return texture;
}

void VkRenderer::Render(const ImageMetadata& render_target,
                        const std::vector<Rectangle2D>& rectangles,
                        const std::vector<ImageMetadata>& images,
                        const std::vector<zx::event>& release_fences) {
  TRACE_DURATION("flatland", "VkRenderer::Render");

  // Escher's frame class acts as a command buffer manager that we use to create a
  // command buffer and submit it to the device queue once we are done.
  auto frame = escher_->NewFrame("flatland::VkRenderer", ++frame_number_);
  auto command_buffer = frame->cmds();

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
    FX_DCHECK(local_texture_map.find(image.identifier) != local_texture_map.end());
    textures.emplace_back(local_texture_map.at(image.identifier));

    // TODO(fxbug.dev/52632): We are hardcoding the multiply color and transparency flag for now.
    // Eventually these will be exposed in the API.
    color_data.emplace_back(
        escher::RectangleCompositor::ColorData(glm::vec4(1.f), image.has_transparency));
  }

  // Grab the output image and use it to generate a depth texture. The depth texture needs to
  // be the same width and height as the output image.
  FX_DCHECK(local_render_target_map.find(render_target.identifier) !=
            local_render_target_map.end());
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
    info.handle = fence_copy.release();
    info.handleType = vk::ExternalSemaphoreHandleTypeFlagBits::eTempZirconEventFUCHSIA;

    auto result = escher_->vk_device().importSemaphoreZirconHandleFUCHSIA(
        info, escher_->device()->dispatch_loader());
    FX_DCHECK(result == vk::Result::eSuccess);

    semaphores.emplace_back(sema);
  }

  // Submit the commands and wait for them to finish.
  frame->EndFrame(semaphores, nullptr);
}

void VkRenderer::WaitIdle() { escher_->vk_device().waitIdle(); }

}  // namespace flatland
