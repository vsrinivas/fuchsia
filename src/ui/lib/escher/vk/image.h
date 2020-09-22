// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_VK_IMAGE_H_
#define SRC_UI_LIB_ESCHER_VK_IMAGE_H_

#include "src/ui/lib/escher/forward_declarations.h"
#include "src/ui/lib/escher/renderer/semaphore.h"
#include "src/ui/lib/escher/resources/resource.h"
#include "src/ui/lib/escher/util/debug_print.h"

namespace escher {

// Full description of the size and layout of an Image.
#pragma pack(push, 1)  // As required by escher::HashMapHasher<ImageInfo>
struct ImageInfo {
  vk::Format format = vk::Format::eUndefined;
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t sample_count = 1;
  vk::ImageUsageFlags usage;
  vk::MemoryPropertyFlags memory_flags = vk::MemoryPropertyFlagBits::eDeviceLocal;
  vk::ImageTiling tiling = vk::ImageTiling::eOptimal;
  bool is_mutable = false;
  bool is_external = false;

  bool operator==(const ImageInfo& other) const {
    return format == other.format && width == other.width && height == other.height &&
           sample_count == other.sample_count && usage == other.usage &&
           memory_flags == other.memory_flags && tiling == other.tiling &&
           is_mutable == other.is_mutable && is_external == other.is_external;
  }

  // Transient images are neither loaded nor stored by render passes.  Instead
  // they may be rendered into by one subpass and used as an input attachment by
  // a subsequent pass.  Consequently, it may be possible (not implemented yet,
  // and depending on the hardware/driver) to avoid allocating any memory for
  // such images, so that it exists only transiently in tile-local storage.
  bool is_transient() const {
    return static_cast<bool>(usage & vk::ImageUsageFlagBits::eTransientAttachment);
  }
};
#pragma pack(pop)

// An Image is a Resource that encapsulates a vk::Image.
class Image : public Resource {
 public:
  static const ResourceTypeInfo kTypeInfo;
  const ResourceTypeInfo& type_info() const override { return kTypeInfo; }

  // Constructor. Wraps an existing image without claiming ownership. Useful
  // when the image is owned/maintained by another system (e.g.,
  // vk::SwapchainKHR).
  static ImagePtr WrapVkImage(ResourceManager* image_owner, ImageInfo info, vk::Image image,
                              vk::ImageLayout initial_layout);

  const ImageInfo& info() const { return info_; }
  vk::Image vk() const { return image_; }
  vk::Format format() const { return info_.format; }
  // TODO(fxbug.dev/7173): decide whether to deprecate format().
  vk::Format vk_format() const { return info_.format; }
  uint32_t width() const { return info_.width; }
  uint32_t height() const { return info_.height; }
  bool has_depth() const { return has_depth_; }
  bool has_stencil() const { return has_stencil_; }
  bool is_transient() const { return info_.is_transient(); }
  vk::DeviceSize size() const { return size_; }
  uint8_t* host_ptr() const { return host_ptr_; }

  // TODO(fxbug.dev/7174): how does this interact with swapchain_layout_?
  // Should it be locked so that the layout can't be changed during a render-pass
  // where it is used as an attachment?

  vk::ImageLayout layout() const { return layout_; }

  bool is_layout_initialized() const {
    return layout() != vk::ImageLayout::eUndefined && layout() != vk::ImageLayout::ePreinitialized;
  }

  // Specify the layout that should be transitioned to when this image is used
  // as a framebuffer attachment.
  void set_swapchain_layout(vk::ImageLayout layout) { swapchain_layout_ = layout; }
  vk::ImageLayout swapchain_layout() const { return swapchain_layout_; }
  bool is_swapchain_image() const { return swapchain_layout_ != vk::ImageLayout::eUndefined; }
  bool use_protected_memory() const {
    return static_cast<bool>(info_.memory_flags & vk::MemoryPropertyFlagBits::eProtected);
  }

 protected:
  // Constructor. In some cases it is necessary to wrap an un-owned vk::Image,
  // which should not be destroyed when this Image is destroyed (e.g. when
  // working with images associated with a vk::SwapchainKHR); this is done by
  // passing nullptr as the |mem| argument.
  Image(ResourceManager* image_owner, ImageInfo info, vk::Image image, vk::DeviceSize size_,
        uint8_t* host_ptr_, vk::ImageLayout initial_layout);

 private:
  friend class CommandBuffer;
  friend class impl::CommandBuffer;

  // Set the image layout stored in |layout_|. This should be only used by
  // friend classes like |CommandBuffer| or |impl::CommandBuffer|
  // when they made transitions to image layouts, e.g. when we invoke
  // |TransitionImageLayout| to manually do layout transitions in
  // BatchGpuUploader.
  void set_layout(vk::ImageLayout new_layout) { layout_ = new_layout; }

  const ImageInfo info_;
  const vk::Image image_;
  const bool has_depth_;
  const bool has_stencil_;
  const vk::DeviceSize size_;
  uint8_t* const host_ptr_;

  // TODO(fxbug.dev/7174): consider allowing image to have an initial layout.
  vk::ImageLayout layout_ = vk::ImageLayout::eUndefined;

  vk::ImageLayout swapchain_layout_ = vk::ImageLayout::eUndefined;

  FXL_DISALLOW_COPY_AND_ASSIGN(Image);
};

typedef fxl::RefPtr<Image> ImagePtr;

// Debugging.
ESCHER_DEBUG_PRINTABLE(ImageInfo);

}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_VK_IMAGE_H_
