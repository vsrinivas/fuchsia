// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/fidl_realtime/stream_sink_client.h"

#include <lib/sync/cpp/completion.h>
#include <lib/syslog/cpp/macros.h>

#include <mutex>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/media/audio/services/common/logging.h"

namespace media_audio {
namespace {

using Packet = StreamSinkClient::Packet;
using PacketQueue = StreamSinkClient::PacketQueue;
using ::fuchsia_mediastreams::wire::AudioSampleFormat;

// These tests work best if we use a format with >= 2 bytes per frame to ensure we compute frame
// counts correctly. Other than that constraint, the specific choice of format does not matter.
const auto kFormat = Format::CreateOrDie({AudioSampleFormat::kFloat, 2, 48000});
const auto kFramesPerPacket = 10;
const auto kBytesPerPacket = kFramesPerPacket * kFormat.bytes_per_frame();
const auto stream_converter = StreamConverter::Create(kFormat, kFormat);

struct TestHarness {
  fidl::Endpoints<fuchsia_media2::StreamSink> endpoints;
  std::map<uint32_t, std::shared_ptr<MemoryMappedBuffer>> payload_buffers;
  std::shared_ptr<PacketQueue> recycled_packet_queue;
  std::shared_ptr<const FidlThread> thread;
  std::shared_ptr<StreamSinkClient> client;
};

TestHarness MakeTestHarness(
    std::map<uint32_t, std::shared_ptr<MemoryMappedBuffer>> payload_buffers) {
  auto endpoints = fidl::CreateEndpoints<fuchsia_media2::StreamSink>();
  if (!endpoints.is_ok()) {
    FX_PLOGS(FATAL, endpoints.status_value()) << "fidl::CreateEndpoints failed";
  }

  TestHarness h;
  h.endpoints = std::move(*endpoints);
  h.payload_buffers = std::move(payload_buffers);
  h.recycled_packet_queue = std::make_shared<PacketQueue>();
  h.thread = FidlThread::CreateFromNewThread("test_fidl_client_thread");
  h.client = std::make_shared<StreamSinkClient>(StreamSinkClient::Args{
      .format = kFormat,
      .frames_per_packet = kFramesPerPacket,
      .client = fidl::WireSharedClient(std::move(h.endpoints.client), h.thread->dispatcher()),
      .payload_buffers = std::move(h.payload_buffers),
      .recycled_packet_queue = h.recycled_packet_queue,
      .thread = h.thread,
  });
  return h;
}

bool PollUntil(zx::duration timeout, std::function<bool()> fn) {
  const auto deadline = zx::deadline_after(zx::sec(5));
  while (zx::clock::get_monotonic() < deadline) {
    if (fn()) {
      return true;
    }
    zx::nanosleep(zx::deadline_after(zx::msec(10)));
  }
  return false;
}

TEST(StreamSinkClientTest, CreatePackets) {
  auto h = MakeTestHarness({
      {0, MemoryMappedBuffer::CreateOrDie(30 * kFormat.bytes_per_frame(), /*writable=*/true)},
      {1, MemoryMappedBuffer::CreateOrDie(25 * kFormat.bytes_per_frame(), /*writable=*/true)},
  });

  fidl::Arena<> arena;

  // With 10 frames per packet, we should have 3 packets from buffer 0, 2 packets from buffer 1.
  for (uint32_t i = 0; i < 2; i++) {
    for (int64_t k = 0; k < ((i == 0) ? 3 : 2); k++) {
      SCOPED_TRACE("buffer" + std::to_string(i) + ", packet" + std::to_string(k));
      auto packet = h.recycled_packet_queue->pop();
      ASSERT_TRUE(packet);
      (*packet)->Recycle(stream_converter, std::nullopt);

      auto fidl = (*packet)->ToFidl(arena);
      ASSERT_EQ(fidl.payload.count(), 1u);
      EXPECT_EQ(fidl.payload[0].buffer_id, i);
      EXPECT_EQ(fidl.payload[0].offset, static_cast<uint64_t>(k * kBytesPerPacket));
      EXPECT_EQ((*packet)->FramesRemaining(), kFramesPerPacket);
    }
  }

  // No more packets.
  ASSERT_FALSE(h.recycled_packet_queue->pop());
}

TEST(StreamSinkClientTest, RecyclePackets) {
  // Local server implementation.
  class StreamSinkServer : public fidl::WireServer<fuchsia_media2::StreamSink> {
   public:
    libsync::Completion& packet_received() { return packet_received_; }
    libsync::Completion& end_received() { return end_received_; }

    void DropFence() {
      std::lock_guard<std::mutex> guard(mutex_);
      fence_ = zx::eventpair();
    }

    // Implementation of fidl::WireServer<fuchsia_media2::StreamSink>.
    void PutPacket(PutPacketRequestView request, PutPacketCompleter::Sync& completer) final {
      ASSERT_EQ(request->packet.payload.count(), 1u);
      EXPECT_EQ(request->packet.payload[0].buffer_id, 0u);
      EXPECT_EQ(request->packet.payload[0].offset, 0u);
      EXPECT_EQ(request->packet.payload[0].size, static_cast<uint64_t>(kBytesPerPacket));
      ASSERT_TRUE(request->release_fence.is_valid());
      {
        std::lock_guard<std::mutex> guard(mutex_);
        fence_ = std::move(request->release_fence);
      }
      packet_received_.Signal();
    }

    void End(EndCompleter::Sync& completer) final { end_received_.Signal(); }
    void Clear(ClearRequestView request, ClearCompleter::Sync& completer) final {}

   private:
    libsync::Completion packet_received_;
    libsync::Completion end_received_;

    std::mutex mutex_;
    TA_GUARDED(mutex_) zx::eventpair fence_;
  };

  // This test needs just one packet.
  auto h =
      MakeTestHarness({{0, MemoryMappedBuffer::CreateOrDie(kBytesPerPacket, /*writable=*/true)}});

  auto server = std::make_shared<StreamSinkServer>();
  auto server_thread = FidlThread::CreateFromNewThread("test_fidl_server_thread");
  auto binding =
      fidl::BindServer(server_thread->dispatcher(), std::move(h.endpoints.server), server);

  // Pop that packet and send to the server.
  auto packet = h.recycled_packet_queue->pop();
  ASSERT_TRUE(packet);
  ASSERT_EQ(h.recycled_packet_queue->pop(), std::nullopt);

  (*packet)->Recycle(stream_converter, std::nullopt);
  (*packet)->AppendSilence(kFramesPerPacket);
  h.client->PutPacket(std::move(*packet));
  ASSERT_EQ(server->packet_received().Wait(zx::sec(5)), ZX_OK);

  // Send an End message.
  h.client->End();
  ASSERT_EQ(server->end_received().Wait(zx::sec(5)), ZX_OK);

  // No packets available yet.
  ASSERT_EQ(h.recycled_packet_queue->pop(), std::nullopt);

  // After the server releases the packet's fence, the packet should be recycled.
  server->DropFence();
  EXPECT_TRUE(PollUntil(zx::sec(5), [&h]() {
    auto packet = h.recycled_packet_queue->pop();
    if (!packet) {
      return false;
    }
    fidl::Arena<> arena;
    auto fidl = (*packet)->ToFidl(arena);
    EXPECT_EQ(fidl.payload.count(), 1u);
    EXPECT_EQ(fidl.payload[0].buffer_id, 0u);
    EXPECT_EQ(fidl.payload[0].offset, static_cast<uint64_t>(0));
    EXPECT_EQ(fidl.payload[0].size, static_cast<uint64_t>(kBytesPerPacket));
    return true;
  }));
}

TEST(StreamSinkClientTest, Shutdown) {
  // Local server implementation.
  class StreamSinkServer : public fidl::WireServer<fuchsia_media2::StreamSink> {
   public:
    libsync::Completion& packet_received() { return packet_received_; }
    int64_t put_packet_calls() {
      std::lock_guard<std::mutex> guard(mutex_);
      return put_packet_calls_;
    }
    int64_t end_calls() {
      std::lock_guard<std::mutex> guard(mutex_);
      return end_calls_;
    }

    // Implementation of fidl::WireServer<fuchsia_media2::StreamSink>.
    void PutPacket(PutPacketRequestView request, PutPacketCompleter::Sync& completer) final {
      ASSERT_TRUE(request->release_fence.is_valid());

      std::lock_guard<std::mutex> guard(mutex_);
      if (put_packet_calls_ == 0) {
        fence_ = std::move(request->release_fence);
      }
      put_packet_calls_++;
      packet_received_.Signal();
    }
    void End(EndCompleter::Sync& completer) final {
      std::lock_guard<std::mutex> guard(mutex_);
      end_calls_++;
    }
    void Clear(ClearRequestView request, ClearCompleter::Sync& completer) final {}

   private:
    libsync::Completion packet_received_;
    zx::eventpair fence_;

    std::mutex mutex_;
    TA_GUARDED(mutex_) int64_t put_packet_calls_ = 0;
    TA_GUARDED(mutex_) int64_t end_calls_ = 0;
  };

  // This test needs two packets.
  auto h = MakeTestHarness(
      {{0, MemoryMappedBuffer::CreateOrDie(2 * kBytesPerPacket, /*writable=*/true)}});

  libsync::Completion server_unbound;
  auto server = std::make_shared<StreamSinkServer>();
  auto server_thread = FidlThread::CreateFromNewThread("test_fidl_server_thread");
  auto binding =
      fidl::BindServer(server_thread->dispatcher(), std::move(h.endpoints.server), server,
                       [&server_unbound](StreamSinkServer* server, fidl::UnbindInfo info,
                                         fidl::ServerEnd<fuchsia_media2::StreamSink> server_end) {
                         EXPECT_TRUE(info.is_peer_closed());
                         server_unbound.Signal();
                       });

  // Send a packet and wait until the server is processing that packet.
  {
    auto packet = h.recycled_packet_queue->pop();
    ASSERT_TRUE(packet);
    (*packet)->Recycle(stream_converter, std::nullopt);
    (*packet)->AppendSilence(kFramesPerPacket);
    h.client->PutPacket(std::move(*packet));
    ASSERT_EQ(server->packet_received().Wait(zx::sec(5)), ZX_OK);
  }

  // Shutdown the client.
  h.client->thread().PostTask([client = h.client]() {
    ScopedThreadChecker checker(client->thread().checker());
    client->Shutdown();
  });

  // Send another packet and an End message.
  {
    auto packet = h.recycled_packet_queue->pop();
    ASSERT_TRUE(packet);
    h.client->PutPacket(std::move(*packet));
    h.client->End();
  }

  // Wait for the server to notice the shutdown.
  EXPECT_EQ(server_unbound.Wait(zx::sec(5)), ZX_OK);

  // Only the first packet should have been received.
  // The other packet and End message happened after Shutdown, so they should be dropped.
  EXPECT_EQ(server->put_packet_calls(), 1);
  EXPECT_EQ(server->end_calls(), 0);
}

}  // namespace
}  // namespace media_audio
