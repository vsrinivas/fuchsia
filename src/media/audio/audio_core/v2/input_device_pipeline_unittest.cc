// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/v2/input_device_pipeline.h"

#include <lib/async-testing/test_loop.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/clock.h>

#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/media/audio/audio_core/v2/reference_clock.h"
#include "src/media/audio/audio_core/v2/testing/fake_graph_server.h"
#include "src/media/audio/audio_core/v2/testing/matchers.h"
#include "src/media/audio/services/common/fidl_thread.h"
#include "src/media/audio/services/common/logging.h"

using ::media::audio::CaptureUsage;
using ::media::audio::StreamUsage;
using ::media::audio::VolumeCurve;
using InputDeviceProfile = ::media::audio::DeviceConfig::InputDeviceProfile;

using ::fuchsia_audio_mixer::GraphCreateEdgeRequest;
using ::fuchsia_audio_mixer::GraphCreateMixerRequest;
using ::fuchsia_audio_mixer::GraphCreateProducerRequest;
using ::fuchsia_audio_mixer::GraphCreateSplitterRequest;
using ::fuchsia_audio_mixer::GraphDeleteNodeRequest;

using ::testing::UnorderedElementsAreArray;

namespace media_audio {
namespace {

constexpr uint32_t kClockDomain = 42;
const Format kFormatIntMono = Format::CreateOrDie({fuchsia_audio::SampleType::kInt32, 1, 1000});
const Format kFormatFloatMono = Format::CreateOrDie({fuchsia_audio::SampleType::kFloat32, 1, 1000});
const Format kFormatIntStereo = Format::CreateOrDie({fuchsia_audio::SampleType::kInt32, 2, 1000});
const Format kFormatFloatStereo =
    Format::CreateOrDie({fuchsia_audio::SampleType::kFloat32, 2, 1000});

struct TestHarness {
  TestHarness();
  ~TestHarness();

  async::TestLoop loop;
  std::shared_ptr<fidl::WireSharedClient<fuchsia_audio_mixer::Graph>> client;
  std::shared_ptr<FakeGraphServer> server;
  ReferenceClock reference_clock;
};

TestHarness::TestHarness() {
  auto endpoints = fidl::CreateEndpoints<fuchsia_audio_mixer::Graph>();
  if (!endpoints.is_ok()) {
    FX_PLOGS(FATAL, endpoints.status_value()) << "fidl::CreateEndpoints failed";
  }

  client = std::make_shared<fidl::WireSharedClient<fuchsia_audio_mixer::Graph>>(
      std::move(endpoints->client), loop.dispatcher());
  server = FakeGraphServer::Create(FidlThread::CreateFromCurrentThread("test", loop.dispatcher()),
                                   std::move(endpoints->server));

  EXPECT_EQ(zx::clock::create(ZX_CLOCK_OPT_MONOTONIC | ZX_CLOCK_OPT_CONTINUOUS, nullptr,
                              &reference_clock.handle),
            ZX_OK);
  reference_clock.domain = kClockDomain;
}

TestHarness::~TestHarness() {
  client = nullptr;
  loop.RunUntilIdle();
  EXPECT_TRUE(server->WaitForShutdown(zx::nsec(0)));
}

void ValidateCreateSourceNodeForFormatCalls(const Format& format, const NodeId root_splitter_id,
                                            const NodeId mixer_id, const NodeId splitter_id,
                                            const ThreadId thread_id,
                                            const std::vector<FakeGraphServer::CallType>& calls,
                                            size_t start_index) {
  auto format_with_float32 = Format::CreateOrDie({
      .sample_type = fuchsia_audio::SampleType::kFloat32,
      .channels = format.channels(),
      .frames_per_second = format.frames_per_second(),
  });

  auto call0 = std::get_if<GraphCreateMixerRequest>(&calls[start_index + 0]);
  auto call1 = std::get_if<GraphCreateSplitterRequest>(&calls[start_index + 1]);

  ASSERT_TRUE(call0);
  EXPECT_EQ(call0->direction(), fuchsia_audio_mixer::PipelineDirection::kInput);
  EXPECT_THAT(call0->dest_format(), FidlFormatEq(format_with_float32));
  EXPECT_THAT(call0->dest_reference_clock(), ValidReferenceClock(kClockDomain));

  ASSERT_TRUE(call1);
  EXPECT_EQ(call1->direction(), fuchsia_audio_mixer::PipelineDirection::kInput);
  EXPECT_THAT(call1->format(), FidlFormatEq(format_with_float32));
  EXPECT_EQ(call1->thread(), thread_id);
  EXPECT_THAT(call1->reference_clock(), ValidReferenceClock(kClockDomain));

  EXPECT_THAT(calls[start_index + 2], CreateEdgeEq(root_splitter_id, mixer_id));
  EXPECT_THAT(calls[start_index + 3], CreateEdgeEq(mixer_id, splitter_id));
}

void ValidateDeletedNodes(const std::vector<FakeGraphServer::CallType>& calls, size_t start_index,
                          const std::vector<NodeId> expected_deletions) {
  // Since `InputDevicePipeline::created_nodes_` is unordered, these can arrive in any order.
  std::vector<NodeId> deleted;
  for (size_t k = start_index; k < start_index + expected_deletions.size(); k++) {
    SCOPED_TRACE("call[" + std::to_string(k) + "]");
    auto call = std::get_if<GraphDeleteNodeRequest>(&calls[k]);
    ASSERT_TRUE(call);
    ASSERT_TRUE(call->id());
    deleted.push_back(*(call->id()));
  }

  EXPECT_THAT(deleted, UnorderedElementsAreArray(expected_deletions));
}

TEST(InputDevicePipelineTest, CreateForDevice) {
  static constexpr auto kThreadId = 100;
  static constexpr auto kInitialDelay = zx::nsec(500);

  TestHarness h;
  fidl::Arena<> arena;

  std::shared_ptr<InputDevicePipeline> pipeline;
  InputDevicePipeline::CreateForDevice({
      .graph_client = h.client,
      .producer =
          {
              .ring_buffer = fuchsia_audio::wire::RingBuffer::Builder(arena)
                                 .format(kFormatIntMono.ToWireFidl(arena))
                                 .reference_clock(h.reference_clock.DupHandle())
                                 .reference_clock_domain(h.reference_clock.domain)
                                 .Build(),
              .external_delay_watcher =
                  fuchsia_audio_mixer::wire::ExternalDelayWatcher::Builder(arena)
                      .initial_delay(kInitialDelay.get())
                      .Build(),
          },
      .config =
          InputDeviceProfile(static_cast<uint32_t>(kFormatIntMono.frames_per_second()),
                             {
                                 StreamUsage::WithCaptureUsage(CaptureUsage::BACKGROUND),
                                 StreamUsage::WithCaptureUsage(CaptureUsage::FOREGROUND),
                             },
                             VolumeCurve::DefaultForMinGain(VolumeCurve::kDefaultGainForMinVolume),
                             /*driver_gain_db=*/0.0f, /*software_gain_db=*/0.0f),
      .thread = kThreadId,
      .callback =
          [&pipeline](auto p) {
            ASSERT_TRUE(p);
            pipeline = std::move(p);
          },
  });

  h.loop.RunUntilIdle();
  ASSERT_TRUE(pipeline);

  EXPECT_FALSE(pipeline->SupportsUsage(CaptureUsage::LOOPBACK));
  EXPECT_TRUE(pipeline->SupportsUsage(CaptureUsage::BACKGROUND));
  EXPECT_TRUE(pipeline->SupportsUsage(CaptureUsage::FOREGROUND));
  EXPECT_FALSE(pipeline->SupportsUsage(CaptureUsage::SYSTEM_AGENT));
  EXPECT_FALSE(pipeline->SupportsUsage(CaptureUsage::COMMUNICATION));

  // Should not created 2 nodes with one edge connecting them.
  EXPECT_EQ(h.server->calls().size(), 3u);

  static constexpr NodeId kProducerId = 1;
  static constexpr NodeId kRootSplitterId = 2;
  static constexpr NodeId kStereoMixerId = 3;
  static constexpr NodeId kStereoSplitterId = 4;

  // When the format matches the device, this call should return immediately.
  {
    SCOPED_TRACE("CreateSourceNode same format as device");
    bool done = false;
    pipeline->CreateSourceNodeForFormat(kFormatIntMono, [&done](auto node) {
      EXPECT_EQ(node, kRootSplitterId);
      done = true;
    });
    EXPECT_TRUE(done);
    EXPECT_EQ(h.server->calls().size(), 3u);
  }

  {
    SCOPED_TRACE("CreateSourceNode same format as device w/ float samples");
    bool done = false;
    pipeline->CreateSourceNodeForFormat(kFormatFloatMono, [&done](auto node) {
      EXPECT_EQ(node, kRootSplitterId);
      done = true;
    });
    EXPECT_TRUE(done);
    EXPECT_EQ(h.server->calls().size(), 3u);
  }

  // With a new format, we should create a new source node.
  {
    SCOPED_TRACE("CreateSourceNode new format");
    bool done = false;
    pipeline->CreateSourceNodeForFormat(kFormatIntStereo, [&done](auto node) {
      EXPECT_EQ(node, kStereoSplitterId);
      done = true;
    });
    h.loop.RunUntilIdle();
    EXPECT_TRUE(done);
    // creates 2 nodes and 2 edges
    EXPECT_EQ(h.server->calls().size(), 7u);
  }

  // With that a compatible format, this should return immediately.
  {
    SCOPED_TRACE("CreateSourceNode same format as prior");
    bool done = false;
    pipeline->CreateSourceNodeForFormat(kFormatFloatStereo, [&done](auto node) {
      EXPECT_EQ(node, kStereoSplitterId);
      done = true;
    });
    EXPECT_TRUE(done);
    EXPECT_EQ(h.server->calls().size(), 7u);
  }

  // Adds 4 DeleteNode calls.
  pipeline->Destroy();
  h.loop.RunUntilIdle();

  // Check the graph calls.
  const auto& calls = h.server->calls();
  ASSERT_EQ(calls.size(), 11u);

  auto call0 = std::get_if<GraphCreateProducerRequest>(&calls[0]);
  auto call1 = std::get_if<GraphCreateSplitterRequest>(&calls[1]);
  EXPECT_THAT(calls[2], CreateEdgeEq(kProducerId, kRootSplitterId));

  // The ProducerNode should be created with the same format as the RingBuffer.
  ASSERT_TRUE(call0);
  EXPECT_EQ(call0->direction(), fuchsia_audio_mixer::PipelineDirection::kInput);
  ASSERT_TRUE(call0->data_source());
  ASSERT_TRUE(call0->data_source()->ring_buffer().has_value());
  EXPECT_THAT(call0->data_source()->ring_buffer()->format(), FidlFormatEq(kFormatIntMono));
  ASSERT_TRUE(call0->external_delay_watcher());
  EXPECT_EQ(call0->external_delay_watcher()->initial_delay(), kInitialDelay.get());

  // The root SplitterNode should have that same format.
  ASSERT_TRUE(call1);
  EXPECT_EQ(call1->direction(), fuchsia_audio_mixer::PipelineDirection::kInput);
  EXPECT_THAT(call1->format(), FidlFormatEq(kFormatIntMono));
  EXPECT_EQ(call1->thread(), kThreadId);
  EXPECT_THAT(call1->reference_clock(), ValidReferenceClock(kClockDomain));

  // Extra nodes are created to handle stereo formats.
  ValidateCreateSourceNodeForFormatCalls(kFormatIntStereo,
                                         /*root_splitter_id=*/kRootSplitterId,
                                         /*mixer_id=*/kStereoMixerId,
                                         /*splitter_id=*/kStereoSplitterId, kThreadId, calls, 3);

  ValidateDeletedNodes(calls, 7, {kProducerId, kRootSplitterId, kStereoMixerId, kStereoSplitterId});
}

TEST(InputDevicePipelineTest, CreateForLoopback) {
  static constexpr auto kThreadId = 100;
  static constexpr auto kLoopbackSplitterId = 99;
  static constexpr auto kStereoMixerId = 1;
  static constexpr auto kStereoSplitterId = 2;

  TestHarness h;
  auto pipeline = InputDevicePipeline::CreateForLoopback({
      .graph_client = h.client,
      .splitter_node = kLoopbackSplitterId,
      .format = kFormatIntMono,
      .reference_clock = h.reference_clock.Dup(),
      .thread = kThreadId,
  });
  ASSERT_TRUE(pipeline);

  EXPECT_TRUE(pipeline->SupportsUsage(CaptureUsage::LOOPBACK));
  EXPECT_FALSE(pipeline->SupportsUsage(CaptureUsage::BACKGROUND));
  EXPECT_FALSE(pipeline->SupportsUsage(CaptureUsage::FOREGROUND));
  EXPECT_FALSE(pipeline->SupportsUsage(CaptureUsage::SYSTEM_AGENT));
  EXPECT_FALSE(pipeline->SupportsUsage(CaptureUsage::COMMUNICATION));

  // Should not have created any nodes.
  EXPECT_EQ(h.server->calls().size(), 0u);

  // When the format matches the splitter, this call should return immediately.
  {
    SCOPED_TRACE("CreateSourceNode same format as splitter");
    bool done = false;
    pipeline->CreateSourceNodeForFormat(kFormatIntMono, [&done](auto node) {
      EXPECT_EQ(node, kLoopbackSplitterId);
      done = true;
    });
    EXPECT_TRUE(done);
    EXPECT_EQ(h.server->calls().size(), 0u);
  }

  {
    SCOPED_TRACE("CreateSourceNode same format as splitter w/ float samples");
    bool done = false;
    pipeline->CreateSourceNodeForFormat(kFormatFloatMono, [&done](auto node) {
      EXPECT_EQ(node, kLoopbackSplitterId);
      done = true;
    });
    EXPECT_TRUE(done);
    EXPECT_EQ(h.server->calls().size(), 0u);
  }

  // With a new format, we should create a new source node.
  {
    SCOPED_TRACE("CreateSourceNode new format");
    bool done = false;
    pipeline->CreateSourceNodeForFormat(kFormatIntStereo, [&done](auto node) {
      EXPECT_EQ(node, kStereoSplitterId);
      done = true;
    });
    h.loop.RunUntilIdle();
    EXPECT_TRUE(done);
    // creates 2 nodes and 2 edges
    EXPECT_EQ(h.server->calls().size(), 4u);
  }

  // With that a compatible format, this should return immediately.
  {
    SCOPED_TRACE("CreateSourceNode same format as prior");
    bool done = false;
    pipeline->CreateSourceNodeForFormat(kFormatFloatStereo, [&done](auto node) {
      EXPECT_EQ(node, kStereoSplitterId);
      done = true;
    });
    EXPECT_TRUE(done);
    EXPECT_EQ(h.server->calls().size(), 4u);
  }

  // Adds 2 DeleteNode calls.
  pipeline->Destroy();
  h.loop.RunUntilIdle();

  // Check the graph calls.
  const auto& calls = h.server->calls();
  ASSERT_EQ(calls.size(), 6u);

  ValidateCreateSourceNodeForFormatCalls(kFormatIntStereo,
                                         /*root_splitter_id=*/kLoopbackSplitterId,
                                         /*mixer_id=*/1,
                                         /*splitter_id=*/2, kThreadId, calls, 0);

  ValidateDeletedNodes(calls, 4, {kStereoMixerId, kStereoSplitterId});
}

}  // namespace
}  // namespace media_audio
