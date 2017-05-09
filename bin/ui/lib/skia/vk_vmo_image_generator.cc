// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Import our copy of vulkan.h first because Skia imports its own that does not
// understand Magma extensions.
#include <vulkan/vulkan.h>

#include "apps/mozart/lib/skia/vk_vmo_image_generator.h"

#include <atomic>
#include <unordered_map>

#include "apps/tracing/lib/trace/event.h"
#include "lib/ftl/logging.h"
#include "lib/mtl/handles/object_info.h"
#include "third_party/skia/include/gpu/GrBackendSurface.h"
#include "third_party/skia/include/gpu/GrContext.h"
#include "third_party/skia/src/gpu/GrResourceProvider.h"
#include "third_party/skia/src/gpu/vk/GrVkGpu.h"

static_assert(sizeof(size_t) == sizeof(uint64_t),
              "Fuchsia should always be 64-bit");

namespace mozart {
namespace {

struct TextureInfo;

// Store a list of TextureInfos for each VMO (represented by the KOID). We need
// this to know if a VMO still has a texture bound to it before we call
// vkFreeMemory.
std::unordered_multimap<mx_koid_t, std::unique_ptr<TextureInfo>> g_textures;

// Remember any VkDeviceMemory that we imported from a given vmo, so that we
// can re-use it rather than calling vkImportDeviceMemoryMAGMA again for the
// same vmo (which is not allowed).
std::unordered_map<mx_koid_t, VkDeviceMemory> g_vmo_to_device_memory_map;

// Number of textures currently alive.
std::atomic<int32_t> g_count;

// Helper struct. Saves data used for cleanup once Skia is done with a texture.
// Also stores a reference to |shared_vmo| to keep it alive.
struct TextureInfo {
 public:
  TextureInfo(VkDevice vk_device,
              VkImage vk_image,
              VkDeviceMemory vk_device_memory,
              ftl::RefPtr<mtl::SharedVmo> shared_vmo)
      : vk_device(vk_device),
        vk_image(vk_image),
        vk_device_memory(vk_device_memory),
        shared_vmo(shared_vmo) {}
  VkDevice vk_device;
  VkImage vk_image;
  VkDeviceMemory vk_device_memory;
  ftl::RefPtr<mtl::SharedVmo> shared_vmo;
};

// Increment/decrement the number of textures currently alive.
void TraceCount(int32_t delta) {
  int32_t count = g_count.fetch_add(delta, std::memory_order_relaxed) + delta;
  TRACE_COUNTER("gfx", "SkImageVulkanVmo", 0u, "count", count);
}

// Store a TextureInfo object in a global map, and return a raw pointer to the
// newly created TextureInfo.
TextureInfo* CreateAndStoreTextureInfoGlobally(
    VkDevice vk_device,
    VkImage vk_image,
    VkDeviceMemory vk_device_memory,
    ftl::RefPtr<mtl::SharedVmo> shared_vmo) {
  // Add information about this texture to our global map of VMOs -> textures
  mx_koid_t vmo_koid = mtl::GetKoid(shared_vmo->vmo().get());

  auto texture_info_ptr = std::make_unique<TextureInfo>(
      vk_device, vk_image, vk_device_memory, shared_vmo);
  TextureInfo* texture_info = texture_info_ptr.get();
  {
    g_textures.insert({vmo_koid, std::move(texture_info_ptr)});
    g_vmo_to_device_memory_map.insert({vmo_koid, vk_device_memory});
  }

  // Increment the global number of textures.
  TraceCount(1);
  return texture_info;
}

// Remove the TextureInfo from the global map, which destroys it and releases a
// reference to the associated SharedVmo. Clean up Vulkan resources associated
// with this TextureInfo.
void ReleaseTexture(void* texture_info) {
  TextureInfo* vmo_info = static_cast<TextureInfo*>(texture_info);

  // Clean up VkImage
  vkDestroyImage(vmo_info->vk_device, vmo_info->vk_image, nullptr);

  mx_koid_t vmo_koid = mtl::GetKoid(vmo_info->shared_vmo->vmo().get());
  {
    // Search for texture_info in g_textures (looking through only the entries
    // that are mapped to vmo_koid
    auto range = g_textures.equal_range(vmo_koid);
    int range_size = std::distance(range.first, range.second);
    FTL_DCHECK(range_size > 0);

    auto are_pointers_equal = [texture_info](auto& kv) -> bool {
      return kv.second.get() == texture_info;
    };
    auto texture_info_it =
        std::find_if(range.first, range.second, are_pointers_equal);
    FTL_DCHECK(texture_info_it != range.second);

    // Clean up VkDeviceMemory only if we're the last texture using this
    // VMO. Otherwise, we can get a crash in the device driver.
    if (range_size == 1) {
      vkFreeMemory(vmo_info->vk_device, vmo_info->vk_device_memory, nullptr);
      g_vmo_to_device_memory_map.erase(vmo_koid);
    }
    // Remove this TextureInfo object from our global map
    g_textures.erase(texture_info_it);
  }

  // Decrement the global number of textures.
  TraceCount(-1);
}

}  // namespace

VkVmoImageGenerator::VkVmoImageGenerator(const SkImageInfo& image_info,
                                         ftl::RefPtr<mtl::SharedVmo> shared_vmo)
    : SkImageGenerator(image_info), shared_vmo_(shared_vmo) {}

VkVmoImageGenerator::~VkVmoImageGenerator() {}

// Imports the |shared_vmo_| as a VkImage and then wraps that in a Skia texture
// (GrTextureProxy).
sk_sp<GrTextureProxy> VkVmoImageGenerator::onGenerateTexture(
    GrContext* context,
    const SkImageInfo& info,
    const SkIPoint& origin) {
  // TODO: Assert subset (described by |info| and |origin|) is equal to full
  // size.
  VkResult err;

  // Create a VkImage from the VkDeviceMemory.
  // TODO: Get the pixel format from info_.
  VkImageCreateInfo image_create_info = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      .pNext = nullptr,
      .flags = VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT,
      .imageType = VK_IMAGE_TYPE_2D,
      .format = VK_FORMAT_B8G8R8A8_UNORM,
      .extent = VkExtent3D{(uint32_t)getInfo().width(),
                           (uint32_t)getInfo().height(), 1},
      .mipLevels = 1,
      .arrayLayers = 1,
      .samples = VK_SAMPLE_COUNT_1_BIT,
      .tiling = VK_IMAGE_TILING_OPTIMAL,
      .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
      .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
      .queueFamilyIndexCount = 0,      // not used since not sharing
      .pQueueFamilyIndices = nullptr,  // not used since not sharing
      .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
  };

  VkImage vk_image;
  VkDevice vk_device = static_cast<GrVkGpu*>(context->getGpu())->device();
  err = vkCreateImage(vk_device, &image_create_info, nullptr, &vk_image);

  if (err != VK_SUCCESS) {
    FTL_LOG(ERROR) << "vkCreateImage failed.";
    return nullptr;
  }

  VkMemoryRequirements memory_reqs;
  vkGetImageMemoryRequirements(vk_device, vk_image, &memory_reqs);

  size_t needed_bytes = memory_reqs.size;
  size_t vmo_size = shared_vmo_->vmo_size();
  if (vmo_size < needed_bytes) {
    FTL_LOG(ERROR) << "Invalid image metadata.";
    return nullptr;
  }

  // Get a VkDeviceMemory out of the VMO.

  // Re-use an existing VkDeviceMemory if we already have one.

  mx_koid_t vmo_koid = mtl::GetKoid(shared_vmo_->vmo().get());
  VkDeviceMemory mem = VK_NULL_HANDLE;
  {
    if (g_vmo_to_device_memory_map.find(vmo_koid) !=
        g_vmo_to_device_memory_map.end()) {
      mem = g_vmo_to_device_memory_map[vmo_koid];
    }
  }

  if (mem == VK_NULL_HANDLE) {
    // Duplicate the VMO because vkImportDeviceMemoryMAGMA takes ownership of
    // the handle it is passed.
    mx::vmo temp_vmo;
    auto status = shared_vmo_->vmo().duplicate(MX_RIGHT_SAME_RIGHTS, &temp_vmo);
    if (status) {
      FTL_LOG(ERROR) << "Failed to duplicate vmo handle.";
      return nullptr;
    }

    err =
        vkImportDeviceMemoryMAGMA(vk_device, temp_vmo.release(), nullptr, &mem);
    if (err != VK_SUCCESS) {
      FTL_LOG(ERROR) << "vkImportDeviceMemoryMAGMA failed.";
      return nullptr;
    }
  }

  err = vkBindImageMemory(vk_device, vk_image, mem, 0);
  if (err != VK_SUCCESS) {
    FTL_LOG(ERROR) << "vkBindImageMemory failed.";
    return nullptr;
  }

  // Now, wrap up the VkImage for Skia.
  GrVkImageInfo gr_texture_info = {
      .fImage = vk_image,
      .fAlloc = {mem, 0, shared_vmo_->vmo_size(), 0},
      .fImageTiling = image_create_info.tiling,
      .fImageLayout = image_create_info.initialLayout,
      .fFormat = image_create_info.format,
      .fLevelCount = image_create_info.mipLevels,
  };

  GrBackendTexture backend_texture(getInfo().width(), getInfo().height(),
                                   gr_texture_info);

  // Get a Skia texture.
  auto tex = context->resourceProvider()->wrapBackendTexture(
      backend_texture,             // backend texture
      kTopLeft_GrSurfaceOrigin,    // origin
      kNone_GrBackendTextureFlag,  // flags
      0,                           // sample count
      kBorrow_GrWrapOwnership      // wrap ownership
      );

  if (!tex) {
    FTL_LOG(ERROR) << "Could not create GrTexture.";
    return nullptr;
  }

  // Create a TextureInfo object to store Vulkan handles and a reference to
  // |shared_vmo_|. We need to store a reference to |shared_vmo_| as long as the
  // texture is alive.
  TextureInfo* texture_info =
      CreateAndStoreTextureInfoGlobally(vk_device, vk_image, mem, shared_vmo_);

  // When Skia is done with the image, |ReleaseTexture| is called and clears
  // Vulkan resources. The associated TextureInfo is also destroyed, which
  // releases a reference to the shared_vmo_.
  tex->setRelease(&ReleaseTexture, texture_info);

  // More wrapping.
  return GrSurfaceProxy::MakeWrapped(tex);
}

}  // namespace mozart
