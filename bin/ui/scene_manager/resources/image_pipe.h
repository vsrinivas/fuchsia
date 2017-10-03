// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zx/event.h>
#include <zx/vmo.h>

#include <queue>

#include "garnet/bin/ui/scene_manager/engine/resource_map.h"
#include "garnet/bin/ui/scene_manager/resources/image.h"
#include "garnet/bin/ui/scene_manager/resources/image_base.h"
#include "garnet/bin/ui/scene_manager/resources/image_pipe.h"
#include "garnet/bin/ui/scene_manager/resources/image_pipe_handler.h"
#include "garnet/bin/ui/scene_manager/resources/resource.h"
#include "garnet/bin/ui/scene_manager/sync/acquire_fence.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fsl/tasks/message_loop_handler.h"
#include "lib/fxl/memory/weak_ptr.h"
#include "lib/images/fidl/image_pipe.fidl.h"

namespace scene_manager {

class ImagePipe;
using ImagePipePtr = fxl::RefPtr<ImagePipe>;

class ImagePipe : public ImageBase {
 public:
  static const ResourceTypeInfo kTypeInfo;

  ImagePipe(Session* session, scenic::ResourceId id);
  ImagePipe(Session* session,
            scenic::ResourceId id,
            ::fidl::InterfaceRequest<scenic::ImagePipe> request);

  // Called by |ImagePipeHandler|, part of |ImagePipe| interface.
  void AddImage(uint32_t image_id,
                scenic::ImageInfoPtr image_info,
                zx::vmo memory,
                scenic::MemoryType memory_type,
                uint64_t memory_offset);
  void RemoveImage(uint32_t image_id);

  void PresentImage(uint32_t image_id,
                    uint64_t presentation_time,
                    zx::event acquire_fence,
                    zx::event release_fence,
                    const scenic::ImagePipe::PresentImageCallback& callback);

  void Accept(class ResourceVisitor* visitor) override;

  // Update to use the most current frame for the specified presentation time.
  // Called before rendering a frame using this ImagePipe.  Return true if the
  // current Image changed since the last time Update() was called, and false
  // otherwise.
  bool Update(uint64_t presentation_time, uint64_t presentation_interval);

  // Returns the image that should be presented at the current time. Can be
  // null.
  const escher::ImagePtr& GetEscherImage() override;

  // Returns true if the connection to the ImagePipe has not closed.
  bool is_valid() { return is_valid_; };

 private:
  friend class ImagePipeHandler;

  // Called when the image pipe connection is closed.
  void OnConnectionError();

  // Called when we want to close the connection ourselves. Cleans up resources
  // and schedules a new frame update.
  void CloseConnectionAndCleanUp();

  // Virtual so that test subclasses can override.
  virtual ImagePtr CreateImage(Session* session,
                               MemoryPtr memory,
                               const scenic::ImageInfoPtr& image_info,
                               uint64_t memory_offset,
                               ErrorReporter* error_reporter);

  fxl::WeakPtrFactory<ImagePipe> weak_ptr_factory_;

  // A |Frame| stores the arguments passed to a particular invocation of
  // Present().
  struct Frame {
    scenic::ResourceId image_id;
    uint64_t presentation_time;
    std::unique_ptr<AcquireFence> acquire_fence;
    zx::event release_fence;

    // Callback to report when the update has been applied in response to
    // an invocation of |ImagePipe.PresentImage()|.
    scenic::ImagePipe::PresentImageCallback present_image_callback;
  };
  std::queue<Frame> frames_;
  std::unique_ptr<ImagePipeHandler> handler_;

  scenic::ResourceId current_image_id_ = 0;
  ImagePtr current_image_;
  zx::event current_release_fence_;

  ResourceMap images_;
  bool is_valid_ = true;

  FXL_DISALLOW_COPY_AND_ASSIGN(ImagePipe);
};

}  // namespace scene_manager
