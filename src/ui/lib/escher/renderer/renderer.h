// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_RENDERER_RENDERER_H_
#define SRC_UI_LIB_ESCHER_RENDERER_RENDERER_H_

#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/memory/ref_counted.h"
#include "src/ui/lib/escher/forward_declarations.h"
#include "src/ui/lib/escher/renderer/frame.h"
#include "src/ui/lib/escher/renderer/semaphore.h"
#include "src/ui/lib/escher/vk/vulkan_context.h"

namespace escher {

class Renderer : public fxl::RefCountedThreadSafe<Renderer> {
 public:
  const VulkanContext& vulkan_context() { return context_; }

  Escher* escher() const { return escher_.get(); }
  EscherWeakPtr GetEscherWeakPtr() { return escher_; }

 protected:
  explicit Renderer(EscherWeakPtr escher);
  virtual ~Renderer();

  // Basic struct for containing the data a renderer needs to render
  // a given frame. Data that is reusuable amongst different renderer
  // subclasses are stored here. Each renderer can also extend this
  // struct to include any additional data they may need.
  struct FrameData {
    FrameData(const FramePtr& frame_in, std::shared_ptr<BatchGpuUploader> gpu_uploader_in,
              const ImagePtr& output_image_in,
              std::pair<TexturePtr, TexturePtr> depth_and_msaa_textures);
    virtual ~FrameData();
    FramePtr frame;
    ImagePtr output_image;
    TexturePtr depth_texture;
    TexturePtr msaa_texture;
    std::shared_ptr<BatchGpuUploader> gpu_uploader;
  };

  // Called in BeginFrame() to obtain suitable render targets.
  // NOTE: call only once per frame.
  std::pair<TexturePtr, TexturePtr> ObtainDepthAndMsaaTextures(const FramePtr& frame,
                                                               const ImageInfo& info,
                                                               uint32_t msaa_sample_count,
                                                               vk::Format depth_stencil_format);

  // Handles the logic for setting up a vulkan render pass. If there are MSAA buffers a resolve
  // subpass is also added. Clear color is set to black and if the frame has a depth texture that
  // will also be used. This is general enough to meet most standard needs but if a client wants
  // something that is not handled here they will have to implement their own render pass function.
  static void InitRenderPassInfo(RenderPassInfo* rp, ImageViewAllocator* allocator,
                                 const FrameData& frame_data, vk::Rect2D render_area);

  const VulkanContext context_;
  std::vector<TexturePtr> depth_buffers_;
  std::vector<TexturePtr> msaa_buffers_;

 private:
  const EscherWeakPtr escher_;

  FRIEND_REF_COUNTED_THREAD_SAFE(Renderer);
  FXL_DISALLOW_COPY_AND_ASSIGN(Renderer);
};

}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_RENDERER_RENDERER_H_
