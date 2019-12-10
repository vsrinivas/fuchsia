// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_GFX_RESOURCES_IMAGE_PIPE2_H_
#define SRC_UI_SCENIC_LIB_GFX_RESOURCES_IMAGE_PIPE2_H_

#include <fuchsia/images/cpp/fidl.h>
#include <lib/zx/event.h>

#include <map>
#include <queue>

#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/memory/weak_ptr.h"
#include "src/ui/scenic/lib/gfx/resources/image.h"
#include "src/ui/scenic/lib/gfx/resources/image_pipe2_handler.h"
#include "src/ui/scenic/lib/gfx/resources/image_pipe_base.h"
#include "src/ui/scenic/lib/gfx/resources/resource.h"

namespace escher {
class BatchGpuUploader;
}  // namespace escher

namespace scenic_impl::gfx {

class ImagePipe2 : public ImagePipeBase {
 public:
  ImagePipe2(Session* session, ResourceId id,
             ::fidl::InterfaceRequest<fuchsia::images::ImagePipe2> request,
             std::shared_ptr<ImagePipeUpdater> image_pipe_updater,
             std::shared_ptr<ErrorReporter> error_reporter);
  virtual ~ImagePipe2();

  static const ResourceTypeInfo kTypeInfo;

  // fuchsia::images::ImagePipe2 implementation
  void AddBufferCollection(
      uint32_t buffer_collection_id,
      ::fidl::InterfaceHandle<::fuchsia::sysmem::BufferCollectionToken> buffer_collection_token);

  // fuchsia::images::ImagePipe2 implementation
  void AddImage(uint32_t image_id, uint32_t buffer_collection_id, uint32_t buffer_collection_index,
                ::fuchsia::sysmem::ImageFormat_2 image_format);

  // fuchsia::images::ImagePipe2 implementation
  void RemoveBufferCollection(uint32_t buffer_collection_id);

  // fuchsia::images::ImagePipe2 implementation
  void RemoveImage(uint32_t image_id);

  // fuchsia::images::ImagePipe2 implementation
  void PresentImage(uint32_t image_id, zx::time presentation_time,
                    std::vector<::zx::event> acquire_fences,
                    std::vector<::zx::event> release_fences, PresentImageCallback callback);

  // ImagePipeBase implementation
  ImagePipeUpdateResults Update(escher::ReleaseFenceSignaller* release_fence_signaller,
                                zx::time presentation_time) override;

  // ImageBase implementation
  // Updates the Escher image to the current frame. This should be called after
  // Update() indicates the current Image changed, and before calling
  // GetEscherImage().
  void UpdateEscherImage(escher::BatchGpuUploader* gpu_uploader,
                         escher::ImageLayoutUpdater* layout_updater) override;

  // ImageBase implementation
  // Returns the image that should be presented at the current time. Can be
  // null.
  const escher::ImagePtr& GetEscherImage() override;

  bool use_protected_memory() override { return num_protected_images_ > 0; }

  ImagePtr current_image() const { return current_image_; }

 protected:
  // |BufferCollectionInfo| stores the information regarding BufferCollection added.
  using BufferCollectionId = uint32_t;
  struct BufferCollectionInfo {
    // Points to BufferCollection object to communicate with Sysmem.
    fuchsia::sysmem::BufferCollectionSyncPtr buffer_collection_ptr;
    // Used to set constraints on for VkImage.
    vk::BufferCollectionFUCHSIA vk_buffer_collection;
    // Info of |buffer_collection_ptr|.
    fuchsia::sysmem::BufferCollectionInfo_2 buffer_collection_info;
    // Set of Images associated with this BufferCollection.
    std::set<ResourceId> images;
  };

  // Virtual functions are wrapping function calls for tests without vulkan support.
  virtual bool SetBufferCollectionConstraints(
      Session* session, fuchsia::sysmem::BufferCollectionTokenSyncPtr token,
      const vk::ImageCreateInfo& create_info,
      vk::BufferCollectionFUCHSIA* out_buffer_collection_fuchsia);
  virtual void DestroyBufferCollection(Session* session,
                                       const vk::BufferCollectionFUCHSIA& vk_buffer_collection);
  virtual ImagePtr CreateImage(Session* session, ResourceId image_id,
                               const ImagePipe2::BufferCollectionInfo& info,
                               uint32_t buffer_collection_index,
                               const ::fuchsia::sysmem::ImageFormat_2& image_format);

  // Called when we want to close the connection ourselves. Cleans up resources and schedules a new
  // frame update.
  void CloseConnectionAndCleanUp();

 private:
  friend class ImagePipe2Handler;

  // Called when the image pipe connection is closed.
  void OnConnectionError();

  // A |Frame| stores the arguments passed to a particular invocation of Present().
  struct Frame {
    ImagePtr image;
    zx::time presentation_time;
    std::unique_ptr<escher::FenceSetListener> acquire_fences;
    ::fidl::VectorPtr<zx::event> release_fences;

    // Callback to report when the update has been applied in response to an invocation of
    // |ImagePipe.PresentImage()|.
    PresentImageCallback present_image_callback;
  };
  std::queue<Frame> frames_;
  std::unique_ptr<ImagePipe2Handler> handler_;

  Session* const session_;
  ResourceId current_image_id_ = 0;
  ImagePtr current_image_;
  ::fidl::VectorPtr<zx::event> current_release_fences_;

  std::unordered_map<BufferCollectionId, BufferCollectionInfo> buffer_collections_;
  std::unordered_map<ResourceId, ImagePtr> images_;

  const std::shared_ptr<ImagePipeUpdater> image_pipe_updater_;
  fuchsia::sysmem::AllocatorSyncPtr sysmem_allocator_;
  const std::shared_ptr<ErrorReporter> error_reporter_;
  uint32_t num_protected_images_ = 0;

  fxl::WeakPtrFactory<ImagePipe2> weak_ptr_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ImagePipe2);
};

}  // namespace scenic_impl::gfx

#endif  // SRC_UI_SCENIC_LIB_GFX_RESOURCES_IMAGE_PIPE2_H_
