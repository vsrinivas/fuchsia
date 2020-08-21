// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/bin/device/stream_impl.h"

#include <lib/async-loop/default.h>
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

static fuchsia::math::Size ConvertToSize(fuchsia::sysmem::ImageFormat_2 format) {
  ZX_DEBUG_ASSERT(format.coded_width < std::numeric_limits<int32_t>::max());
  ZX_DEBUG_ASSERT(format.coded_height < std::numeric_limits<int32_t>::max());
  return {.width = static_cast<int32_t>(format.coded_width),
          .height = static_cast<int32_t>(format.coded_height)};
}

StreamImpl::StreamImpl(const fuchsia::camera3::StreamProperties& properties,
                       const fuchsia::camera2::hal::StreamConfig& legacy_config,
                       fidl::InterfaceRequest<fuchsia::camera3::Stream> request,
                       StreamRequestedCallback on_stream_requested, fit::closure on_no_clients)
    : loop_(&kAsyncLoopConfigNoAttachToCurrentThread),
      properties_(properties),
      legacy_config_(legacy_config),
      on_stream_requested_(std::move(on_stream_requested)),
      on_no_clients_(std::move(on_no_clients)) {
  legacy_stream_.set_error_handler(fit::bind_member(this, &StreamImpl::OnLegacyStreamDisconnected));
  legacy_stream_.events().OnFrameAvailable = fit::bind_member(this, &StreamImpl::OnFrameAvailable);
  current_resolution_ = ConvertToSize(properties.image_format);
  OnNewRequest(std::move(request));
  ZX_ASSERT(loop_.StartThread("Camera Stream Thread") == ZX_OK);
}

StreamImpl::~StreamImpl() {
  Unbind(legacy_stream_);
  async::PostTask(loop_.dispatcher(), [this] {
    for (auto& it : frame_waiters_) {
      // TODO(fxbug.dev/50018): async::Wait destructor ordering edge case
      it.second->Cancel();
      it.second = nullptr;
    }
    loop_.Quit();
  });
  loop_.JoinThreads();
}

void StreamImpl::PostSetMuteState(MuteState mute_state, fit::closure completed) {
  auto nonce = TRACE_NONCE();
  TRACE_DURATION("camera", "StreamImpl::PostSetMuteState");
  TRACE_FLOW_BEGIN("camera", "post_set_mute_state_task", nonce);
  async::PostTask(loop_.dispatcher(), [this, mute_state, completed = std::move(completed), nonce] {
    TRACE_DURATION("camera", "StreamImpl::PostSetMuteState.task");
    TRACE_FLOW_END("camera", "post_set_mute_state_task", nonce);
    mute_state_ = mute_state;
    // On either transition, invalidate existing frames.
    while (!frames_.empty()) {
      frames_.pop();
    }
    completed();
  });
}

void StreamImpl::OnNewRequest(fidl::InterfaceRequest<fuchsia::camera3::Stream> request) {
  auto nonce = TRACE_NONCE();
  TRACE_DURATION("camera", "StreamImpl::OnNewRequest");
  TRACE_FLOW_BEGIN("camera", "post_on_new_request", nonce);
  zx_status_t status =
      async::PostTask(loop_.dispatcher(), [this, request = std::move(request), nonce]() mutable {
        TRACE_DURATION("camera", "StreamImpl::OnNewRequest.task");
        TRACE_FLOW_END("camera", "post_on_new_request", nonce);
        auto client = std::make_unique<Client>(*this, client_id_next_, std::move(request));
        client->PostReceiveResolution(current_resolution_);
        client->PostReceiveCropRegion(nullptr);
        clients_.emplace(client_id_next_++, std::move(client));
      });
  ZX_ASSERT(status == ZX_OK);
}

void StreamImpl::OnLegacyStreamDisconnected(zx_status_t status) {
  FX_PLOGS(ERROR, status) << "Legacy Stream disconnected unexpectedly.";
  clients_.clear();
  on_no_clients_();
}

void StreamImpl::PostRemoveClient(uint64_t id) {
  auto nonce = TRACE_NONCE();
  TRACE_DURATION("camera", "StreamImpl::PostRemoveClient");
  TRACE_FLOW_BEGIN("camera", "post_remove_client", nonce);
  async::PostTask(loop_.dispatcher(), [=]() {
    TRACE_DURATION("camera", "StreamImpl::PostRemoveClient.task");
    TRACE_FLOW_END("camera", "post_remove_client", nonce);
    clients_.erase(id);
    if (clients_.empty()) {
      on_no_clients_();
    }
  });
}

void StreamImpl::PostAddFrameSink(uint64_t id) {
  auto nonce = TRACE_NONCE();
  TRACE_DURATION("camera", "StreamImpl::PostAddFrameSink");
  TRACE_FLOW_BEGIN("camera", "post_add_frame_sink", nonce);
  async::PostTask(loop_.dispatcher(), [=]() {
    TRACE_DURATION("camera", "StreamImpl::PostAddFrameSink.task");
    TRACE_FLOW_END("camera", "post_add_frame_sink", nonce);
    frame_sinks_.push(id);
    SendFrames();
  });
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
  auto waiter =
      std::make_unique<async::Wait>(fence.get(), ZX_EVENTPAIR_PEER_CLOSED, 0,
                                    [this, fence = std::move(fence), index = info.buffer_id](
                                        async_dispatcher_t* dispatcher, async::WaitBase* wait,
                                        zx_status_t status, const zx_packet_signal_t* signal) {
                                      if (status != ZX_OK) {
                                        return;
                                      }
                                      legacy_stream_->ReleaseFrame(index);
                                      frame_waiters_.erase(index);
                                    });
  ZX_ASSERT(waiter->Begin(loop_.dispatcher()) == ZX_OK);
  frame_waiters_[info.buffer_id] = std::move(waiter);

  // If there are too many frames outstanding, eagerly release the oldest frame not held by a
  // client. This may be the frame that was just received.
  if (frame_waiters_.size() > max_camping_buffers_) {
    ZX_ASSERT(!frames_.empty());
    auto buffer_index = frames_.front().buffer_index;
    auto it = frame_waiters_.find(buffer_index);
    ZX_ASSERT(it != frame_waiters_.end());
    // The destructor ordering of Wait vs. WaitBase requires that the wait be explicitly canceled
    // prior to being deleted if the wait handler holds the reference to the waited-upon object.
    it->second->Cancel();
    frame_waiters_.erase(it);
    legacy_stream_->ReleaseFrame(buffer_index);
    frames_.pop();
  }

  // Send the frame to any pending recipients.
  SendFrames();
}

void StreamImpl::PostSetBufferCollection(
    uint64_t id, fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token) {
  auto nonce = TRACE_NONCE();
  TRACE_DURATION("camera", "StreamImpl::PostSetBufferCollection");
  TRACE_FLOW_BEGIN("camera", "post_set_buffer_collection", nonce);
  async::PostTask(loop_.dispatcher(), [this, id, token_handle = std::move(token), nonce]() mutable {
    TRACE_DURATION("camera", "StreamImpl::PostSetBufferCollection.task");
    TRACE_FLOW_END("camera", "post_set_buffer_collection", nonce);
    auto it = clients_.find(id);
    if (it == clients_.end()) {
      FX_LOGS(ERROR) << "Client " << id << " not found.";
      token_handle.BindSync()->Close();
      ZX_DEBUG_ASSERT(false);
      return;
    }
    auto& client = it->second;

    // If null, just unregister the client and return.
    if (!token_handle) {
      client->Participant() = false;
      return;
    }

    client->Participant() = true;

    // Bind and duplicate the token for each participating client.
    fuchsia::sysmem::BufferCollectionTokenPtr token;
    zx_status_t status = token.Bind(std::move(token_handle), loop_.dispatcher());
    if (status != ZX_OK) {
      ZX_ASSERT(status == ZX_ERR_CANCELED);
      // Thread is shutting down.
      return;
    }
    for (auto& client : clients_) {
      if (client.second->Participant()) {
        fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> client_token;
        token->Duplicate(ZX_RIGHT_SAME_RIGHTS, client_token.NewRequest());
        client.second->PostReceiveBufferCollection(std::move(client_token));
      }
    }

    // Synchronize and pass the final token to the device for constraints application.
    token->Sync([this, token = std::move(token)]() mutable {
      ZX_ASSERT(on_stream_requested_);
      frame_waiters_.clear();
      on_stream_requested_(
          std::move(token), legacy_stream_.NewRequest(loop_.dispatcher()),
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
      it->second->PostSendFrame(std::move(frames_.front()));
      frames_.pop();
    }
  }
}

void StreamImpl::PostSetResolution(uint32_t id, fuchsia::math::Size coded_size) {
  auto nonce = TRACE_NONCE();
  TRACE_DURATION("camera", "StreamImpl::PostSetResolution");
  TRACE_FLOW_BEGIN("camera", "post_set_resolution", nonce);
  zx_status_t status = async::PostTask(loop_.dispatcher(), [this, id, coded_size, nonce] {
    TRACE_DURATION("camera", "StreamImpl::PostSetResolution.task");
    TRACE_FLOW_END("camera", "post_set_resolution", nonce);
    auto it = clients_.find(id);
    if (it == clients_.end()) {
      FX_LOGS(ERROR) << "Client " << id << " not found.";
      ZX_DEBUG_ASSERT(false);
      return;
    }
    auto& client = it->second;

    // Begin with the full resolution.
    auto best_size = ConvertToSize(properties_.image_format);
    if (coded_size.width > best_size.width || coded_size.height > best_size.height) {
      client->PostCloseConnection(ZX_ERR_INVALID_ARGS);
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
            it->second->PostCloseConnection(ZX_ERR_INTERNAL);
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
      client->PostReceiveResolution(best_size);
    }
  });
  ZX_DEBUG_ASSERT(status == ZX_OK);
}

void StreamImpl::PostSetCropRegion(uint32_t id, std::unique_ptr<fuchsia::math::RectF> region) {
  auto nonce = TRACE_NONCE();
  TRACE_DURATION("camera", "StreamImpl::PostSetCropRegion");
  TRACE_FLOW_BEGIN("camera", "post_set_crop_region", nonce);
  zx_status_t status =
      async::PostTask(loop_.dispatcher(), [this, region = std::move(region), nonce]() mutable {
        TRACE_DURATION("camera", "StreamImpl::PostSetCropRegion.task");
        TRACE_FLOW_END("camera", "post_set_crop_region", nonce);
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
          client->PostReceiveCropRegion(std::move(region));
        }
      });
  ZX_DEBUG_ASSERT(status == ZX_OK);
}
