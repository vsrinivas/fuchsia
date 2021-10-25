// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_VNEXT_LIB_STREAM_SINK_STREAM_SINK_IMPL_H_
#define SRC_MEDIA_VNEXT_LIB_STREAM_SINK_STREAM_SINK_IMPL_H_

#include <fuchsia/media2/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/syslog/cpp/macros.h>

#include "src/media/vnext/lib/stream_sink/clear_request.h"
#include "src/media/vnext/lib/stream_sink/converters.h"
#include "src/media/vnext/lib/stream_sink/release_fence.h"
#include "src/media/vnext/lib/stream_sink/stream_queue.h"

namespace fmlib {

// |fuchsia.media2.StreamSink| implementation. This class forwards packets received via |StreamSink|
// to a |StreamQueue| having first converting the packet to an internal internal type |T|. Clear
// requests are of type |ClearRequest|. The |FromPacketConverter| template must have a
// specialization for |T|.
template <typename T, typename U>
class StreamSinkImpl : public fuchsia::media2::StreamSink {
 public:
  // Constructs a new |StreamSinkImpl| in unconnected state.
  StreamSinkImpl() : binding_(this) {}

  // Constructs a new |StreamSinkImpl| that connects |stream_sink_request| to |stream_queue|.
  // |*stream_queue| is unowned and must exist until disconnection. |conversion_context| is passed
  // to |FromPacketConverter<T>::Convert| to provide context for the conversion.
  StreamSinkImpl(fidl::InterfaceRequest<fuchsia::media2::StreamSink> stream_sink_request,
                 StreamQueue<T, ClearRequest>* stream_queue, U conversion_context)
      : binding_(this, std::move(stream_sink_request)),
        stream_queue_(stream_queue),
        conversion_context_(std::move(conversion_context)) {
    FX_CHECK(binding_.is_bound());
    FX_CHECK(stream_queue_);

    ConnectInternal();
  }

  ~StreamSinkImpl() override {
    if (disconnect_completer_) {
      disconnect_completer_.abandon();
    }

    (void)Disconnect();
  }

  // Disallow copy, assign, and move.
  StreamSinkImpl(StreamSinkImpl&&) = delete;
  StreamSinkImpl(const StreamSinkImpl&) = delete;
  StreamSinkImpl& operator=(StreamSinkImpl&&) = delete;
  StreamSinkImpl& operator=(const StreamSinkImpl&) = delete;

  // Connects |stream_sink_request| to |stream_queue|. |*stream_queue| is unowned and must exist
  // until disconnection. |conversion_context| is passed to |FromPacketConverter<T>::Convert| to
  // provide context for the conversion.
  void Connect(fidl::InterfaceRequest<fuchsia::media2::StreamSink> stream_sink_request,
               StreamQueue<T, ClearRequest>* stream_queue, U conversion_context) {
    FX_CHECK(stream_sink_request);
    FX_CHECK(stream_queue);

    if (is_connected()) {
      Disconnect();
    }

    binding_.Bind(std::move(stream_sink_request));
    stream_queue_ = stream_queue;
    conversion_context_ = std::move(conversion_context);

    ConnectInternal();
  }

  // Disconnects from the |StreamSink| channel.
  fidl::InterfaceRequest<fuchsia::media2::StreamSink> Disconnect() {
    if (!stream_queue_) {
      // Already disconnected.
      return nullptr;
    }

    binding_.set_error_handler(nullptr);
    auto result = binding_.Unbind();

    stream_queue_->drain();
    stream_queue_ = nullptr;

    if (disconnect_completer_) {
      disconnect_completer_.complete_ok();
    }

    return result;
  }

  // Indicates whether this |StreamSinkImpl| is currently connected.
  bool is_connected() const { return binding_.is_bound(); }
  explicit operator bool() const { return binding_.is_bound(); }

  // Returns a promise that completes successfully when:
  // 1) this |StreamSinkImpl| is not connected when this method is called, or
  // 2) this |StreamSinkImpl| is destroyed when connected, or
  // 3) |Disconnect| is called,
  // The promise completes with an error when the FIDL connection fails. The |zx_status_t| returned
  // indicates the error that occurred.
  fpromise::promise<void, zx_status_t> WhenDisconnected() {
    FX_CHECK(!disconnect_completer_);

    if (!is_connected()) {
      return fpromise::make_result_promise<void, zx_status_t>(fpromise::ok());
    }

    fpromise::bridge<void, zx_status_t> bridge;
    disconnect_completer_ = std::move(bridge.completer);
    return bridge.consumer.promise();
  }

 private:
  class ReleaseFenceImpl : public ReleaseFence {
   public:
    explicit ReleaseFenceImpl(zx::eventpair actual) : actual_(std::move(actual)) {}

   private:
    zx::eventpair actual_;
  };

  void ConnectInternal() {
    binding_.set_error_handler([this](zx_status_t status) {
      auto disconnect_completer = std::move(disconnect_completer_);
      Disconnect();
      if (disconnect_completer) {
        disconnect_completer.complete_error(status);
      }
    });
  }

  // fuchsia::media2::StreamSink implementation.
  void PutPacket(fuchsia::media2::Packet packet, zx::eventpair release_fence) override {
    FX_CHECK(stream_queue_);
    stream_queue_->push(FromPacketConverter<T, U>::Convert(
        std::move(packet), std::make_unique<ReleaseFenceImpl>(std::move(release_fence)),
        conversion_context_));
  }

  void End() override {
    FX_CHECK(stream_queue_);
    stream_queue_->end();
  }

  void Clear(bool hold_last_frame, zx::handle completion_fence) override {
    FX_CHECK(stream_queue_);
    stream_queue_->clear(ClearRequest(hold_last_frame, zx::eventpair(completion_fence.release())));
  }

  fidl::Binding<fuchsia::media2::StreamSink> binding_;
  StreamQueue<T, ClearRequest>* stream_queue_ = nullptr;
  U conversion_context_;
  fpromise::completer<void, zx_status_t> disconnect_completer_;
};

}  // namespace fmlib

#endif  // SRC_MEDIA_VNEXT_LIB_STREAM_SINK_STREAM_SINK_IMPL_H_
