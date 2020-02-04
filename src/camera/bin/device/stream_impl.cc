// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/bin/device/stream_impl.h"

#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/fit/function.h>
#include <lib/syslog/cpp/logger.h>
#include <lib/zx/time.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include "src/camera/bin/device/messages.h"
#include "src/camera/bin/device/util.h"

StreamImpl::StreamImpl(fidl::InterfaceHandle<fuchsia::camera2::Stream> legacy_stream,
                       fidl::InterfaceRequest<fuchsia::camera3::Stream> request,
                       uint32_t max_camping_buffers, fit::closure on_no_clients)
    : loop_(&kAsyncLoopConfigNoAttachToCurrentThread),
      on_no_clients_(std::move(on_no_clients)),
      max_camping_buffers_(max_camping_buffers) {
  ZX_ASSERT(legacy_stream_.Bind(std::move(legacy_stream), loop_.dispatcher()) == ZX_OK);
  legacy_stream_.set_error_handler(fit::bind_member(this, &StreamImpl::OnLegacyStreamDisconnected));
  legacy_stream_.events().OnFrameAvailable = fit::bind_member(this, &StreamImpl::OnFrameAvailable);
  auto client = std::make_unique<Client>(*this, client_id_next_, std::move(request));
  clients_.emplace(client_id_next_++, std::move(client));
  ZX_ASSERT(loop_.StartThread("Camera Stream Thread") == ZX_OK);
}

StreamImpl::~StreamImpl() {
  Unbind(legacy_stream_);
  loop_.Quit();
  loop_.JoinThreads();
}

void StreamImpl::OnLegacyStreamDisconnected(zx_status_t status) {
  FX_PLOGS(ERROR, status) << "Legacy Stream disconnected unexpectedly.";
  clients_.clear();
  on_no_clients_();
}

void StreamImpl::PostRemoveClient(uint64_t id) {
  async::PostTask(loop_.dispatcher(), [=]() {
    clients_.erase(id);
    if (clients_.empty()) {
      on_no_clients_();
    }
  });
}

void StreamImpl::PostAddFrameSink(uint64_t id) {
  async::PostTask(loop_.dispatcher(), [=]() {
    frame_sinks_.push(id);
    SendFrames();
  });
}

void StreamImpl::OnFrameAvailable(fuchsia::camera2::FrameAvailableInfo info) {
  if (info.frame_status != fuchsia::camera2::FrameStatus::OK) {
    FX_LOGS(WARNING) << "Driver reported a bad frame. This will not be reported to clients.";
    legacy_stream_->AcknowledgeFrameError();
    return;
  }

  // Construct the frame info and create the release fence.
  fuchsia::camera3::FrameInfo frame;
  frame.buffer_index = info.buffer_id;
  frame.frame_counter = ++frame_counter_;
  frame.timestamp = info.metadata.timestamp();
  zx::eventpair fence;
  ZX_ASSERT(zx::eventpair::create(0u, &fence, &frame.release_fence) == ZX_OK);
  frames_.push(std::move(frame));

  // Release frames in excess of the camping limit.
  while (frames_.size() > max_camping_buffers_) {
    frames_.pop();
  }

  // Queue a waiter so that when the client end of the fence is released, the frame is released back
  // to the driver.
  auto waiter =
      std::make_unique<async::Wait>(fence.get(), ZX_EVENTPAIR_PEER_CLOSED, 0,
                                    [this, fence = std::move(fence), index = frame.buffer_index](
                                        async_dispatcher_t* dispatcher, async::WaitBase* wait,
                                        zx_status_t status, const zx_packet_signal_t* signal) {
                                      legacy_stream_->ReleaseFrame(index);
                                      frame_waiters_.erase(index);
                                    });
  ZX_ASSERT(waiter->Begin(loop_.dispatcher()) == ZX_OK);
  frame_waiters_[frame.buffer_index] = std::move(waiter);

  // Send the frame to any pending recipients.
  SendFrames();
}

void StreamImpl::SendFrames() {
  if (frame_sinks_.size() > 1 && !frame_sink_warning_sent_) {
    FX_LOGS(INFO) << Messages::kMultipleFrameClients;
    frame_sink_warning_sent_ = true;
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
