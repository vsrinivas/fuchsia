// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_SERVICES_MIXER_FIDL_REALTIME_TESTING_TEST_STREAM_SINK_SERVER_AND_CLIENT_H_
#define SRC_MEDIA_AUDIO_SERVICES_MIXER_FIDL_REALTIME_TESTING_TEST_STREAM_SINK_SERVER_AND_CLIENT_H_

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>

#include <gtest/gtest.h>

#include "src/media/audio/services/common/logging.h"
#include "src/media/audio/services/common/testing/test_server_and_client.h"
#include "src/media/audio/services/mixer/fidl_realtime/stream_sink_server.h"

namespace media_audio {

// A wrapper around a TestServerAndClient<StreamSinkServer>> which adds some extra functionality for
// StreamSinkServers.
class TestStreamSinkServerAndClient {
 public:
  TestStreamSinkServerAndClient(std::shared_ptr<FidlThread> thread, uint32_t payload_buffer_id,
                                uint64_t payload_buffer_size, const Format& format,
                                TimelineRate media_ticks_per_ns)
      : thread_(thread) {
    payload_buffer_ = MemoryMappedBuffer::CreateOrDie(payload_buffer_size, true);
    wrapper_ = std::make_unique<TestServerAndClient<StreamSinkServer>>(
        thread, StreamSinkServer::Args{
                    .format = format,
                    .media_ticks_per_ns = media_ticks_per_ns,
                    .payload_buffers = {{{payload_buffer_id, payload_buffer_}}},
                });
  }

  StreamSinkServer& server() { return wrapper_->server(); }
  std::shared_ptr<StreamSinkServer> server_ptr() { return wrapper_->server_ptr(); }
  fidl::WireSyncClient<fuchsia_media2::StreamSink>& client() { return wrapper_->client(); }

  // Returns a pointer into the payload buffer at the given offset.
  void* PayloadBufferOffset(int64_t offset) {
    return static_cast<char*>(payload_buffer_->start()) + offset;
  }

  // Calls `client()->PutPacket` and wants for that call to complete.
  // Should be called with ASSERT_NO_FATAL_FAILURE(..).
  void PutPacket(fuchsia_media2::wire::PayloadRange payload,
                 fuchsia_media2::wire::PacketTimestamp timestamp, zx::eventpair fence) {
    std::vector<fuchsia_media2::wire::PayloadRange> payloads{payload};
    auto result = client()->PutPacket(
        {
            .payload = fidl::VectorView<fuchsia_media2::wire::PayloadRange>(arena_, payloads),
            .timestamp = timestamp,
        },
        std::move(fence));

    ASSERT_TRUE(result.ok()) << result.status_string();
    ASSERT_TRUE(WaitForNextCall());
  }

  // Blocks until the next FIDL call completes. Returns false on timeout.
  bool WaitForNextCall(zx::duration timeout = zx::sec(5)) {
    return PollServerUntil(zx::deadline_after(timeout), [this]() {
      ScopedThreadChecker checker(server().thread().checker());
      if (auto completed = server().fidl_calls_completed_; completed > fidl_calls_completed_) {
        fidl_calls_completed_++;
        return true;
      }
      return false;
    });
  }

 private:
  bool PollServerUntil(zx::time deadline, std::function<bool()> is_done) {
    while (deadline > zx::clock::get_monotonic()) {
      libsync::Completion task_done;
      bool polling_done = false;
      thread_->PostTask([&task_done, &polling_done, &is_done]() mutable {
        polling_done = is_done();
        task_done.Signal();
      });
      if (task_done.Wait(deadline) != ZX_OK) {
        return false;
      }
      if (polling_done) {
        return true;
      }
      zx::nanosleep(zx::deadline_after(zx::msec(5)));
    }
    return false;
  }

  fidl::Arena<> arena_;
  std::shared_ptr<MemoryMappedBuffer> payload_buffer_;
  std::shared_ptr<FidlThread> thread_;
  std::unique_ptr<TestServerAndClient<StreamSinkServer>> wrapper_;

  int64_t fidl_calls_completed_{0};
};

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_SERVICES_MIXER_FIDL_REALTIME_TESTING_TEST_STREAM_SINK_SERVER_AND_CLIENT_H_
