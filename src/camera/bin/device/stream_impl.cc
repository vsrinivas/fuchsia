// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/bin/device/stream_impl.h"

#include <lib/async/cpp/task.h>
#include <lib/fit/function.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>
#include <lib/zx/time.h>
#include <zircon/assert.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include "src/camera/bin/device/messages.h"
#include "src/camera/bin/device/util.h"
#include "src/lib/fsl/handles/object_info.h"

static fuchsia::math::Size ConvertToSize(fuchsia::sysmem::ImageFormat_2 format) {
  ZX_DEBUG_ASSERT(format.coded_width < std::numeric_limits<int32_t>::max());
  ZX_DEBUG_ASSERT(format.coded_height < std::numeric_limits<int32_t>::max());
  return {.width = static_cast<int32_t>(format.coded_width),
          .height = static_cast<int32_t>(format.coded_height)};
}

StreamImpl::StreamImpl(async_dispatcher_t* dispatcher,
                       const fuchsia::camera3::StreamProperties2& properties,
                       const fuchsia::camera2::hal::StreamConfig& legacy_config,
                       fidl::InterfaceRequest<fuchsia::camera3::Stream> request,
                       CheckTokenCallback check_token, StreamRequestedCallback on_stream_requested,
                       fit::closure on_no_clients)
    : dispatcher_(dispatcher),
      properties_(properties),
      legacy_config_(legacy_config),
      check_token_(std::move(check_token)),
      on_stream_requested_(std::move(on_stream_requested)),
      on_no_clients_(std::move(on_no_clients)) {
  legacy_stream_.set_error_handler(fit::bind_member(this, &StreamImpl::OnLegacyStreamDisconnected));
  legacy_stream_.events().OnFrameAvailable = fit::bind_member(this, &StreamImpl::OnFrameAvailable);
  current_resolution_ = ConvertToSize(properties.image_format());
  OnNewRequest(std::move(request));
}

StreamImpl::~StreamImpl() = default;

void StreamImpl::SetMuteState(MuteState mute_state) {
  TRACE_DURATION("camera", "StreamImpl::SetMuteState");
  mute_state_ = mute_state;
  // On either transition, invalidate existing frames.
  while (!frames_.empty()) {
    frames_.pop();
  }
}

void StreamImpl::OnNewRequest(fidl::InterfaceRequest<fuchsia::camera3::Stream> request) {
  TRACE_DURATION("camera", "StreamImpl::OnNewRequest");
  auto client = std::make_unique<Client>(*this, client_id_next_, std::move(request));
  client->ReceiveResolution(current_resolution_);
  client->ReceiveCropRegion(nullptr);
  clients_.emplace(client_id_next_++, std::move(client));
}

void StreamImpl::OnLegacyStreamDisconnected(zx_status_t status) {
  FX_PLOGS(ERROR, status) << "Legacy Stream disconnected unexpectedly.";
  clients_.clear();
  on_no_clients_();
}

void StreamImpl::RemoveClient(uint64_t id) {
  TRACE_DURATION("camera", "StreamImpl::RemoveClient");
  clients_.erase(id);
  if (clients_.empty()) {
    on_no_clients_();
  }
}

void StreamImpl::AddFrameSink(uint64_t id) {
  TRACE_DURATION("camera", "StreamImpl::AddFrameSink");
  frame_sinks_.push(id);
  SendFrames();
}

void StreamImpl::OnFrameAvailable(fuchsia::camera2::FrameAvailableInfo info) {
  TRACE_DURATION("camera", "StreamImpl::OnFrameAvailable");
  if (info.metadata.has_timestamp()) {
    TRACE_FLOW_END("camera", "camera_stream_on_frame_available", info.metadata.timestamp());
  }

  if (info.frame_status != fuchsia::camera2::FrameStatus::OK) {
    FX_LOGS(WARNING) << "Driver reported a bad frame. This will not be reported to clients.";
    legacy_stream_->AcknowledgeFrameError();
    return;
  }

  if (frame_waiters_.find(info.buffer_id) != frame_waiters_.end()) {
    FX_LOGS(WARNING) << "Driver sent a frame that was already in use (ID = " << info.buffer_id
                     << "). This frame will not be sent to clients.";
    legacy_stream_->ReleaseFrame(info.buffer_id);
    return;
  }

  if (!info.metadata.has_timestamp()) {
    FX_LOGS(WARNING)
        << "Driver sent a frame without a timestamp. This frame will not be sent to clients.";
    legacy_stream_->ReleaseFrame(info.buffer_id);
    return;
  }

  // Discard any spurious frames received while muted.
  if (mute_state_.muted()) {
    legacy_stream_->ReleaseFrame(info.buffer_id);
    return;
  }

  // Construct the frame info and create the release fence. These are held by the server until a
  // client requests one via GetNextFrame.
  fuchsia::camera3::FrameInfo frame;
  frame.buffer_index = info.buffer_id;
  frame.frame_counter = ++frame_counter_;
  frame.timestamp = info.metadata.timestamp();
  zx::eventpair fence;
  ZX_ASSERT(zx::eventpair::create(0u, &fence, &frame.release_fence) == ZX_OK);
  frames_.push(std::move(frame));

  // Queue a waiter so that when the client end of the fence is released, the frame is released back
  // to the driver.
  ZX_ASSERT(frame_waiters_.size() <= max_camping_buffers_);
  frame_waiters_[info.buffer_id] =
      std::make_unique<FrameWaiter>(dispatcher_, std::move(fence), [this, index = info.buffer_id] {
        legacy_stream_->ReleaseFrame(index);
        frame_waiters_.erase(index);
      });

  // If there are too many frames outstanding, eagerly release the oldest frame not held by a
  // client. This may be the frame that was just received.
  if (frame_waiters_.size() > max_camping_buffers_) {
    ZX_ASSERT(!frames_.empty());
    auto buffer_index = frames_.front().buffer_index;
    auto it = frame_waiters_.find(buffer_index);
    ZX_ASSERT(it != frame_waiters_.end());
    frame_waiters_.erase(it);
    legacy_stream_->ReleaseFrame(buffer_index);
    frames_.pop();
  }

  // Send the frame to any pending recipients.
  SendFrames();
}

void StreamImpl::SetBufferCollection(
    uint64_t id, fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token) {
  TRACE_DURATION("camera", "StreamImpl::SetBufferCollection");
  auto it = clients_.find(id);
  if (it == clients_.end()) {
    FX_LOGS(ERROR) << "Client " << id << " not found.";
    token.BindSync()->Close();
    ZX_DEBUG_ASSERT(false);
    return;
  }
  auto& client = it->second;

  // If null, just unregister the client and return.
  if (!token) {
    client->Participant() = false;
    return;
  }
  client->Participant() = true;

  // Validate the token.
  fsl::GetRelatedKoid(token.channel().get());
  check_token_(
      fsl::GetRelatedKoid(token.channel().get()),
      [this, it, token_handle = std::move(token)](bool valid) mutable {
        if (!valid) {
          FX_LOGS(INFO) << "Client provided an invalid BufferCollectionToken.";
          it->second->CloseConnection(ZX_ERR_BAD_STATE);
          return;
        }
        // Duplicate and send each client a token.
        fuchsia::sysmem::BufferCollectionTokenPtr token;
        token.Bind(std::move(token_handle));
        std::map<uint64_t, fuchsia::sysmem::BufferCollectionTokenHandle> client_tokens;
        for (auto& client : clients_) {
          if (client.second->Participant()) {
            token->Duplicate(ZX_RIGHT_SAME_RIGHTS, client_tokens[client.first].NewRequest());
          }
        }
        token->Sync([this, token = std::move(token),
                     client_tokens = std::move(client_tokens)]() mutable {
          for (auto& [id, token] : client_tokens) {
            auto it = clients_.find(id);
            if (it == clients_.end()) {
              token.BindSync()->Close();
            } else {
              it->second->ReceiveBufferCollection(std::move(token));
            }
          }
          // Send the last token to the device for constraints application.
          frame_waiters_.clear();
          on_stream_requested_(
              std::move(token), legacy_stream_.NewRequest(),
              [this](uint32_t max_camping_buffers) { max_camping_buffers_ = max_camping_buffers; },
              legacy_stream_format_index_);
          legacy_stream_->Start();
        });
      });
}

void StreamImpl::SendFrames() {
  TRACE_DURATION("camera", "StreamImpl::SendFrames");
  if (frame_sinks_.size() > 1 && !frame_sink_warning_sent_) {
    FX_LOGS(INFO) << Messages::kMultipleFrameClients;
    frame_sink_warning_sent_ = true;
  }

  if (mute_state_.muted()) {
    return;
  }

  while (!frames_.empty() && !frame_sinks_.empty()) {
    auto it = clients_.find(frame_sinks_.front());
    frame_sinks_.pop();
    if (it != clients_.end()) {
      it->second->SendFrame(std::move(frames_.front()));
      frames_.pop();
    }
  }
}

void StreamImpl::SetResolution(uint64_t id, fuchsia::math::Size coded_size) {
  TRACE_DURATION("camera", "StreamImpl::SetResolution");
  auto it = clients_.find(id);
  if (it == clients_.end()) {
    FX_LOGS(ERROR) << "Client " << id << " not found.";
    ZX_DEBUG_ASSERT(false);
    return;
  }
  auto& client = it->second;

  // Begin with the full resolution.
  auto best_size = ConvertToSize(properties_.image_format());
  if (coded_size.width > best_size.width || coded_size.height > best_size.height) {
    client->CloseConnection(ZX_ERR_INVALID_ARGS);
    return;
  }

  // Examine all supported resolutions, preferring those that cover the requested resolution but
  // have fewer pixels, breaking ties by picking the one with a smaller width.
  uint32_t best_index = 0;
  for (uint32_t i = 0; i < legacy_config_.image_formats.size(); ++i) {
    auto size = ConvertToSize(legacy_config_.image_formats[i]);
    bool contains_request = size.width >= coded_size.width && size.height >= coded_size.height;
    bool smaller_size = size.width * size.height < best_size.width * best_size.height;
    bool equal_size = size.width * size.height == best_size.width * best_size.height;
    bool smaller_width = size.width < best_size.width;
    if (contains_request && (smaller_size || (equal_size && smaller_width))) {
      best_size = size;
      best_index = i;
    }
  }

  // Save the selected image format, and set it on the stream if bound.
  legacy_stream_format_index_ = best_index;
  if (legacy_stream_) {
    legacy_stream_->SetImageFormat(legacy_stream_format_index_, [this](zx_status_t status) {
      if (status != ZX_OK) {
        FX_PLOGS(ERROR, status) << "Unexpected response from driver.";
        while (!clients_.empty()) {
          auto it = clients_.begin();
          it->second->CloseConnection(ZX_ERR_INTERNAL);
          clients_.erase(it);
        }
        on_no_clients_();
        return;
      }
    });
  }
  current_resolution_ = best_size;

  // Inform clients of the resolution change.
  for (auto& [id, client] : clients_) {
    client->ReceiveResolution(best_size);
  }
}

void StreamImpl::SetCropRegion(uint64_t id, std::unique_ptr<fuchsia::math::RectF> region) {
  TRACE_DURATION("camera", "StreamImpl::SetCropRegion");
  if (legacy_stream_) {
    float x_min = 0.0f;
    float y_min = 0.0f;
    float x_max = 1.0f;
    float y_max = 1.0f;
    if (region) {
      x_min = region->x;
      y_min = region->y;
      x_max = x_min + region->width;
      y_max = y_min + region->height;
    }
    legacy_stream_->SetRegionOfInterest(x_min, y_min, x_max, y_max, [](zx_status_t status) {
      // TODO(fxbug.dev/50908): Make this an error once RegionOfInterest support is known at
      // init time. FX_PLOGS(WARNING, status) << "Stream does not support crop region.";
    });
  }
  current_crop_region_ = std::move(region);

  // Inform clients of the resolution change.
  for (auto& [id, client] : clients_) {
    std::unique_ptr<fuchsia::math::RectF> region;
    if (current_crop_region_) {
      region = std::make_unique<fuchsia::math::RectF>(*current_crop_region_);
    }
    client->ReceiveCropRegion(std::move(region));
  }
}
