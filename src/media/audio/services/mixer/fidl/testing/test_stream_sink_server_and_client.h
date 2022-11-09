// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_SERVICES_MIXER_FIDL_TESTING_TEST_STREAM_SINK_SERVER_AND_CLIENT_H_
#define SRC_MEDIA_AUDIO_SERVICES_MIXER_FIDL_TESTING_TEST_STREAM_SINK_SERVER_AND_CLIENT_H_

#include <fidl/fuchsia.audio/cpp/wire.h>
#include <fidl/fuchsia.media2/cpp/wire.h>
#include <lib/async-testing/test_loop.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>

#include <gtest/gtest.h>

#include "src/media/audio/services/common/logging.h"
#include "src/media/audio/services/mixer/fidl/stream_sink_server.h"

namespace media_audio {

// A wrapper around a TestServerAndWireSyncClient<StreamSinkServer>> which adds
// some extra functionality for StreamSinkServers.
class TestStreamSinkServerAndClient {
 public:
  TestStreamSinkServerAndClient(async::TestLoop& loop, uint32_t payload_buffer_id,
                                uint64_t payload_buffer_size, const Format& format,
                                TimelineRate media_ticks_per_ns)
      : loop_(loop),
        thread_(FidlThread::CreateFromCurrentThread("TestThreadForStreamSinkServer",
                                                    loop.dispatcher())),
        payload_buffer_(MemoryMappedBuffer::CreateOrDie(payload_buffer_size, true)) {
    auto endpoints = fidl::CreateEndpoints<fuchsia_audio::StreamSink>();
    if (!endpoints.is_ok()) {
      FX_PLOGS(FATAL, endpoints.status_value()) << "fidl::CreateEndpoints failed";
    }
    client_ = fidl::WireClient(std::move(endpoints->client), loop.dispatcher(), &event_watcher_);
    server_ =
        StreamSinkServer::Create(thread_, std::move(endpoints->server),
                                 StreamSinkServer::Args{
                                     .format = format,
                                     .media_ticks_per_ns = media_ticks_per_ns,
                                     .payload_buffers = {{{payload_buffer_id, payload_buffer_}}},
                                     .initial_segment_id = 0,
                                 });
  }

  ~TestStreamSinkServerAndClient() {
    client_ = fidl::WireClient<fuchsia_audio::StreamSink>();
    // RunUntilIdle should run all on_unbound callbacks, so the servers should now be shut down.
    loop_.RunUntilIdle();
    EXPECT_TRUE(server_->WaitForShutdown(zx::nsec(0)));
  }

  StreamSinkServer& server() { return *server_; }
  std::shared_ptr<StreamSinkServer> server_ptr() { return server_; }
  fidl::WireClient<fuchsia_audio::StreamSink>& client() { return client_; }

  // Returns a pointer into the payload buffer at the given offset.
  void* PayloadBufferOffset(int64_t offset) {
    return static_cast<char*>(payload_buffer_->start()) + offset;
  }

  // Calls `client()->PutPacket`.
  // Should be called with ASSERT_NO_FATAL_FAILURE(..).
  void PutPacket(fuchsia_media2::wire::PayloadRange payload,
                 fuchsia_audio::wire::Timestamp timestamp, zx::eventpair fence) {
    auto result =
        client()->PutPacket(fuchsia_audio::wire::StreamSinkPutPacketRequest::Builder(arena_)
                                .packet(fuchsia_audio::wire::Packet::Builder(arena_)
                                            .payload(payload)
                                            .timestamp(timestamp)
                                            .Build())
                                .release_fence(std::move(fence))
                                .Build());
    ASSERT_TRUE(result.ok()) << result;
  }

  // Calls `client()->StartSegment`.
  // Should be called with ASSERT_NO_FATAL_FAILURE(..).
  void StartSegment(int64_t segment_id) {
    auto result =
        client()->StartSegment(fuchsia_audio::wire::StreamSinkStartSegmentRequest::Builder(arena_)
                                   .segment_id(segment_id)
                                   .Build());
    ASSERT_TRUE(result.ok()) << result;
  }

  // Returns the reason passed to the OnWillClose event, or std::nullopt if not event received.
  // Resets the state on returning so the next call will report if another event has happened.
  std::optional<fuchsia_media2::ConsumerClosedReason> on_will_close_reason() {
    return event_watcher_.on_will_close_reason();
  }

 private:
  class ClientEventWatcher : public fidl::WireAsyncEventHandler<fuchsia_audio::StreamSink> {
   public:
    void OnWillClose(fidl::WireEvent<fuchsia_audio::StreamSink::OnWillClose>* event) final {
      ASSERT_TRUE(event->has_reason());
      on_will_close_reason_ = event->reason();
    }

    std::optional<fuchsia_media2::ConsumerClosedReason> on_will_close_reason() {
      return std::exchange(on_will_close_reason_, std::nullopt);
    }

   private:
    std::optional<fuchsia_media2::ConsumerClosedReason> on_will_close_reason_;
  };

  fidl::Arena<> arena_;
  ClientEventWatcher event_watcher_;

  async::TestLoop& loop_;
  std::shared_ptr<FidlThread> thread_;
  std::shared_ptr<MemoryMappedBuffer> payload_buffer_;
  std::shared_ptr<StreamSinkServer> server_;
  fidl::WireClient<fuchsia_audio::StreamSink> client_;
};

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_SERVICES_MIXER_FIDL_TESTING_TEST_STREAM_SINK_SERVER_AND_CLIENT_H_
