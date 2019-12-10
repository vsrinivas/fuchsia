// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/resources/image_pipe.h"

#include <trace/event.h>

#include "src/ui/lib/escher/flib/fence.h"
#include "src/ui/scenic/lib/gfx/engine/session.h"
#include "src/ui/scenic/lib/gfx/resources/memory.h"
#include "src/ui/scenic/lib/gfx/util/time.h"
#include "src/ui/scenic/lib/scheduling/frame_scheduler.h"

namespace scenic_impl {
namespace gfx {

const ResourceTypeInfo ImagePipe::kTypeInfo = {ResourceType::kImagePipe | ResourceType::kImageBase,
                                               "ImagePipe"};

ImagePipe::ImagePipe(Session* session, ResourceId id,
                     std::shared_ptr<ImagePipeUpdater> image_pipe_updater,
                     std::shared_ptr<ErrorReporter> error_reporter)
    : ImagePipeBase(session, id, ImagePipe::kTypeInfo),
      image_pipe_updater_(std::move(image_pipe_updater)),
      error_reporter_(std::move(error_reporter)),
      weak_ptr_factory_(this) {
  FXL_CHECK(image_pipe_updater_);
  FXL_CHECK(error_reporter_);
}

ImagePipe::ImagePipe(Session* session, ResourceId id,
                     fidl::InterfaceRequest<fuchsia::images::ImagePipe> request,
                     std::shared_ptr<ImagePipeUpdater> image_pipe_updater,
                     std::shared_ptr<ErrorReporter> error_reporter)
    : ImagePipeBase(session, id, ImagePipe::kTypeInfo),
      handler_(std::make_unique<ImagePipeHandler>(std::move(request), this)),
      image_pipe_updater_(std::move(image_pipe_updater)),
      error_reporter_(std::move(error_reporter)),
      weak_ptr_factory_(this) {
  FXL_CHECK(image_pipe_updater_);
  FXL_CHECK(error_reporter_);
}

void ImagePipe::AddImage(uint32_t image_id, fuchsia::images::ImageInfo image_info, zx::vmo vmo,
                         uint64_t offset_bytes, uint64_t size_bytes,
                         fuchsia::images::MemoryType memory_type) {
  if (image_id == 0) {
    error_reporter_->ERROR() << "ImagePipe::AddImage: Image can not be assigned an ID of 0.";
    CloseConnectionAndCleanUp();
    return;
  }
  uint64_t vmo_size;
  auto status = vmo.get_size(&vmo_size);

  if (status != ZX_OK) {
    error_reporter_->ERROR() << "ImagePipe::AddImage(): zx_vmo_get_size failed (err=" << status
                             << ").";
    CloseConnectionAndCleanUp();
    return;
  }
  ::fuchsia::ui::gfx::MemoryArgs memory_args;
  memory_args.memory_type = memory_type;
  memory_args.vmo = std::move(vmo);
  memory_args.allocation_size = vmo_size;
  MemoryPtr memory =
      Memory::New(session_DEPRECATED(), 0u, std::move(memory_args), error_reporter_.get());
  if (!memory) {
    error_reporter_->ERROR() << "ImagePipe::AddImage: Unable to create a memory object.";
    CloseConnectionAndCleanUp();
    return;
  }
  auto image = CreateImage(session_DEPRECATED(), image_id, memory, image_info, offset_bytes);
  auto result = images_.insert({image_id, std::move(image)});
  if (!result.second) {
    error_reporter_->ERROR() << "ImagePipe::AddImage(): resource with ID " << image_id
                             << " already exists.";
    CloseConnectionAndCleanUp();
    return;
  }
};

void ImagePipe::CloseConnectionAndCleanUp() {
  handler_.reset();
  is_valid_ = false;
  frames_ = {};
  images_.clear();

  // Schedule a new frame.
  image_pipe_updater_->ScheduleImagePipeUpdate(zx::time(0), fxl::WeakPtr<ImagePipeBase>());
}

void ImagePipe::OnConnectionError() { CloseConnectionAndCleanUp(); }

ImagePtr ImagePipe::CreateImage(Session* session, ResourceId id, MemoryPtr memory,
                                const fuchsia::images::ImageInfo& image_info,
                                uint64_t memory_offset) {
  return Image::New(session, id, memory, image_info, memory_offset, error_reporter_.get());
}

void ImagePipe::RemoveImage(uint32_t image_id) {
  TRACE_DURATION("gfx", "ImagePipe::RemoveImage", "image_id", image_id);
  size_t erased_count = images_.erase(image_id);
  if (erased_count == 0) {
    error_reporter_->ERROR() << "ImagePipe::RemoveImage(): Could not find image with id="
                             << image_id << ".";
    CloseConnectionAndCleanUp();
  }
};

void ImagePipe::PresentImage(uint32_t image_id, zx::time presentation_time,
                             std::vector<zx::event> acquire_fences,
                             std::vector<zx::event> release_fences,
                             fuchsia::images::ImagePipe::PresentImageCallback callback) {
  TRACE_DURATION("gfx", "ImagePipe::PresentImage", "image_id", image_id);
  TRACE_FLOW_END("gfx", "image_pipe_present_image", image_id);

  if (!frames_.empty() && presentation_time < frames_.back().presentation_time) {
    error_reporter_->ERROR()
        << "ImagePipe: Present called with out-of-order presentation time. presentation_time="
        << presentation_time
        << ", last scheduled presentation time=" << frames_.back().presentation_time;
    CloseConnectionAndCleanUp();
    return;
  }

  // Verify that image_id is valid.
  auto image_it = images_.find(image_id);
  if (image_it == images_.end()) {
    error_reporter_->ERROR() << "ImagePipe::PresentImage could not find Image with ID: "
                             << image_id;
    CloseConnectionAndCleanUp();
    return;
  }

  auto acquire_fences_listener =
      std::make_unique<escher::FenceSetListener>(std::move(acquire_fences));
  acquire_fences_listener->WaitReadyAsync(
      [weak = weak_ptr_factory_.GetWeakPtr(), presentation_time] {
        if (weak) {
          weak->image_pipe_updater_->ScheduleImagePipeUpdate(presentation_time, weak);
        }
      });
  TRACE_FLOW_BEGIN("gfx", "image_pipe_present_image_to_update", image_id);
  frames_.push(Frame{image_it->second, presentation_time, std::move(acquire_fences_listener),
                     fidl::VectorPtr(std::move(release_fences)), std::move(callback)});
};

ImagePipeUpdateResults ImagePipe::Update(escher::ReleaseFenceSignaller* release_fence_signaller,
                                         zx::time presentation_time) {
  FXL_DCHECK(release_fence_signaller);

  ImagePipeUpdateResults results{.image_updated = false};

  bool present_next_image = false;
  ResourceId next_image_id = current_image_id_;
  fidl::VectorPtr<zx::event> next_release_fences;

  ImagePtr next_image = nullptr;
  while (!frames_.empty() && frames_.front().presentation_time <= presentation_time &&
         frames_.front().acquire_fences->ready()) {
    if (next_image) {
      // We're skipping a frame, so we should also mark the image as dirty, in
      // case the producer updates the pixels in the buffer between now and a
      // future present call.
      next_image->MarkAsDirty();
    }

    next_image = frames_.front().image;
    FXL_DCHECK(next_image);
    next_image_id = next_image->id();

    if (next_release_fences.has_value() && !next_release_fences->empty()) {
      // We're skipping a frame, so we can immediately signal its release
      // fences.
      for (auto& fence : *next_release_fences) {
        fence.signal(0u, escher::kFenceSignalled);
      }
    }

    next_release_fences = std::move(frames_.front().release_fences);

    results.callbacks.push_back(std::move(frames_.front().present_image_callback));
    TRACE_FLOW_END("gfx", "image_pipe_present_image_to_update", next_image_id);
    frames_.pop();
    present_next_image = true;
  }

  if (!present_next_image) {
    results.image_updated = false;
    return results;
  }

  // TODO(SCN-151): This code, and the code below that marks an image as dirty,
  // assumes that the same image cannot be presented twice in a row on the same
  // image pipe, while also requiring a call to UpdatePixels(). If not, this
  // needs a new test.
  if (next_image_id == current_image_id_) {
    // This ImagePipe did not change since the last frame was rendered.
    results.image_updated = false;
    return results;
  }

  // We're replacing a frame with a new one, so we hand off its release
  // fence to the |ReleaseFenceSignaller|, which will signal it as soon as
  // all work previously submitted to the GPU is finished.
  if (current_release_fences_) {
    release_fence_signaller->AddCPUReleaseFences(std::move(current_release_fences_));
  }
  current_release_fences_ = std::move(next_release_fences);
  current_image_id_ = next_image_id;
  // TODO(SCN-1010): Determine proper signaling for marking images as dirty.
  // For now, mark all released images as dirty, with the assumption that the
  // client will likely write into the buffer before submitting it again.
  if (current_image_) {
    current_image_->MarkAsDirty();
  }
  current_image_ = std::move(next_image);

  results.image_updated = true;
  return results;
}

void ImagePipe::UpdateEscherImage(escher::BatchGpuUploader* gpu_uploader,
                                  escher::ImageLayoutUpdater* layout_updater) {
  // Update current escher Image.
  if (current_image_) {
    current_image_->UpdateEscherImage(gpu_uploader, layout_updater);
  }
}

const escher::ImagePtr& ImagePipe::GetEscherImage() {
  if (current_image_) {
    return current_image_->GetEscherImage();
  }
  static const escher::ImagePtr kNullEscherImage;
  return kNullEscherImage;
}

}  // namespace gfx
}  // namespace scenic_impl
