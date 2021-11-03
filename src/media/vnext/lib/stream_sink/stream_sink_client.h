// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_VNEXT_LIB_STREAM_SINK_STREAM_SINK_CLIENT_H_
#define SRC_MEDIA_VNEXT_LIB_STREAM_SINK_STREAM_SINK_CLIENT_H_

#include <fuchsia/media2/cpp/fidl.h>
#include <lib/async/cpp/executor.h>
#include <lib/async/default.h>
#include <lib/fpromise/scope.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/eventpair.h>

#include "src/media/vnext/lib/stream_sink/clear_request.h"
#include "src/media/vnext/lib/stream_sink/converters.h"
#include "src/media/vnext/lib/stream_sink/stream_queue.h"

namespace fmlib {

// |fuchsia.media2.StreamSink| client. This class forwards internal packets of type |T| from a
// |StreamQueue| to a |fuchsia.media2.StreamSink| service. Clear requests are of type
// |ClearRequest|. The |ToPacketConverter| template must have a specialization for |T|.
template <typename T>
class StreamSinkClient {
 public:
  // Constructs a new |StreamSinkClient| in unconnected state.
  StreamSinkClient() = default;

  // Constructs a new |StreamSinkClient| that connects |stream_queue| to |stream_sink|.
  // |*stream_queue| is unowned and must exist until disconnection.
  StreamSinkClient(async::Executor& executor, StreamQueue<T, ClearRequest>* stream_queue,
                   fuchsia::media2::StreamSinkHandle stream_sink_handle) {
    FX_CHECK(stream_queue_);
    FX_CHECK(stream_sink_handle);
    Connect(executor, stream_queue, std::move(stream_sink_handle));
  }

  ~StreamSinkClient() {
    if (disconnect_completer_) {
      disconnect_completer_.abandon();
    }

    (void)Disconnect();
  }

  // Disallow copy, assign, and move.
  StreamSinkClient(StreamSinkClient&&) = delete;
  StreamSinkClient(const StreamSinkClient&) = delete;
  StreamSinkClient& operator=(StreamSinkClient&&) = delete;
  StreamSinkClient& operator=(const StreamSinkClient&) = delete;

  // Connects |stream_queue| to |stream_sink|. |*stream_queue| is unowned and must exist until
  // disconnection.
  void Connect(async::Executor& executor, StreamQueue<T, ClearRequest>* stream_queue,
               fuchsia::media2::StreamSinkHandle stream_sink_handle) {
    FX_CHECK(stream_queue);
    FX_CHECK(stream_sink_handle);

    if (is_connected()) {
      Disconnect();
    }

    executor_ = &executor;
    stream_queue_ = stream_queue;

    stream_sink_.Bind(std::move(stream_sink_handle));
    stream_sink_.set_error_handler([this](zx_status_t status) {
      auto disconnect_completer = std::move(disconnect_completer_);
      Disconnect();
      if (disconnect_completer) {
        disconnect_completer.complete_error(status);
      }
    });

    Pull();
  }

  // Disconnects from the |StreamSink| channel.
  fuchsia::media2::StreamSinkHandle Disconnect() {
    if (!is_connected()) {
      return nullptr;
    }

    if (stream_queue_) {
      stream_queue_->cancel_pull();
      stream_queue_ = nullptr;
    }

    stream_sink_.set_error_handler(nullptr);
    auto result = stream_sink_.Unbind();

    if (disconnect_completer_) {
      disconnect_completer_.complete_ok();
    }

    return result;
  }

  // Indicates whether this |StreamSinkClient| is currently connected.
  bool is_connected() const { return !!stream_sink_; }
  explicit operator bool() const { return !!stream_sink_; }

  // Returns a promise that completes successfully when:
  // 1) this |StreamSinkClient| is not connected when this method is called, or
  // 2) |Disconnect| is called.
  // The promise completes with an error when the FIDL connection fails. The |zx_status_t| returned
  // indicates the error that occurred. The promise is abandoned if this |StreamSinkClient| is
  // destroyed while connected.
  [[nodiscard]] fpromise::promise<void, zx_status_t> WhenDisconnected() {
    FX_CHECK(!disconnect_completer_);

    if (!is_connected()) {
      return fpromise::make_result_promise<void, zx_status_t>(fpromise::ok());
    }

    fpromise::bridge<void, zx_status_t> bridge;
    disconnect_completer_ = std::move(bridge.completer);
    return bridge.consumer.promise();
  }

  // Returns a promise that completes successfully when this |StreamSinkClient| pulls
  // |StreamQueueError::kDrained|. This method may only be called once for a given instance of
  // |StreamSinkClient|. If this method is called after |StreamQueueError::kDrained| is pulled,
  // the returned promise completes immediately.
  [[nodiscard]] fpromise::promise<> WhenDrained() {
    FX_CHECK(drain_bridge_.consumer);
    return drain_bridge_.consumer.promise();
  }

 private:
  // Pulls from the stream queue and asynchronously handles the consumed elements. This method
  // invokes itself (asynchronously) until the connection is no longer viable.
  void Pull();

  async::Executor* executor_;
  StreamQueue<T, ClearRequest>* stream_queue_ = nullptr;
  fuchsia::media2::StreamSinkPtr stream_sink_;
  fpromise::completer<void, zx_status_t> disconnect_completer_;
  fpromise::bridge<> drain_bridge_;
  fpromise::scope scope_;
};

template <typename T>
void StreamSinkClient<T>::Pull() {
  FX_CHECK(executor_);
  FX_CHECK(async_get_default_dispatcher() == executor_->dispatcher());

  if (!stream_queue_ || !stream_sink_) {
    return;
  }

  executor_->schedule_task(
      stream_queue_->pull()
          .then([this](typename StreamQueue<T, ClearRequest>::PullResult& result) {
            if (result.is_error()) {
              switch (result.error()) {
                case StreamQueueError::kDrained:
                  // The queue has been drained.
                  FX_CHECK(drain_bridge_.completer);
                  drain_bridge_.completer.complete_ok();
                  break;
                case StreamQueueError::kCanceled:
                  // |Disconnect| was called while we were waiting.
                  break;
              }

              // Done pulling.
              return;
            }

            if (result.value().is_packet()) {
              auto& packet = result.value().packet();

              // Create the release fence.
              zx::eventpair release_fence_local;
              zx::eventpair release_fence_remote;
              zx_status_t status =
                  zx::eventpair::create(0, &release_fence_local, &release_fence_remote);
              if (status != ZX_OK) {
                FX_PLOGS(WARNING, status) << "Failed to create event, dropping packet";
                Pull();
                return;
              }

              // Put the packet.
              stream_sink_->PutPacket(ToPacketConverter<T>::Convert(packet),
                                      std::move(release_fence_remote));

              // When the release fence peer is closed, destroy the packet.
              auto release_fence_unowned = zx::unowned_handle(release_fence_local.get());
              executor_->schedule_task(
                  executor_
                      ->MakePromiseWaitHandle(std::move(release_fence_unowned),
                                              ZX_EVENTPAIR_PEER_CLOSED)
                      .then([packet = std::move(packet),
                             release_fence = std::move(release_fence_local)](
                                fpromise::result<zx_packet_signal_t, zx_status_t>& result) {
                        if (result.is_error()) {
                          FX_PLOGS(WARNING, result.error())
                              << "Failed to wait for release fence, releasing now";
                        }

                        // |packet| goes out of scope here, signaling that the packet's payload
                        // regions are available for reuse.
                      }));
            } else if (result.value().is_clear_request()) {
              auto& clear_request = result.value().clear_request();
              bool hold_last_frame = clear_request.hold_last_frame();
              stream_sink_->Clear(hold_last_frame, clear_request.take_completion_fence());
            } else {
              FX_CHECK(result.value().is_ended());
              stream_sink_->End();
            }

            Pull();
          })
          .wrap_with(scope_));
}

}  // namespace fmlib

#endif  // SRC_MEDIA_VNEXT_LIB_STREAM_SINK_STREAM_SINK_CLIENT_H_
