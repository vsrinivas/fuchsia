// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_VNEXT_LIB_STREAM_IO_INPUT_H_
#define SRC_MEDIA_VNEXT_LIB_STREAM_IO_INPUT_H_

#include <fuchsia/media2/cpp/fidl.h>
#include <lib/fpromise/scope.h>
#include <lib/fpromise/sequencer.h>

#include "src/media/vnext/lib/stream_io/buffer_collection.h"
#include "src/media/vnext/lib/stream_sink/stream_queue.h"
#include "src/media/vnext/lib/stream_sink/stream_sink_impl.h"
#include "src/media/vnext/lib/threads/thread.h"

namespace fmlib {

// Errors returned by |Input::Pull|.
enum class InputError {
  // Input is disconnected.
  kDisconnected,
};

// An input through which a consumer receives a stream of packets. |T| is the internal packet type,
// which must be moveable and have a specialization defined for |FromPacketConverter|.
template <typename T>
class Input {
 public:
  using PullValue = typename StreamQueue<T, ClearRequest>::Element;
  using PullResult = fpromise::result<PullValue, InputError>;

  // An active input connection.
  class Connection {
   public:
    ~Connection() {
      if (drained_bridge_.completer) {
        drained_bridge_.completer.complete_error();
      }
    }

    // Disallow copy, assign and move.
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;
    Connection(Connection&&) = delete;
    Connection& operator=(Connection&&) = delete;

    // Indicates whether this |Input| is currently connected.
    bool is_connected() const { return stream_sink_impl_.is_connected(); }

    // Returns the buffer collection for this connection. This method is valid only if the
    // connection was created using a buffer token. Connections created with no buffer token do
    // not map buffers and have no buffer collection.
    const InputBufferCollection& buffer_collection() const {
      FX_CHECK(buffer_collection_);
      return *buffer_collection_;
    }

    // Returns a promise that completes successfully when this input connection is already
    // disconnected and completes with an error when the connection is disconnected unexpectedly.
    // The |zx_status_t| returned indicates the connection error that occurred, usually
    // |ZX_ERR_PEER_CLOSED|. The promise is abandoned when this connection is destroyed.
    [[nodiscard]] fpromise::promise<void, zx_status_t> WhenDisconnected() {
      return stream_sink_impl_.WhenDisconnected();
    }

    // Sets a closure that will be called when a clear is received. This call is asynchronous with
    // respect to |Pull|, so it can be used to unblock a thread that is blocked and cannot call
    // |Pull| to receive the clear indication that way.
    void SetClearedClosure(fit::closure closure) {
      return stream_queue_.set_cleared_closure(std::move(closure));
    }

    // Returns a promise that completes with the element at the front of the queue, removing it on
    // completion. An element is a wrapped variant that can be a packet, an end indication or a
    // clear request. |InputError::kDisconnected| indicates this input was disconnected.
    //
    // After this method is called, it may not be called again until after the promise completes.
    [[nodiscard]] fpromise::promise<PullValue, InputError> Pull() {
      if (!is_connected()) {
        fpromise::make_error_promise(InputError::kDisconnected);
      }

      return stream_queue_.pull()
          .or_else([this](const StreamQueueError& error) {
            switch (error) {
              case StreamQueueError::kDrained:
                FX_CHECK(drained_bridge_.completer);
                drained_bridge_.completer.complete_ok();
                stream_sink_impl_.Disconnect();
                return fpromise::error(InputError::kDisconnected);

              case StreamQueueError::kCanceled:
                FX_CHECK(false) << "got unexpected StreamQueueError::kCanceled";
                abort();
            }
          })
          .wrap_with(scope_);
    }

   private:
    explicit Connection(std::unique_ptr<InputBufferCollection> buffer_collection)
        : buffer_collection_(std::move(buffer_collection)) {}

    // Binds the |StreamSinkImpl| to |stream_sink_request|. This method must be called on the
    // fidl thread to be used to run the stream sink service.
    [[nodiscard]] fpromise::promise<> Bind(
        fidl::InterfaceRequest<fuchsia::media2::StreamSink> stream_sink_request) {
      FX_CHECK(stream_sink_request);
      return fpromise::make_promise(
                 [this, stream_sink_request = std::move(stream_sink_request)]() mutable {
                   stream_sink_impl_.Connect(std::move(stream_sink_request), &stream_queue_,
                                             buffer_collection_.get());
                 })
          .wrap_with(scope_);
    }

    // Returns a promise that returns ok when this connection is drained and disconnected. The
    // promise returns with an error if this connection is deleted before it drains.
    [[nodiscard]] fpromise::promise<> WhenDrained() {
      FX_CHECK(drained_bridge_.consumer);
      return drained_bridge_.consumer.promise();
    }

    StreamQueue<T, ClearRequest> stream_queue_;
    StreamSinkImpl<T, InputBufferCollection*> stream_sink_impl_;
    std::unique_ptr<InputBufferCollection> buffer_collection_;
    fpromise::bridge<> drained_bridge_;
    fpromise::scope scope_;

    friend class Input;
  };

  Input() = default;

  ~Input() = default;

  // Disallow copy, assign and move.
  Input(const Input&) = delete;
  Input& operator=(const Input&) = delete;
  Input(Input&&) = delete;
  Input& operator=(Input&&) = delete;

  using ConnectResult =
      fpromise::result<std::unique_ptr<Connection>, fuchsia::media2::ConnectionError>;

  // Returns a promise that creates a |Connection| and returns a unique pointer to it. Deleting
  // the |Connection| disconnects it immediately. If an active connection already exists for this
  // input, the promise returned by this method won't complete until the prior connection is
  // disconnected.
  //
  // This overload is used when payloads must be mapped into system memory. A buffer collection is
  // created and populated using the last three parameters. The closure does not complete until the
  // buffer collection is populated.
  [[nodiscard]] fpromise::promise<std::unique_ptr<Connection>, fuchsia::media2::ConnectionError>
  Connect(Thread fidl_thread,
          fidl::InterfaceRequest<fuchsia::media2::StreamSink> stream_sink_request,
          fuchsia::media2::BufferProvider& buffer_provider, zx::eventpair buffer_collection_token,
          fuchsia::media2::BufferConstraints constraints) {
    FX_CHECK(stream_sink_request);
    FX_CHECK(buffer_collection_token);

    return InputBufferCollection::Create(buffer_provider, std::move(buffer_collection_token),
                                         constraints, "input", 0)
        .and_then([this, fidl_thread = std::move(fidl_thread),
                   stream_sink_request = std::move(stream_sink_request)](
                      std::unique_ptr<InputBufferCollection>& buffer_collection) mutable {
          return ConnectInternal(std::move(fidl_thread), std::move(stream_sink_request),
                                 std::move(buffer_collection));
        })
        .wrap_with(scope_);
  }

  // Returns a promise that creates a |Connection| and returns a unique pointer to it. Deleting
  // the |Connection| disconnects it immediately. If an active connection already exists for this
  // input, the promise returned by this method won't complete until the prior connection is
  // disconnected.
  //
  // This overload is used when payloads should not be mapped into system memory. No buffer
  // collection is established, and incoming packets have no local memory pointers for in-proc
  // access. The caller is expected to handle interaction with the buffer provider, and the
  // returned promise completes regardless of whether a buffer collection has been negotiated.
  [[nodiscard]] fpromise::promise<std::unique_ptr<Connection>, fuchsia::media2::ConnectionError>
  Connect(Thread fidl_thread,
          fidl::InterfaceRequest<fuchsia::media2::StreamSink> stream_sink_request) {
    FX_CHECK(stream_sink_request);

    return ConnectInternal(std::move(fidl_thread), std::move(stream_sink_request), nullptr);
  }

 private:
  // Returns a promise that creates a |Connection| and returns a unique pointer to it. Deleting
  // the |Connection| disconnects it immediately. If an active connection already exists for this
  // input, the promise returned by this method won't complete until the prior connection is
  // disconnected.
  [[nodiscard]] fpromise::promise<std::unique_ptr<Connection>, fuchsia::media2::ConnectionError>
  ConnectInternal(Thread fidl_thread,
                  fidl::InterfaceRequest<fuchsia::media2::StreamSink> stream_sink_request,
                  std::unique_ptr<InputBufferCollection> buffer_collection) {
    FX_CHECK(stream_sink_request);

    auto connection = std::unique_ptr<Connection>(new Connection(std::move(buffer_collection)));

    // Bind the connection on |fidl_thread|. We need to wait for this, so we use a bridge.
    fpromise::bridge<> bridge;
    fidl_thread.schedule_task(connection->Bind(std::move(stream_sink_request))
                                  .and_then([completer = std::move(bridge.completer)]() mutable {
                                    completer.complete_ok();
                                  }));

    // Possibly wait for the previous connection to be drained.
    auto when = when_prior_connection_drained_ ? std::move(when_prior_connection_drained_)
                                               : fpromise::make_ok_promise();
    when_prior_connection_drained_ = connection->WhenDrained();

    return fpromise::join_promises(bridge.consumer.promise(), std::move(when))
        .then([connection = std::move(connection), fidl_thread](
                  fpromise::result<std::tuple<fpromise::result<void, void>,
                                              fpromise::result<void, void>>>& result) mutable
              -> fpromise::result<std::unique_ptr<Connection>, fuchsia::media2::ConnectionError> {
          // We don't care if the prior |WhenDrained| promise fails.
          return fpromise::ok(std::move(connection));
        });
  }

  fpromise::promise<> when_prior_connection_drained_;
  fpromise::scope scope_;
};

}  // namespace fmlib

#endif  // SRC_MEDIA_VNEXT_LIB_STREAM_IO_INPUT_H_
