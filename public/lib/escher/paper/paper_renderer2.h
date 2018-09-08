// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_PAPER_PAPER_RENDERER2_H_
#define LIB_ESCHER_PAPER_PAPER_RENDERER2_H_

#include "lib/escher/forward_declarations.h"
#include "lib/escher/paper/paper_render_queue.h"
#include "lib/escher/paper/paper_shape_cache.h"
#include "lib/escher/renderer/renderer.h"
#include "lib/fxl/logging.h"

namespace escher {

class PaperRenderer2;
using PaperRenderer2Ptr = fxl::RefPtr<PaperRenderer2>;

// PaperRenderer builds atop recently-introduced Escher functionality
// (e.g. CommandBuffer, RenderQueue, ShaderProgram) to support rendering of the
// scenes currently supported by Scenic.
class PaperRenderer2 : public Renderer {
 public:
  static PaperRenderer2Ptr New(EscherWeakPtr escher);

  // Begin rendering a "paper" scene into |output_image|.  This sets up some
  // state, but generates no Vulkan commands.  After calling BeginFrame(), the
  // client uses shape_cache() and render_queue() to specify the scene that is
  // to be drawn (this is temporary: soon the PaperRenderer2 will add methods
  // for adding scene content, and the cache and render queue will be private).
  // Finally, the client calls EndFrame() to sort the scene content and emit
  // Vulkan commands to render it.
  void BeginFrame(const FramePtr& frame, escher::Stage* stage,
                  const escher::Camera& camera,
                  const escher::ImagePtr& output_image);

  // See BeginFrame().  After populating the scene content, clients call
  // EndFrame() to emit Vulkan commands to render the content into
  // |output_image|.
  void EndFrame();

  ~PaperRenderer2() override;

  // Set the number of depth images that the renderer should round-robin
  // through.
  void SetNumDepthBuffers(size_t count);

  PaperShapeCache* shape_cache() {
    FXL_DCHECK(frame_data_) << "Can only access shape_cache() during frame.";
    return &shape_cache_;
  }

  PaperRenderQueue* render_queue() {
    FXL_DCHECK(frame_data_) << "Can only access render_queue() during frame.";
    return &render_queue_;
  }

 protected:
  explicit PaperRenderer2(EscherWeakPtr escher);

 private:
  void BeginRenderPass(const FramePtr& frame, const ImagePtr& output_image);
  void EndRenderPass(const FramePtr& frame);

  PaperShapeCache shape_cache_;
  PaperRenderQueue render_queue_;
  std::vector<TexturePtr> depth_buffers_;

  struct FrameData {
    FrameData(const FramePtr& frame, const ImagePtr& output_image);
    ~FrameData();
    FramePtr frame;
    ImagePtr output_image;
    BatchGpuUploaderPtr gpu_uploader;
  };
  std::unique_ptr<FrameData> frame_data_;
};

}  // namespace escher

#endif  // LIB_ESCHER_PAPER_PAPER_RENDERER2_H_
