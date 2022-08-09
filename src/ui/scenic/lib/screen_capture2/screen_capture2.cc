// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "screen_capture2.h"

#include <lib/async/cpp/wait.h>
#include <lib/async/default.h>
#include <lib/syslog/cpp/macros.h>

#include <optional>
#include <utility>

#include "src/lib/fsl/handles/object_info.h"

using fuchsia::ui::composition::internal::FrameInfo;
using fuchsia::ui::composition::internal::ScreenCaptureConfig;
using fuchsia::ui::composition::internal::ScreenCaptureError;
using std::vector;

namespace screen_capture2 {

ScreenCapture::ScreenCapture(std::shared_ptr<screen_capture::ScreenCaptureBufferCollectionImporter>
                                 screen_capture_buffer_collection_importer,
                             std::shared_ptr<flatland::Renderer> renderer,
                             GetRenderables get_renderables)
    : screen_capture_buffer_collection_importer_(screen_capture_buffer_collection_importer),
      renderer_(renderer),
      get_renderables_(std::move(get_renderables)),
      weak_factory_(this) {}

ScreenCapture::~ScreenCapture() { ClearImages(); }

void ScreenCapture::Configure(ScreenCaptureConfig args, ConfigureCallback callback) {
  if (!args.has_image_size()) {
    FX_LOGS(WARNING) << "ScreenCapture::Configure: Missing image size";
    callback(fpromise::error(ScreenCaptureError::MISSING_ARGS));
    return;
  }

  if (!args.has_import_token()) {
    FX_LOGS(WARNING) << "ScreenCapture::Configure: Missing import token";
    callback(fpromise::error(ScreenCaptureError::MISSING_ARGS));
    return;
  }

  if (!args.image_size().width || !args.image_size().height) {
    FX_LOGS(WARNING) << "ScreenCapture::Configure: Invalid arguments.";
    callback(fpromise::error(ScreenCaptureError::INVALID_ARGS));
    return;
  }

  auto import_token = args.mutable_import_token();
  const zx_koid_t global_collection_id = fsl::GetRelatedKoid(import_token->value.get());

  if (global_collection_id == ZX_KOID_INVALID) {
    FX_LOGS(WARNING) << "ScreenCapture::Configure: Event pair ID must be valid.";
    callback(fpromise::error(ScreenCaptureError::INVALID_ARGS));
    return;
  }

  std::optional<BufferCount> buffer_count_opt =
      screen_capture_buffer_collection_importer_->GetBufferCollectionBufferCount(
          global_collection_id);
  if (!buffer_count_opt) {
    FX_LOGS(WARNING) << "ScreenCapture::Configure: Failed to get BufferCount.";
    callback(fpromise::error(ScreenCaptureError::INVALID_ARGS));
    return;
  }

  BufferCount buffer_count = buffer_count_opt.value();

  if (buffer_count < 0) {
    FX_LOGS(WARNING) << "ScreenCapture::Configure: There must be at least 0 buffers.";
    callback(fpromise::error(ScreenCaptureError::INVALID_ARGS));
    return;
  }

  // Release any existing buffers and reset |image_ids_| and |available_buffers_|.
  ClearImages();

  // Create the associated metadata. Note that clients are responsible for ensuring reasonable
  // parameters.
  allocation::ImageMetadata metadata;
  metadata.collection_id = global_collection_id;
  metadata.width = args.image_size().width;
  metadata.height = args.image_size().height;

  // For each buffer in the collection, add the image to the importer.
  for (uint32_t i = 0; i < buffer_count; i++) {
    metadata.identifier = allocation::GenerateUniqueImageId();
    metadata.vmo_index = i;
    auto result = screen_capture_buffer_collection_importer_->ImportBufferImage(
        metadata, allocation::BufferCollectionUsage::kRenderTarget);
    if (!result) {
      ClearImages();

      FX_LOGS(WARNING) << "ScreenCapture::Configure: Failed to import BufferImage at index " << i;
      callback(fpromise::error(ScreenCaptureError::INVALID_ARGS));
      return;
    }
    image_ids_[i] = metadata;
    available_buffers_.push_front(i);
  }

  client_received_last_frame_ = false;
  render_frame_in_progress_ = false;
  current_callback_ = std::nullopt;
  callback(fpromise::ok());
}

void ScreenCapture::GetNextFrame(ScreenCapture::GetNextFrameCallback callback) {
  if (current_callback_ != std::nullopt) {
    FX_LOGS(WARNING) << "ScreenCapture::GetNextFrame: GetNextFrame already in progress. Wait for "
                        "it to return before calling again.";
    callback(fpromise::error(ScreenCaptureError::BAD_HANGING_GET));
    return;
  }
  current_callback_ = std::move(callback);
  if (!client_received_last_frame_ && !available_buffers_.empty()) {
    MaybeRenderFrame();
  }
}

void ScreenCapture::MaybeRenderFrame() {
  if (render_frame_in_progress_) {
    return;
  }

  render_frame_in_progress_ = true;

  if (current_callback_ == std::nullopt) {
    client_received_last_frame_ = false;
    render_frame_in_progress_ = false;
    return;
  }

  if (available_buffers_.empty()) {
    FX_LOGS(WARNING) << "ScreenCapture::MaybeRenderFrame: Should ensure there are available "
                        "buffers before call.";
    client_received_last_frame_ = false;
    render_frame_in_progress_ = false;
    return;
  }

  const uint32_t buffer_index = available_buffers_.front();
  available_buffers_.pop_front();
  // Get renderables from the engine.
  auto renderables = get_renderables_();
  const auto& rects = renderables.first;
  const auto& image_metadatas = renderables.second;

  const auto& metadata = image_ids_[buffer_index];

  zx::event release_fence;
  zx_status_t status = zx::event::create(0, &release_fence);
  FX_DCHECK(status == ZX_OK);

  // Set up the |async::Wait| for call to Render() to signal release_fence. Note that we are
  // passing ownership of the callback here.
  auto wait = std::make_shared<async::WaitOnce>(release_fence.get(), ZX_EVENT_SIGNALED);
  status = wait->Begin(async_get_default_dispatcher(),
                       [copy_ref = wait, weak_ptr = weak_factory_.GetWeakPtr(), buffer_index](
                           async_dispatcher_t*, async::WaitOnce*, zx_status_t status,
                           const zx_packet_signal_t* signal) mutable {
                         FX_DCHECK(status == ZX_OK || status == ZX_ERR_CANCELED);
                         if (!weak_ptr) {
                           return;
                         }
                         weak_ptr->HandleRender(buffer_index, signal->timestamp);
                       });
  FX_DCHECK(status == ZX_OK);

  // TODO(fxbug.dev/93069): Clean up current_release_fences_ once bug is fixed.
  FX_DCHECK(current_release_fences_.empty());
  current_release_fences_.push_back(std::move(release_fence));
  // Render content into user-provided buffer, which will signal the release_fence.
  renderer_->Render(metadata, rects, image_metadatas, current_release_fences_);
}

void ScreenCapture::HandleRender(uint32_t buffer_index, uint64_t timestamp) {
  zx::eventpair buffer_release_client_token;
  zx::eventpair buffer_release_server_token;
  zx::eventpair::create(0, &buffer_release_server_token, &buffer_release_client_token);

  // Set up |async::Wait| for when client releases buffer.
  auto wait = std::make_shared<async::WaitOnce>(buffer_release_server_token.get(),
                                                ZX_EVENTPAIR_PEER_CLOSED | ZX_EVENTPAIR_SIGNALED);
  auto status = wait->Begin(async_get_default_dispatcher(),
                            [copy_ref = wait, weak_ptr = weak_factory_.GetWeakPtr(), buffer_index](
                                async_dispatcher_t*, async::WaitOnce*, zx_status_t status,
                                const zx_packet_signal_t*) mutable {
                              FX_DCHECK(status == ZX_OK || status == ZX_ERR_CANCELED);
                              if (!weak_ptr) {
                                return;
                              }
                              weak_ptr->HandleBufferRelease(buffer_index);
                            });
  FX_DCHECK(status == ZX_OK);

  buffer_server_tokens_[buffer_index] = std::move(buffer_release_server_token);

  FrameInfo frame_info;
  frame_info.set_buffer_index(buffer_index);
  frame_info.set_buffer_release_token(std::move(buffer_release_client_token));
  frame_info.set_capture_timestamp(timestamp);
  GetNextFrameCallback callback = std::move(current_callback_.value());
  callback(fpromise::ok(std::move(frame_info)));

  current_release_fences_.clear();
  current_callback_ = std::nullopt;
  client_received_last_frame_ = true;
  render_frame_in_progress_ = false;
}

void ScreenCapture::HandleBufferRelease(uint32_t buffer_index) {
  buffer_server_tokens_.erase(buffer_index);
  if (available_buffers_.empty() && (current_callback_ != std::nullopt)) {
    available_buffers_.push_front(buffer_index);
    MaybeRenderFrame();
    return;
  }
  available_buffers_.push_front(buffer_index);
}

void ScreenCapture::ClearImages() {
  for (auto& image_id : image_ids_) {
    auto identifier = image_id.second.identifier;
    screen_capture_buffer_collection_importer_->ReleaseBufferImage(identifier);
  }
  image_ids_.clear();
  available_buffers_.clear();
}

}  // namespace screen_capture2
