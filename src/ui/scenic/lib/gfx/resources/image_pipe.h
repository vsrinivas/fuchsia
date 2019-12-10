// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_GFX_RESOURCES_IMAGE_PIPE_H_
#define SRC_UI_SCENIC_LIB_GFX_RESOURCES_IMAGE_PIPE_H_

#include <fuchsia/images/cpp/fidl.h>
#include <lib/zx/event.h>
#include <lib/zx/vmo.h>

#include <queue>

#include "src/lib/fxl/memory/weak_ptr.h"
#include "src/ui/lib/escher/flib/fence_set_listener.h"
#include "src/ui/scenic/lib/gfx/resources/image.h"
#include "src/ui/scenic/lib/gfx/resources/image_pipe_base.h"
#include "src/ui/scenic/lib/gfx/resources/image_pipe_handler.h"
#include "src/ui/scenic/lib/gfx/resources/resource.h"

namespace escher {
class BatchGpuUploader;
class ImageLayoutUpdater;
}  // namespace escher

namespace scenic_impl {
namespace gfx {

class ImagePipe;
using ImagePipePtr = fxl::RefPtr<ImagePipe>;
using PresentImageCallback = fuchsia::images::ImagePipe::PresentImageCallback;

class ImagePipe : public ImagePipeBase {
 public:
  static const ResourceTypeInfo kTypeInfo;

  ImagePipe(Session* session, ResourceId id, std::shared_ptr<ImagePipeUpdater> image_pipe_updater,
            std::shared_ptr<ErrorReporter> error_reporter);
  ImagePipe(Session* session, ResourceId id,
            fidl::InterfaceRequest<fuchsia::images::ImagePipe> request,
            std::shared_ptr<ImagePipeUpdater> image_pipe_updater,
            std::shared_ptr<ErrorReporter> error_reporter);

  // Called by |ImagePipeHandler|, part of |ImagePipe| interface.
  void AddImage(uint32_t image_id, fuchsia::images::ImageInfo image_info, zx::vmo memory,
                uint64_t offset_bytes, uint64_t size_bytes,
                fuchsia::images::MemoryType memory_type);
  void RemoveImage(uint32_t image_id);

  void PresentImage(uint32_t image_id, zx::time presentation_time,
                    std::vector<zx::event> acquire_fences, std::vector<zx::event> release_fences,
                    fuchsia::images::ImagePipe::PresentImageCallback callback);

  // ImagePipeBase implementation
  ImagePipeUpdateResults Update(escher::ReleaseFenceSignaller* release_fence_signaller,
                                zx::time presentation_time) override;

  // Updates the Escher image to the current frame. This should be called after
  // Update() indicates the current Image changed, and before calling
  // GetEscherImage().
  void UpdateEscherImage(escher::BatchGpuUploader* gpu_uploader,
                         escher::ImageLayoutUpdater* layout_updater) override;

  // Returns the image that should be presented at the current time. Can be
  // null.
  const escher::ImagePtr& GetEscherImage() override;

  bool use_protected_memory() override { return false; }

  // Returns true if the connection to the ImagePipe has not closed.
  bool is_valid() { return is_valid_; };

  fxl::WeakPtr<ImagePipe> GetWeakPtr() { return weak_ptr_factory_.GetWeakPtr(); }

  ImagePtr current_image() const { return current_image_; }

 private:
  friend class ImagePipeHandler;

  // Called when the image pipe connection is closed.
  void OnConnectionError();

  // Called when we want to close the connection ourselves. Cleans up resources
  // and schedules a new frame update.
  void CloseConnectionAndCleanUp();

  // Virtual so that test subclasses can override.
  virtual ImagePtr CreateImage(Session* session, ResourceId id, MemoryPtr memory,
                               const fuchsia::images::ImageInfo& image_info,
                               uint64_t memory_offset);

  // A |Frame| stores the arguments passed to a particular invocation of
  // Present().
  struct Frame {
    ImagePtr image;
    zx::time presentation_time;
    std::unique_ptr<escher::FenceSetListener> acquire_fences;
    fidl::VectorPtr<zx::event> release_fences;

    // Callback to report when the update has been applied in response to
    // an invocation of |ImagePipe.PresentImage()|.
    PresentImageCallback present_image_callback;
  };
  std::queue<Frame> frames_;
  std::unique_ptr<ImagePipeHandler> handler_;

  ResourceId current_image_id_ = 0;
  ImagePtr current_image_;
  fidl::VectorPtr<zx::event> current_release_fences_;

  std::unordered_map<ResourceId, ImagePtr> images_;
  bool is_valid_ = true;

  const std::shared_ptr<ImagePipeUpdater> image_pipe_updater_;
  const std::shared_ptr<ErrorReporter> error_reporter_;

  fxl::WeakPtrFactory<ImagePipe> weak_ptr_factory_;  // must be last

  FXL_DISALLOW_COPY_AND_ASSIGN(ImagePipe);
};

}  // namespace gfx
}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_LIB_GFX_RESOURCES_IMAGE_PIPE_H_
