// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_FLATLAND_RENDERER_GPU_MEM_H_
#define SRC_UI_SCENIC_LIB_FLATLAND_RENDERER_GPU_MEM_H_

#include <optional>

#include "src/ui/lib/escher/vk/gpu_mem.h"
#include "src/ui/scenic/lib/flatland/buffers/buffer_collection.h"

namespace flatland {

// Class containing the GPU and Vulkan related data necessary to create a vulkan image
// from a sysmem buffer collection.
class GpuImageInfo {
 public:
  // Generates an GpuImageInfo struct containing all of the relevant information required
  // to make a vk::Image. This involves importing the BufferCollectionInfo |collection|'s
  // vmo at |index| into GPU memory, which is returned in the |mem| member of GpuImageInfo.
  // This requires |collection| to already be allocated, and will not wait for an allocation.
  static GpuImageInfo New(const vk::Device& device, const vk::DispatchLoaderDynamic& vk_loader,
                          const fuchsia::sysmem::BufferCollectionInfo_2& info,
                          const vk::BufferCollectionFUCHSIA& vk_buffer_collection, uint32_t index);

  // Default constructor which does not initialize any of the member variables. A fully initialized
  // instance of this class must be created with |New|.
  GpuImageInfo() = default;

  // Wrapper around the vk::Device memory used to create the vk::Image. This
  // is created from the collections vmo which is imported to the gpu.
  escher::GpuMemPtr GetGpuMem() const { return mem_; }

  // Required extension for creating images from sysmem buffer collections.
  std::optional<vk::BufferCollectionImageCreateInfoFUCHSIA> p_extension() const {
    return p_extension_;
  }

  // Returns the data required to create a vk::Image. If |p_extension| has data it
  // will be passed into |setPNext()| so Vulkan is aware of the extension data. If
  // |is_secure| is true, vk::ImageCreateFlagBits::eProtected will be set on the flags.
  // The image created with this vk::ImageCreateInfo should use the calling GpuImageInfo
  // instance's escher::GpuMemPtr due to this vk::ImageCreateInfo being created with
  // the vk::BufferCollectionImageCreateInfoFUCHSIA extension from this class.
  //
  // This class (GpuImageInfo) must be kept alive as long as images created from it are
  // in use, since this class holds the vk memory that is being used to back the images.
  // So it must stay alive so that we do not run into use-after-free errors.
  vk::ImageCreateInfo NewVkImageCreateInfo(uint32_t width, uint32_t height,
                                           vk::ImageUsageFlags usage) const;

 private:
  GpuImageInfo(escher::GpuMemPtr mem, vk::BufferCollectionFUCHSIA vk_buffer_collection,
               uint32_t vmo_index, bool is_protected);

  std::optional<vk::BufferCollectionImageCreateInfoFUCHSIA> p_extension_ = std::nullopt;
  escher::GpuMemPtr mem_ = nullptr;

  // Used for protected memory.
  bool is_protected_ = false;
};

}  // namespace flatland

#endif  // SRC_UI_SCENIC_LIB_FLATLAND_RENDERER_GPU_MEM_H_
