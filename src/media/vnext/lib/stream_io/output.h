// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_VNEXT_LIB_STREAM_IO_OUTPUT_H_
#define SRC_MEDIA_VNEXT_LIB_STREAM_IO_OUTPUT_H_

#include <fuchsia/media2/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>

#include "src/media/vnext/lib/stream_io/buffer_collection.h"
#include "src/media/vnext/lib/stream_sink/stream_queue.h"
#include "src/media/vnext/lib/stream_sink/stream_sink_client.h"
#include "src/media/vnext/lib/threads/thread.h"

namespace fmlib {

// An output through which a producer sends a stream of packets. |T| is the internal packet type,
// which must be moveable and have a specialization defined for |ToPacketConverter|.
template <typename T>
class Output {
 public:
  // An active output connection.
  class Connection {
   public:
    ~Connection() = default;

    // Disallow copy, assign and move.
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;
    Connection(Connection&&) = delete;
    Connection& operator=(Connection&&) = delete;

    // Indicates whether this output connection is still connected.
    bool is_connected() const { return stream_sink_client_.is_connected(); }

    // Returns a promise that completes successfully when this output connection is already
    // disconnected and completes with an error when the connection is disconnected unexpectedly.
    // The |zx_status_t| returned indicates the connection error that occurred, the epitaph for the
    // channel or |ZX_ERR_PEER_CLOSED|. The promise is abandoned when this connection is destroyed
    // or passed in a call to |Output::DrainAndDisconnect|.
    [[nodiscard]] fpromise::promise<void, zx_status_t> WhenDisconnected() {
      return stream_sink_client_.WhenDisconnected();
    }

    // Returns a reference to the buffer collection owned by this connection.
    OutputBufferCollection& buffer_collection() {
      FX_CHECK(buffer_collection_);
      return *buffer_collection_;
    }

    // Enqueues a packet if this connection is connected, does nothing otherwise.
    template <typename U>
    void Push(U&& element) {
      if (is_connected()) {
        stream_queue_.push(std::move(element));
      }
    }

    // Enqueues a packet if this connection is connected, does nothing otherwise.
    template <typename U>
    void Push(const U& element) {
      if (is_connected()) {
        stream_queue_.push(element);
      }
    }

    // Enqueues an 'ended' indication if this connection is connected, does nothing
    // otherwise.
    void End() {
      if (is_connected()) {
        stream_queue_.end();
      }
    }

    // Clears the queue and enqueues a 'cleared' indication if this connection is connected,
    // does nothing otherwise.
    void Clear(bool hold_last_frame, zx::eventpair completion_fence) {
      if (is_connected()) {
        stream_queue_.clear(ClearRequest(hold_last_frame, std::move(completion_fence)));
      }
    }

   private:
    Connection(Thread fidl_thread, fuchsia::media2::StreamSinkHandle stream_sink_handle,
               std::unique_ptr<OutputBufferCollection> buffer_collection)
        : buffer_collection_(std::move(buffer_collection)) {
      FX_CHECK(stream_sink_handle);
      fidl_thread.schedule_task(
          fpromise::make_promise([this, fidl_thread,
                                  stream_sink_handle = std::move(stream_sink_handle)]() mutable {
            stream_sink_client_.Connect(fidl_thread.executor(), &stream_queue_,
                                        std::move(stream_sink_handle));
          }).wrap_with(scope_));
    }

    // Drains this connection and returns a promise that completes when the connection is drained.
    [[nodiscard]] fpromise::promise<> Drain() {
      stream_queue_.drain();
      return stream_sink_client_.WhenDrained();
    }

    StreamQueue<T, ClearRequest> stream_queue_;
    StreamSinkClient<T> stream_sink_client_;
    std::unique_ptr<OutputBufferCollection> buffer_collection_;
    fpromise::scope scope_;

    friend class Output;
  };

  // Constructs an |Output|.
  Output() = default;

  ~Output() = default;

  // Disallow copy, assign and move.
  Output(const Output&) = delete;
  Output& operator=(const Output&) = delete;
  Output(Output&&) = delete;
  Output& operator=(Output&&) = delete;

  using ConnectResult =
      fpromise::result<std::unique_ptr<Connection>, fuchsia::media2::ConnectionError>;

  // Returns a promise that creates a |Connection| and returns a unique pointer to it. Deleting
  // the |Connection| disconnects it immediately. Passing it to |DrainAndDisconnect| disconnects
  // it after all packets and signals (errors) have been forwarded to the connected input.
  //
  // This overload is used when payloads must be mapped into system memory. A buffer collection is
  // created and populated using the last three parameters. The closure does not complete until the
  // buffer collection is populated.
  [[nodiscard]] fpromise::promise<std::unique_ptr<Connection>, fuchsia::media2::ConnectionError>
  Connect(Thread fidl_thread, fuchsia::media2::StreamSinkHandle stream_sink_handle,
          fuchsia::media2::BufferProvider& buffer_provider, zx::eventpair buffer_collection_token,
          fuchsia::media2::BufferConstraints constraints) {
    FX_CHECK(stream_sink_handle);
    FX_CHECK(buffer_collection_token);

    return OutputBufferCollection::Create(fidl_thread.executor(), buffer_provider,
                                          std::move(buffer_collection_token), constraints, "output",
                                          0)
        .and_then(
            [fidl_thread, stream_sink_handle = std::move(stream_sink_handle)](
                std::unique_ptr<OutputBufferCollection>& buffer_collection) mutable
            -> fpromise::result<std::unique_ptr<Connection>, fuchsia::media2::ConnectionError> {
              return fpromise::ok(std::unique_ptr<Connection>(new Connection(
                  fidl_thread, std::move(stream_sink_handle), std::move(buffer_collection))));
            })
        .wrap_with(scope_);
  }

  // Returns a promise that creates a |Connection| and returns a unique pointer to it. Deleting
  // the |Connection| disconnects it immediately. Passing it to |DrainAndDisconnect| disconnects
  // it after all packets and signals (errors) have been forwarded to the connected input.
  //
  // This overload is used when payloads should not be mapped into system memory. No buffer
  // collection is established, and outgoing packets have no local memory pointers for in-proc
  // access. The caller is expected to handle interaction with the buffer provider, and the
  // returned promise completes regardless of whether a buffer collection has been negotiated.
  [[nodiscard]] fpromise::promise<std::unique_ptr<Connection>, fuchsia::media2::ConnectionError>
  Connect(Thread fidl_thread, fuchsia::media2::StreamSinkHandle stream_sink_handle) {
    FX_CHECK(stream_sink_handle);

    return fpromise::make_ok_promise(std::unique_ptr<Connection>(
        new Connection(std::move(fidl_thread), std::move(stream_sink_handle), nullptr)));
  }

  // Returns a promise that completes when |connection| has forwarded all packets and signals
  // (errors) to the connected input, and the connection has been disconnected.
  [[nodiscard]] fpromise::promise<> DrainAndDisconnect(std::unique_ptr<Connection> connection) {
    FX_CHECK(connection);

    auto connection_raw = connection.get();
    return connection_raw->Drain().inspect(
        [connection = std::move(connection)](const fpromise::result<void, void>& result) {
          // |connection| goes out of scope here, disconnecting the channel.
        });
  }

 private:
  fpromise::scope scope_;
};

}  // namespace fmlib

#endif  // SRC_MEDIA_VNEXT_LIB_STREAM_IO_OUTPUT_H_
