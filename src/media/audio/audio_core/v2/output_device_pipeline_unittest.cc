// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/v2/output_device_pipeline.h"

#include <lib/async-testing/test_loop.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/clock.h>

#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/media/audio/audio_core/v2/reference_clock.h"
#include "src/media/audio/audio_core/v2/testing/fake_graph_server.h"
#include "src/media/audio/audio_core/v2/testing/matchers.h"
#include "src/media/audio/effects/test_effects/test_effects_v2.h"
#include "src/media/audio/lib/effects_loader/effects_loader_v2.h"
#include "src/media/audio/lib/format2/format.h"
#include "src/media/audio/services/common/fidl_thread.h"
#include "src/media/audio/services/common/logging.h"

using ::media::audio::EffectsLoaderV2;
using ::media::audio::PipelineConfig;
using ::media::audio::RenderUsage;
using ::media::audio::StreamUsage;
using ::media::audio::TestEffectsV2;
using ::media::audio::VolumeCurve;
using OutputDeviceProfile = ::media::audio::DeviceConfig::OutputDeviceProfile;

using ::fuchsia_audio_mixer::GraphCreateConsumerRequest;
using ::fuchsia_audio_mixer::GraphCreateCustomRequest;
using ::fuchsia_audio_mixer::GraphCreateEdgeRequest;
using ::fuchsia_audio_mixer::GraphCreateMixerRequest;
using ::fuchsia_audio_mixer::GraphCreateSplitterRequest;
using ::fuchsia_audio_mixer::GraphDeleteNodeRequest;

using ::testing::UnorderedElementsAreArray;

namespace media_audio {
namespace {

constexpr ThreadId kThreadId = 100;
constexpr uint32_t kClockDomain = 42;
constexpr auto kInitialDelay = zx::nsec(500);

struct TestHarness {
  TestHarness();
  ~TestHarness();

  async::TestLoop loop;
  std::shared_ptr<fidl::WireSharedClient<fuchsia_audio_mixer::Graph>> client;
  std::shared_ptr<FakeGraphServer> server;
  ReferenceClock reference_clock;

  TestEffectsV2 effects;
  std::unique_ptr<EffectsLoaderV2> effects_loader;
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

  effects.AddEffect({
      .name = "NoOp",
      .process =
          [](uint64_t num_frames, float* input, float* output, float total_applied_gain_for_input,
             std::vector<fuchsia_audio_effects::wire::ProcessMetrics>& metrics_vector) {
            return ZX_OK;
          },
      .process_in_place = false,
      .max_frames_per_call = 128,
      .frames_per_second = 48000,
      .input_channels = 2,
      .output_channels = 2,
  });
  effects.AddEffect({
      .name = "NoOpRechannel2To4",
      .process =
          [](uint64_t num_frames, float* input, float* output, float total_applied_gain_for_input,
             std::vector<fuchsia_audio_effects::wire::ProcessMetrics>& metrics_vector) {
            return ZX_OK;
          },
      .process_in_place = false,
      .max_frames_per_call = 128,
      .frames_per_second = 48000,
      .input_channels = 2,
      .output_channels = 4,
  });

  {
    auto result = EffectsLoaderV2::CreateFromChannel(effects.NewClient());
    FX_CHECK(result.is_ok());
    effects_loader = std::move(result.value());
  }
}

TestHarness::~TestHarness() {
  client = nullptr;
  loop.RunUntilIdle();
  EXPECT_TRUE(server->WaitForShutdown(zx::nsec(0)));
}

std::shared_ptr<OutputDevicePipeline> CreatePipeline(TestHarness& h, const Format& device_format,
                                                     PipelineConfig::MixGroup root) {
  fidl::Arena<> arena;
  std::shared_ptr<OutputDevicePipeline> pipeline;

  OutputDevicePipeline::Create({
      .graph_client = h.client,
      .consumer =
          {
              .thread = kThreadId,
              .ring_buffer = fuchsia_audio::wire::RingBuffer::Builder(arena)
                                 .format(device_format.ToWireFidl(arena))
                                 .reference_clock(h.reference_clock.DupHandle())
                                 .reference_clock_domain(h.reference_clock.domain)
                                 .Build(),
              .external_delay_watcher =
                  fuchsia_audio_mixer::wire::ExternalDelayWatcher::Builder(arena)
                      .initial_delay(kInitialDelay.get())
                      .Build(),
          },
      .config =
          OutputDeviceProfile(/*eligible_for_loopback=*/true,
                              {
                                  // We assume that `root` enables these usages.
                                  StreamUsage::WithRenderUsage(RenderUsage::BACKGROUND),
                                  StreamUsage::WithRenderUsage(RenderUsage::MEDIA),
                                  StreamUsage::WithRenderUsage(RenderUsage::SYSTEM_AGENT),
                                  StreamUsage::WithRenderUsage(RenderUsage::INTERRUPTION),
                                  StreamUsage::WithRenderUsage(RenderUsage::COMMUNICATION),
                              },
                              VolumeCurve::DefaultForMinGain(VolumeCurve::kDefaultGainForMinVolume),
                              /*independent_volume_control=*/true, PipelineConfig(root),
                              /*driver_gain_db=*/0.0f, /*software_gain_db=*/0.0f),
      .effects_loader = std::move(h.effects_loader),
      .callback =
          [&pipeline](auto p) {
            ASSERT_TRUE(p);
            pipeline = std::move(p);
          },
  });

  h.loop.RunUntilIdle();
  if (!pipeline) {
    return nullptr;
  }

  EXPECT_TRUE(pipeline->SupportsUsage(RenderUsage::BACKGROUND));
  EXPECT_TRUE(pipeline->SupportsUsage(RenderUsage::MEDIA));
  EXPECT_TRUE(pipeline->SupportsUsage(RenderUsage::INTERRUPTION));
  EXPECT_TRUE(pipeline->SupportsUsage(RenderUsage::SYSTEM_AGENT));
  EXPECT_TRUE(pipeline->SupportsUsage(RenderUsage::COMMUNICATION));
  EXPECT_FALSE(pipeline->SupportsUsage(RenderUsage::ULTRASOUND));

  return pipeline;
}

void ValidateEffect(const std::optional<fuchsia_audio_effects::ProcessorConfiguration>& config,
                    const Format& input_format, const Format& output_format) {
  ASSERT_TRUE(config);
  ASSERT_TRUE(config->processor());
  ASSERT_TRUE(config->processor()->is_valid());
  ASSERT_TRUE(config->inputs());
  ASSERT_TRUE(config->outputs());
  ASSERT_EQ(config->inputs()->size(), 1u);
  ASSERT_EQ(config->outputs()->size(), 1u);

  EXPECT_THAT((*config->inputs())[0].format(), LegacyFidlFormatEq(input_format));
  EXPECT_THAT((*config->outputs())[0].format(), LegacyFidlFormatEq(output_format));
}

void ValidateDeletedNodes(const std::vector<FakeGraphServer::CallType>& calls, size_t start_index,
                          const std::vector<NodeId> expected_deletions) {
  // Since `OutputDevicePipeline::created_nodes_` is unordered, these can arrive in any order.
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

TEST(OutputDevicePipelineTest, EmptyNoLoopback) {
  PipelineConfig::MixGroup root{
      .name = "linearize",
      .input_streams =
          {
              RenderUsage::BACKGROUND,
              RenderUsage::MEDIA,
              RenderUsage::SYSTEM_AGENT,
              RenderUsage::INTERRUPTION,
              RenderUsage::COMMUNICATION,
          },
      .loopback = false,
      .output_rate = 48000,
      .output_channels = 2,
  };

  const auto kMixerFormat = Format::CreateOrDie({fuchsia_audio::SampleType::kFloat32, 2, 48000});
  const auto kDeviceFormat = Format::CreateOrDie({fuchsia_audio::SampleType::kInt32, 2, 48000});

  TestHarness h;
  auto pipeline = CreatePipeline(h, kDeviceFormat, std::move(root));
  ASSERT_TRUE(pipeline);

  // FakeGraphServer assigns IDs in monotonically increasing order, meaning the order below is the
  // same as creation order. We hardcode these numbers below to simplify this test -- the actual
  // creation order is an unimportant side effect of the implementation.
  static constexpr NodeId kConsumerId = 2;
  static constexpr NodeId kMixerId = 1;

  // 1 nodes and 1 edge.
  EXPECT_EQ(h.server->calls().size(), 3u);

  EXPECT_EQ(pipeline->DestNodeForUsage(RenderUsage::BACKGROUND), kMixerId);
  EXPECT_EQ(pipeline->DestNodeForUsage(RenderUsage::MEDIA), kMixerId);
  EXPECT_EQ(pipeline->DestNodeForUsage(RenderUsage::SYSTEM_AGENT), kMixerId);
  EXPECT_EQ(pipeline->DestNodeForUsage(RenderUsage::INTERRUPTION), kMixerId);
  EXPECT_EQ(pipeline->DestNodeForUsage(RenderUsage::COMMUNICATION), kMixerId);

  // No loopback.
  ASSERT_FALSE(pipeline->loopback());

  // Adds 2 DeleteNode calls.
  pipeline->Destroy();
  h.loop.RunUntilIdle();

  // Check the graph calls.
  const auto& calls = h.server->calls();
  EXPECT_EQ(calls.size(), 5u);

  {
    SCOPED_TRACE("Consumer");
    auto call = std::get_if<GraphCreateConsumerRequest>(&calls[kConsumerId - 1]);
    ASSERT_TRUE(call);
    EXPECT_EQ(call->direction(), fuchsia_audio_mixer::PipelineDirection::kOutput);
    ASSERT_TRUE(call->data_sink());
    ASSERT_TRUE(call->data_sink()->ring_buffer().has_value());
    EXPECT_THAT(call->data_sink()->ring_buffer()->format(), FidlFormatEq(kDeviceFormat));
    EXPECT_EQ(call->source_sample_type(), fuchsia_audio::SampleType::kFloat32);
    EXPECT_EQ(call->thread(), kThreadId);
    ASSERT_TRUE(call->external_delay_watcher());
    EXPECT_EQ(call->external_delay_watcher()->initial_delay(), kInitialDelay.get());
  }

  {
    SCOPED_TRACE("Mixer");
    auto call = std::get_if<GraphCreateMixerRequest>(&calls[kMixerId - 1]);
    ASSERT_TRUE(call);
    EXPECT_EQ(call->direction(), fuchsia_audio_mixer::PipelineDirection::kOutput);
    EXPECT_THAT(call->dest_format(), FidlFormatEq(kMixerFormat));
    EXPECT_THAT(call->dest_reference_clock(), ValidReferenceClock(kClockDomain));
  }

  EXPECT_THAT(calls[2], CreateEdgeEq(kMixerId, kConsumerId));

  ValidateDeletedNodes(calls, 3, {kConsumerId, kMixerId});
}

TEST(OutputDevicePipelineTest, MultilevelWithEffectsAndLoopback) {
  PipelineConfig::MixGroup root{
      .name = "linearize",
      .input_streams =
          {
              RenderUsage::BACKGROUND,
          },
      .effects_v2 = PipelineConfig::EffectV2{.instance_name = "NoOp"},
      .inputs = {{
          .name = "mix",
          .input_streams =
              {
                  RenderUsage::MEDIA,
                  RenderUsage::SYSTEM_AGENT,
                  RenderUsage::INTERRUPTION,
                  RenderUsage::COMMUNICATION,
              },
          .effects_v2 = PipelineConfig::EffectV2{.instance_name = "NoOp"},
          .loopback = true,
          .output_rate = 48000,
          .output_channels = 2,
      }},
      .loopback = false,
      .output_rate = 48000,
      .output_channels = 2,
  };

  const auto kLoopbackFormat = Format::CreateOrDie({fuchsia_audio::SampleType::kFloat32, 2, 48000});
  const auto kLinearizeFormat =
      Format::CreateOrDie({fuchsia_audio::SampleType::kFloat32, 2, 48000});
  const auto kDeviceFormat = Format::CreateOrDie({fuchsia_audio::SampleType::kInt32, 2, 48000});

  TestHarness h;
  auto pipeline = CreatePipeline(h, kDeviceFormat, std::move(root));
  ASSERT_TRUE(pipeline);

  // FakeGraphServer assigns IDs in monotonically increasing order, meaning the order below is the
  // same as creation order. We hardcode these numbers below to simplify this test -- the actual
  // creation order is an unimportant side effect of the implementation.
  static constexpr NodeId kConsumerId = 6;
  static constexpr NodeId kLinearizeMixerId = 4;
  static constexpr NodeId kLinearizeCustomId = 5;
  static constexpr NodeId kMixMixerId = 1;
  static constexpr NodeId kMixCustomId = 2;
  static constexpr NodeId kMixSplitterId = 3;

  // 6 nodes and 5 edges.
  EXPECT_EQ(h.server->calls().size(), 11u);

  EXPECT_EQ(pipeline->DestNodeForUsage(RenderUsage::BACKGROUND), kLinearizeMixerId);
  EXPECT_EQ(pipeline->DestNodeForUsage(RenderUsage::MEDIA), kMixMixerId);
  EXPECT_EQ(pipeline->DestNodeForUsage(RenderUsage::SYSTEM_AGENT), kMixMixerId);
  EXPECT_EQ(pipeline->DestNodeForUsage(RenderUsage::INTERRUPTION), kMixMixerId);
  EXPECT_EQ(pipeline->DestNodeForUsage(RenderUsage::COMMUNICATION), kMixMixerId);

  auto loopback = pipeline->loopback();
  ASSERT_TRUE(loopback);

  // When passing the same format as the loopback interface, this should return immediately without
  // creating any nodes.
  {
    bool done = false;
    loopback->CreateSourceNodeForFormat(kLoopbackFormat, [&done](auto node) {
      EXPECT_EQ(node, kMixSplitterId);
      done = true;
    });
    EXPECT_TRUE(done);
    EXPECT_EQ(h.server->calls().size(), 11u);
  }

  // Adds 6 DeleteNode calls.
  pipeline->Destroy();
  h.loop.RunUntilIdle();

  // Check the graph calls.
  const auto& calls = h.server->calls();
  EXPECT_EQ(calls.size(), 17u);

  {
    SCOPED_TRACE("Consumer");
    auto call = std::get_if<GraphCreateConsumerRequest>(&calls[kConsumerId - 1]);
    ASSERT_TRUE(call);
    EXPECT_EQ(call->direction(), fuchsia_audio_mixer::PipelineDirection::kOutput);
    ASSERT_TRUE(call->data_sink());
    ASSERT_TRUE(call->data_sink()->ring_buffer().has_value());
    EXPECT_THAT(call->data_sink()->ring_buffer()->format(), FidlFormatEq(kDeviceFormat));
    EXPECT_EQ(call->source_sample_type(), fuchsia_audio::SampleType::kFloat32);
    EXPECT_EQ(call->thread(), kThreadId);
    ASSERT_TRUE(call->external_delay_watcher());
    EXPECT_EQ(call->external_delay_watcher()->initial_delay(), kInitialDelay.get());
  }

  {
    SCOPED_TRACE("Linearizer.Mixer");
    auto call = std::get_if<GraphCreateMixerRequest>(&calls[kLinearizeMixerId - 1]);
    ASSERT_TRUE(call);
    EXPECT_EQ(call->direction(), fuchsia_audio_mixer::PipelineDirection::kOutput);
    EXPECT_THAT(call->dest_format(), FidlFormatEq(kLinearizeFormat));
    EXPECT_THAT(call->dest_reference_clock(), ValidReferenceClock(kClockDomain));
  }

  {
    SCOPED_TRACE("Linearizer.Custom");
    auto call = std::get_if<GraphCreateCustomRequest>(&calls[kLinearizeCustomId - 1]);
    ASSERT_TRUE(call);
    EXPECT_EQ(call->direction(), fuchsia_audio_mixer::PipelineDirection::kOutput);
    EXPECT_THAT(call->reference_clock(), ValidReferenceClock(kClockDomain));
    ValidateEffect(call->config(), kLinearizeFormat, kLinearizeFormat);
  }

  {
    SCOPED_TRACE("Mix.Mixer");
    auto call = std::get_if<GraphCreateMixerRequest>(&calls[kMixMixerId - 1]);
    ASSERT_TRUE(call);
    EXPECT_EQ(call->direction(), fuchsia_audio_mixer::PipelineDirection::kOutput);
    EXPECT_THAT(call->dest_format(), FidlFormatEq(kLoopbackFormat));
    EXPECT_THAT(call->dest_reference_clock(), ValidReferenceClock(kClockDomain));
  }

  {
    SCOPED_TRACE("Mix.Custom");
    auto call = std::get_if<GraphCreateCustomRequest>(&calls[kMixCustomId - 1]);
    ASSERT_TRUE(call);
    EXPECT_EQ(call->direction(), fuchsia_audio_mixer::PipelineDirection::kOutput);
    EXPECT_THAT(call->reference_clock(), ValidReferenceClock(kClockDomain));
    ValidateEffect(call->config(), kLoopbackFormat, kLoopbackFormat);
  }

  {
    SCOPED_TRACE("Mix.Splitter");
    auto call = std::get_if<GraphCreateSplitterRequest>(&calls[kMixSplitterId - 1]);
    ASSERT_TRUE(call);
    EXPECT_EQ(call->direction(), fuchsia_audio_mixer::PipelineDirection::kOutput);
    EXPECT_THAT(call->format(), FidlFormatEq(kLoopbackFormat));
    EXPECT_EQ(call->thread(), kThreadId);
    EXPECT_THAT(call->reference_clock(), ValidReferenceClock(kClockDomain));
  }

  EXPECT_THAT(calls[6], CreateEdgeEq(kLinearizeMixerId, kLinearizeCustomId));
  EXPECT_THAT(calls[7], CreateEdgeEq(kLinearizeCustomId, kConsumerId));
  EXPECT_THAT(calls[8], CreateEdgeEq(kMixMixerId, kMixCustomId));
  EXPECT_THAT(calls[9], CreateEdgeEq(kMixCustomId, kMixSplitterId));
  EXPECT_THAT(calls[10], CreateEdgeEq(kMixSplitterId, kLinearizeMixerId));

  ValidateDeletedNodes(calls, 11,
                       {kConsumerId, kLinearizeMixerId, kLinearizeCustomId, kMixMixerId,
                        kMixCustomId, kMixSplitterId});
}

TEST(OutputDevicePipelineTest, UpsampleAfterLoopback) {
  PipelineConfig::MixGroup root{
      .name = "linearize",
      .input_streams =
          {
              RenderUsage::BACKGROUND,
          },
      .inputs = {{
          .name = "mix",
          .input_streams =
              {
                  RenderUsage::MEDIA,
                  RenderUsage::SYSTEM_AGENT,
                  RenderUsage::INTERRUPTION,
                  RenderUsage::COMMUNICATION,
              },
          .loopback = true,
          .output_rate = 48000,
          .output_channels = 2,
      }},
      .loopback = false,
      .output_rate = 96000,
      .output_channels = 2,
  };

  const auto kLoopbackFormat = Format::CreateOrDie({fuchsia_audio::SampleType::kFloat32, 2, 48000});
  const auto kLinearizeFormat =
      Format::CreateOrDie({fuchsia_audio::SampleType::kFloat32, 2, 96000});
  const auto kDeviceFormat = Format::CreateOrDie({fuchsia_audio::SampleType::kInt32, 2, 96000});

  TestHarness h;
  auto pipeline = CreatePipeline(h, kDeviceFormat, std::move(root));
  ASSERT_TRUE(pipeline);

  // FakeGraphServer assigns IDs in monotonically increasing order, meaning the order below is the
  // same as creation order. We hardcode these numbers below to simplify this test -- the actual
  // creation order is an unimportant side effect of the implementation.
  static constexpr NodeId kConsumerId = 4;
  static constexpr NodeId kLinearizeMixerId = 3;
  static constexpr NodeId kMixMixerId = 1;
  static constexpr NodeId kMixSplitterId = 2;

  // 4 nodes and 3 edges.
  EXPECT_EQ(h.server->calls().size(), 7u);

  EXPECT_EQ(pipeline->DestNodeForUsage(RenderUsage::BACKGROUND), kLinearizeMixerId);
  EXPECT_EQ(pipeline->DestNodeForUsage(RenderUsage::MEDIA), kMixMixerId);
  EXPECT_EQ(pipeline->DestNodeForUsage(RenderUsage::SYSTEM_AGENT), kMixMixerId);
  EXPECT_EQ(pipeline->DestNodeForUsage(RenderUsage::INTERRUPTION), kMixMixerId);
  EXPECT_EQ(pipeline->DestNodeForUsage(RenderUsage::COMMUNICATION), kMixMixerId);

  auto loopback = pipeline->loopback();
  ASSERT_TRUE(loopback);

  // When passing the same format as the loopback interface, this should return immediately without
  // creating any nodes.
  {
    bool done = false;
    loopback->CreateSourceNodeForFormat(kLoopbackFormat, [&done](auto node) {
      EXPECT_EQ(node, kMixSplitterId);
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
  EXPECT_EQ(calls.size(), 11u);

  {
    SCOPED_TRACE("Consumer");
    auto call = std::get_if<GraphCreateConsumerRequest>(&calls[kConsumerId - 1]);
    ASSERT_TRUE(call);
    EXPECT_EQ(call->direction(), fuchsia_audio_mixer::PipelineDirection::kOutput);
    ASSERT_TRUE(call->data_sink());
    ASSERT_TRUE(call->data_sink()->ring_buffer().has_value());
    EXPECT_THAT(call->data_sink()->ring_buffer()->format(), FidlFormatEq(kDeviceFormat));
    EXPECT_EQ(call->source_sample_type(), fuchsia_audio::SampleType::kFloat32);
    EXPECT_EQ(call->thread(), kThreadId);
    ASSERT_TRUE(call->external_delay_watcher());
    EXPECT_EQ(call->external_delay_watcher()->initial_delay(), kInitialDelay.get());
  }

  {
    SCOPED_TRACE("Linearizer.Mixer");
    auto call = std::get_if<GraphCreateMixerRequest>(&calls[kLinearizeMixerId - 1]);
    ASSERT_TRUE(call);
    EXPECT_EQ(call->direction(), fuchsia_audio_mixer::PipelineDirection::kOutput);
    EXPECT_THAT(call->dest_format(), FidlFormatEq(kLinearizeFormat));
    EXPECT_THAT(call->dest_reference_clock(), ValidReferenceClock(kClockDomain));
  }

  {
    SCOPED_TRACE("Mix.Mixer");
    auto call = std::get_if<GraphCreateMixerRequest>(&calls[kMixMixerId - 1]);
    ASSERT_TRUE(call);
    EXPECT_EQ(call->direction(), fuchsia_audio_mixer::PipelineDirection::kOutput);
    EXPECT_THAT(call->dest_format(), FidlFormatEq(kLoopbackFormat));
    EXPECT_THAT(call->dest_reference_clock(), ValidReferenceClock(kClockDomain));
  }

  {
    SCOPED_TRACE("Mix.Splitter");
    auto call = std::get_if<GraphCreateSplitterRequest>(&calls[kMixSplitterId - 1]);
    ASSERT_TRUE(call);
    EXPECT_EQ(call->direction(), fuchsia_audio_mixer::PipelineDirection::kOutput);
    EXPECT_THAT(call->format(), FidlFormatEq(kLoopbackFormat));
    EXPECT_EQ(call->thread(), kThreadId);
    EXPECT_THAT(call->reference_clock(), ValidReferenceClock(kClockDomain));
  }

  EXPECT_THAT(calls[4], CreateEdgeEq(kLinearizeMixerId, kConsumerId));
  EXPECT_THAT(calls[5], CreateEdgeEq(kMixMixerId, kMixSplitterId));
  EXPECT_THAT(calls[6], CreateEdgeEq(kMixSplitterId, kLinearizeMixerId));

  ValidateDeletedNodes(calls, 7, {kConsumerId, kLinearizeMixerId, kMixMixerId, kMixSplitterId});
}

TEST(OutputDevicePipelineTest, RechannelEffects) {
  PipelineConfig::MixGroup root{
      .name = "linearize",
      .input_streams =
          {
              RenderUsage::BACKGROUND,
              RenderUsage::MEDIA,
              RenderUsage::SYSTEM_AGENT,
              RenderUsage::INTERRUPTION,
              RenderUsage::COMMUNICATION,
          },
      .effects_v2 = PipelineConfig::EffectV2{.instance_name = "NoOpRechannel2To4"},
      .loopback = true,
      .output_rate = 48000,
      .output_channels = 2,
  };

  const auto kMixFormat = Format::CreateOrDie({fuchsia_audio::SampleType::kFloat32, 2, 48000});
  const auto kLoopbackFormat = Format::CreateOrDie({fuchsia_audio::SampleType::kFloat32, 4, 48000});
  const auto kDeviceFormat = Format::CreateOrDie({fuchsia_audio::SampleType::kInt32, 4, 48000});

  TestHarness h;
  auto pipeline = CreatePipeline(h, kDeviceFormat, std::move(root));
  ASSERT_TRUE(pipeline);

  // FakeGraphServer assigns IDs in monotonically increasing order, meaning the order below is the
  // same as creation order. We hardcode these numbers below to simplify this test -- the actual
  // creation order is an unimportant side effect of the implementation.
  static constexpr NodeId kConsumerId = 4;
  static constexpr NodeId kMixerId = 1;
  static constexpr NodeId kCustomId = 2;
  static constexpr NodeId kSplitterId = 3;

  // 4 nodes and 3 edges.
  EXPECT_EQ(h.server->calls().size(), 7u);

  EXPECT_EQ(pipeline->DestNodeForUsage(RenderUsage::BACKGROUND), kMixerId);
  EXPECT_EQ(pipeline->DestNodeForUsage(RenderUsage::MEDIA), kMixerId);
  EXPECT_EQ(pipeline->DestNodeForUsage(RenderUsage::SYSTEM_AGENT), kMixerId);
  EXPECT_EQ(pipeline->DestNodeForUsage(RenderUsage::INTERRUPTION), kMixerId);
  EXPECT_EQ(pipeline->DestNodeForUsage(RenderUsage::COMMUNICATION), kMixerId);

  auto loopback = pipeline->loopback();
  ASSERT_TRUE(loopback);

  // When passing the same format as the loopback interface, this should return immediately without
  // creating any nodes.
  {
    bool done = false;
    loopback->CreateSourceNodeForFormat(kLoopbackFormat, [&done](auto node) {
      EXPECT_EQ(node, kSplitterId);
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
  EXPECT_EQ(calls.size(), 11u);

  {
    SCOPED_TRACE("Consumer");
    auto call = std::get_if<GraphCreateConsumerRequest>(&calls[kConsumerId - 1]);
    ASSERT_TRUE(call);
    EXPECT_EQ(call->direction(), fuchsia_audio_mixer::PipelineDirection::kOutput);
    ASSERT_TRUE(call->data_sink());
    ASSERT_TRUE(call->data_sink()->ring_buffer().has_value());
    EXPECT_THAT(call->data_sink()->ring_buffer()->format(), FidlFormatEq(kDeviceFormat));
    EXPECT_EQ(call->source_sample_type(), fuchsia_audio::SampleType::kFloat32);
    EXPECT_EQ(call->thread(), kThreadId);
    ASSERT_TRUE(call->external_delay_watcher());
    EXPECT_EQ(call->external_delay_watcher()->initial_delay(), kInitialDelay.get());
  }

  {
    SCOPED_TRACE("Mixer");
    auto call = std::get_if<GraphCreateMixerRequest>(&calls[kMixerId - 1]);
    ASSERT_TRUE(call);
    EXPECT_EQ(call->direction(), fuchsia_audio_mixer::PipelineDirection::kOutput);
    EXPECT_THAT(call->dest_format(), FidlFormatEq(kMixFormat));
    EXPECT_THAT(call->dest_reference_clock(), ValidReferenceClock(kClockDomain));
  }

  {
    SCOPED_TRACE("Custom");
    auto call = std::get_if<GraphCreateCustomRequest>(&calls[kCustomId - 1]);
    ASSERT_TRUE(call);
    EXPECT_EQ(call->direction(), fuchsia_audio_mixer::PipelineDirection::kOutput);
    EXPECT_THAT(call->reference_clock(), ValidReferenceClock(kClockDomain));
    ValidateEffect(call->config(), kMixFormat, kLoopbackFormat);
  }

  {
    SCOPED_TRACE("Splitter");
    auto call = std::get_if<GraphCreateSplitterRequest>(&calls[kSplitterId - 1]);
    ASSERT_TRUE(call);
    EXPECT_EQ(call->direction(), fuchsia_audio_mixer::PipelineDirection::kOutput);
    EXPECT_THAT(call->format(), FidlFormatEq(kLoopbackFormat));
    EXPECT_EQ(call->thread(), kThreadId);
    EXPECT_THAT(call->reference_clock(), ValidReferenceClock(kClockDomain));
  }

  EXPECT_THAT(calls[4], CreateEdgeEq(kMixerId, kCustomId));
  EXPECT_THAT(calls[5], CreateEdgeEq(kCustomId, kSplitterId));
  EXPECT_THAT(calls[6], CreateEdgeEq(kSplitterId, kConsumerId));

  ValidateDeletedNodes(calls, 7, {kConsumerId, kMixerId, kCustomId, kSplitterId});
}

}  // namespace
}  // namespace media_audio
