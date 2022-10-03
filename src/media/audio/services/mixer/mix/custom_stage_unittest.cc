// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/mix/custom_stage.h"

#include <fuchsia/audio/effects/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async-testing/test_loop.h>
#include <lib/fidl/cpp/wire/arena.h>
#include <lib/fidl/cpp/wire/object_view.h>
#include <lib/fidl/cpp/wire/vector_view.h>
#include <lib/fzl/vmo-mapper.h>

#include <memory>
#include <string_view>
#include <utility>
#include <vector>

#include <fbl/algorithm.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "fidl/fuchsia.audio.effects/cpp/wire_types.h"
#include "src/lib/fxl/strings/string_printf.h"
#include "src/media/audio/lib/format2/fixed.h"
#include "src/media/audio/lib/format2/format.h"
#include "src/media/audio/services/common/thread_checker.h"
#include "src/media/audio/services/mixer/mix/mix_job_context.h"
#include "src/media/audio/services/mixer/mix/pipeline_detached_thread.h"
#include "src/media/audio/services/mixer/mix/ptr_decls.h"
#include "src/media/audio/services/mixer/mix/simple_packet_queue_producer_stage.h"
#include "src/media/audio/services/mixer/mix/testing/defaults.h"

namespace media_audio {

namespace {

using ::fuchsia_audio_effects::Processor;
using ::fuchsia_audio_effects::wire::ProcessorConfiguration;
using ::testing::Each;
using ::testing::FloatEq;

// Arena type used by test code. The initial size does not matter since this is a test (it's ok to
// dynamically allocate).
using Arena = fidl::Arena<512>;

// By default, the `MakeProcessorWith*` functions below create input and output buffers that are
// large enough to process at most this many frames.
constexpr auto kProcessingBufferMaxFrames = 1024;

// Helper struct to specify a `ProcessorConfiguration`.
struct ConfigOptions {
  bool in_place = false;
  fuchsia_mem::wire::Range input_buffer = {.offset = 0, .size = 0};
  fuchsia_mem::wire::Range output_buffer = {.offset = 0, .size = 0};
  fuchsia_mediastreams::wire::AudioFormat input_format = {
      .sample_format = fuchsia_mediastreams::wire::AudioSampleFormat::kFloat,
      .channel_count = 1,
      .frames_per_second = 48000,
  };
  fuchsia_mediastreams::wire::AudioFormat output_format = {
      .sample_format = fuchsia_mediastreams::wire::AudioSampleFormat::kFloat,
      .channel_count = 1,
      .frames_per_second = 48000,
  };
  uint64_t max_frames_per_call = 0;
  uint64_t block_size_frames = 1;
  uint64_t latency_frames = 0;
  uint64_t ring_out_frames = 0;
};

zx::vmo CreateVmoOrDie(uint64_t size_bytes) {
  zx::vmo vmo;
  if (const auto status = zx::vmo::create(size_bytes, 0, &vmo); status != ZX_OK) {
    FX_PLOGS(FATAL, status) << "failed to create VMO with size " << size_bytes;
  }
  return vmo;
}

zx::vmo DupVmoOrDie(const zx::vmo& vmo, zx_rights_t rights) {
  zx::vmo dup;
  if (const auto status = vmo.duplicate(rights, &dup); status != ZX_OK) {
    FX_PLOGS(FATAL, status) << "failed to duplicate VMO with rights 0x" << std::hex << rights;
  }
  return dup;
}

void CreateSeparateVmos(ConfigOptions& options, uint64_t input_size_bytes,
                        uint64_t output_size_bytes) {
  options.input_buffer.vmo = CreateVmoOrDie(input_size_bytes);
  options.input_buffer.size = input_size_bytes;
  options.output_buffer.vmo = CreateVmoOrDie(output_size_bytes);
  options.output_buffer.size = output_size_bytes;
}

void CreateSharedVmo(ConfigOptions& options,
                     uint64_t vmo_size_bytes,  // must be large enough for input & output
                     uint64_t input_offset_bytes, uint64_t input_size_bytes,
                     uint64_t output_offset_bytes, uint64_t output_size_bytes) {
  options.input_buffer.vmo = CreateVmoOrDie(vmo_size_bytes);
  options.input_buffer.offset = input_offset_bytes;
  options.input_buffer.size = input_size_bytes;
  options.output_buffer.vmo = DupVmoOrDie(options.input_buffer.vmo, ZX_RIGHT_SAME_RIGHTS);
  options.output_buffer.offset = output_offset_bytes;
  options.output_buffer.size = output_size_bytes;
  if (input_offset_bytes == output_offset_bytes) {
    options.in_place = true;
  }
}

ConfigOptions DupConfigOptions(const ConfigOptions& options) {
  return ConfigOptions{
      .in_place = options.in_place,
      .input_buffer =
          {
              .vmo = DupVmoOrDie(options.input_buffer.vmo, ZX_RIGHT_SAME_RIGHTS),
              .offset = options.input_buffer.offset,
              .size = options.input_buffer.size,
          },
      .output_buffer =
          {
              .vmo = DupVmoOrDie(options.output_buffer.vmo, ZX_RIGHT_SAME_RIGHTS),
              .offset = options.output_buffer.offset,
              .size = options.output_buffer.size,
          },
      .input_format =
          {
              .sample_format = options.input_format.sample_format,
              .channel_count = options.input_format.channel_count,
              .frames_per_second = options.input_format.frames_per_second,
          },
      .output_format =
          {
              .sample_format = options.output_format.sample_format,
              .channel_count = options.output_format.channel_count,
              .frames_per_second = options.output_format.frames_per_second,
          },
      .max_frames_per_call = options.max_frames_per_call,
      .block_size_frames = options.block_size_frames,
      .latency_frames = options.latency_frames,
      .ring_out_frames = options.ring_out_frames,
  };
}

ProcessorConfiguration MakeProcessorConfig(Arena& arena, ConfigOptions options,
                                           fidl::ClientEnd<Processor> client) {
  auto builder = ProcessorConfiguration::Builder(arena);

  builder.max_frames_per_call(options.max_frames_per_call
                                  ? options.max_frames_per_call
                                  : options.input_buffer.size /
                                        (options.input_format.channel_count * sizeof(float)));
  builder.block_size_frames(options.block_size_frames);

  if (auto& buffer = options.input_buffer; buffer.vmo.is_valid()) {
    buffer.vmo = DupVmoOrDie(buffer.vmo, ZX_RIGHT_MAP | ZX_RIGHT_READ | ZX_RIGHT_WRITE);
  }
  if (auto& buffer = options.output_buffer; buffer.vmo.is_valid()) {
    buffer.vmo = DupVmoOrDie(buffer.vmo, ZX_RIGHT_MAP | ZX_RIGHT_READ | ZX_RIGHT_WRITE);
  }

  fidl::VectorView<fuchsia_audio_effects::wire::InputConfiguration> inputs(arena, 1);
  inputs.at(0) = fuchsia_audio_effects::wire::InputConfiguration::Builder(arena)
                     .buffer(std::move(options.input_buffer))
                     .format(options.input_format)
                     .Build();
  builder.inputs(fidl::ObjectView{arena, inputs});

  fidl::VectorView<fuchsia_audio_effects::wire::OutputConfiguration> outputs(arena, 1);
  outputs.at(0) = fuchsia_audio_effects::wire::OutputConfiguration::Builder(arena)
                      .buffer(std::move(options.output_buffer))
                      .format(options.output_format)
                      .latency_frames(options.latency_frames)
                      .ring_out_frames(options.ring_out_frames)
                      .Build();
  builder.outputs(fidl::ObjectView{arena, outputs});

  builder.processor(std::move(client));

  return builder.Build();
}

fuchsia_mediastreams::wire::AudioFormat DefaultFormatWithChannels(uint32_t channels) {
  return {
      .sample_format = fuchsia_mediastreams::wire::AudioSampleFormat::kFloat,
      .channel_count = channels,
      .frames_per_second = 48000,
  };
}

// TODO(fxbug.dev/87651): Remove `ConfigOptions` above and directly pass in `CustomStage::Args`
// instead of processor `config`.
PipelineStagePtr MakeCustomStage(ProcessorConfiguration config, PipelineStagePtr source_stage) {
  PipelineStagePtr custom_stage = std::make_shared<CustomStage>(CustomStage::Args{
      .name = "CustomStage",
      .reference_clock = DefaultUnreadableClock(),
      .source_format = Format::CreateLegacyOrDie(config.inputs()[0].format()),
      .source_buffer = std::move(config.inputs()[0].buffer()),
      .dest_format = Format::CreateLegacyOrDie(config.outputs()[0].format()),
      .dest_buffer = std::move(config.outputs()[0].buffer()),
      .block_size_frames = static_cast<int64_t>(config.block_size_frames()),
      .latency_frames = static_cast<int64_t>(config.outputs()[0].latency_frames()),
      .max_frames_per_call = static_cast<int64_t>(config.max_frames_per_call()),
      .ring_out_frames = static_cast<int64_t>(config.outputs()[0].ring_out_frames()),
      .processor = fidl::WireSyncClient(std::move(config.processor())),
  });
  custom_stage->set_thread(std::make_shared<PipelineDetachedThread>());
  ScopedThreadChecker checker(custom_stage->thread()->checker());
  custom_stage->AddSource(std::move(source_stage), /*options=*/{});
  custom_stage->UpdatePresentationTimeToFracFrame(
      DefaultPresentationTimeToFracFrame(custom_stage->format()));
  return custom_stage;
}

std::vector<float> ToVector(void* payload, size_t sample_start_idx, size_t sample_end_idx) {
  float* p = static_cast<float*>(payload);
  return std::vector<float>(p + sample_start_idx, p + sample_end_idx);
}

}  // namespace

class CustomStageTestProcessor : public fidl::WireServer<Processor> {
 public:
  CustomStageTestProcessor(const ConfigOptions& options, fidl::ServerEnd<Processor> server_end,
                           async_dispatcher_t* dispatcher)
      : binding_(fidl::BindServer(dispatcher, std::move(server_end), this,
                                  [](CustomStageTestProcessor* impl, fidl::UnbindInfo info,
                                     fidl::ServerEnd<Processor> server_end) {
                                    if (!info.is_user_initiated() && !info.is_peer_closed()) {
                                      FX_PLOGS(WARNING, info.status())
                                          << "Client disconnected unexpectedly: " << info;
                                    }
                                  })),
        buffers_(options.input_buffer, options.output_buffer) {}

  float* input_data() const { return static_cast<float*>(buffers_.input); }
  float* output_data() const { return static_cast<float*>(buffers_.output); }

 private:
  fidl::ServerBindingRef<Processor> binding_;
  CustomStage::FidlBuffers buffers_;
};

class CustomStageTest : public testing::Test {
 public:
  template <class ProcessorType>
  struct ProcessorInfo {
    ProcessorType processor;
    bool in_place;
    ProcessorConfiguration config;
  };

  CustomStageTest() { fidl_loop_.StartThread("fidl"); }

  template <class ProcessorType>
  ProcessorInfo<ProcessorType> MakeProcessor(ConfigOptions options) {
    if (options.max_frames_per_call) {
      FX_CHECK(options.max_frames_per_call < kProcessingBufferMaxFrames);
    }
    if (options.block_size_frames) {
      FX_CHECK(options.block_size_frames < kProcessingBufferMaxFrames);
    }

    auto endpoints = fidl::CreateEndpoints<Processor>();
    if (!endpoints.is_ok()) {
      FX_PLOGS(FATAL, endpoints.status_value()) << "failed to construct a zx::channel";
    }

    auto config =
        MakeProcessorConfig(arena_, DupConfigOptions(options), std::move(endpoints->client));
    return {
        .processor = ProcessorType(options, std::move(endpoints->server), fidl_loop_.dispatcher()),
        .in_place = options.in_place,
        .config = config,
    };
  }

  // Processor uses different VMOs for the input and output.
  template <class ProcessorType>
  ProcessorInfo<ProcessorType> MakeProcessorWithDifferentVmos(ConfigOptions options) {
    const auto kInputChannels = options.input_format.channel_count;
    const auto kOutputChannels = options.output_format.channel_count;

    const auto kInputBufferBytes = kProcessingBufferMaxFrames * kInputChannels * sizeof(float);
    const auto kOutputBufferBytes = kProcessingBufferMaxFrames * kOutputChannels * sizeof(float);
    CreateSeparateVmos(options, kInputBufferBytes, kOutputBufferBytes);

    return MakeProcessor<ProcessorType>(std::move(options));
  }

  // Processor uses the same `fuchsia.mem.Range` for the input and output with an in-place update.
  template <class ProcessorType>
  ProcessorInfo<ProcessorType> MakeProcessorWithSameRange(ConfigOptions options) {
    FX_CHECK(options.input_format.channel_count == options.output_format.channel_count)
        << "In-place updates requires matched input and output channels";

    const auto kVmoSamples = kProcessingBufferMaxFrames * options.input_format.channel_count;
    const auto kVmoBytes = kVmoSamples * sizeof(float);

    CreateSharedVmo(options, kVmoBytes,  // VMO size
                    0, kVmoBytes,        // input buffer offset & size
                    0, kVmoBytes);       // output buffer offset & size

    return MakeProcessor<ProcessorType>(std::move(options));
  }

  // Processor uses non-overlapping ranges of the same VMO for the input and output.
  template <class ProcessorType>
  ProcessorInfo<ProcessorType> MakeProcessorWithSameVmoDifferentRanges(ConfigOptions options) {
    const auto kInputChannels = options.input_format.channel_count;
    const auto kOutputChannels = options.output_format.channel_count;

    // To map input and output separately, the offset must be page-aligned.
    const auto page_size = zx_system_get_page_size();
    const auto kInputBufferBytes = kProcessingBufferMaxFrames * kInputChannels * sizeof(float);
    const auto kOutputBufferBytes = kProcessingBufferMaxFrames * kOutputChannels * sizeof(float);
    auto input_bytes = fbl::round_up(kInputBufferBytes, page_size);
    auto output_bytes = fbl::round_up(kOutputBufferBytes, page_size);

    CreateSharedVmo(options, input_bytes + output_bytes,  // VMO size
                    0, kInputBufferBytes,                 // input buffer offset & size
                    input_bytes, kOutputBufferBytes);     // output buffer offset & size

    return MakeProcessor<ProcessorType>(std::move(options));
  }

  // A simple test case where the source is a packet queue with a single packet of the given size.
  // The test makes two `Read` calls:
  //
  //   1. Read(0, packet_frames), which should return a buffer of size `read_buffer_frames`
  //      containing data processed by the AddOne effect.
  //
  //   2. Read(packet_frames, packet_frames), which should return `std::nullopt`.
  template <class T>
  void TestAddOneWithSinglePacket(ProcessorInfo<T> info, int64_t packet_frames = 480,
                                  int64_t read_buffer_frames = 480) {
    const auto input_channels = info.config.inputs()[0].format().channel_count;
    const auto output_channels = info.config.outputs()[0].format().channel_count;
    const Format input_format = Format::CreateLegacyOrDie(info.config.inputs()[0].format());
    const Format output_format = Format::CreateLegacyOrDie(info.config.outputs()[0].format());

    auto producer_stage = MakeDefaultPacketQueue(input_format);
    auto custom_stage = MakeCustomStage(std::move(info.config), producer_stage);

    // Push one packet of the requested size.
    std::vector<float> packet_payload(packet_frames * input_channels, 1.0f);
    producer_stage->push(
        PacketView({input_format, Fixed(0), packet_frames, packet_payload.data()}));

    {
      // Read the first packet. Since our effect adds 1.0 to each sample, and we populated the
      // packet with 1.0 samples, we expect to see only 2.0 samples in the result.
      const auto packet = custom_stage->Read(DefaultCtx(), Fixed(0), packet_frames);
      ASSERT_TRUE(packet);
      EXPECT_EQ(packet->format(), output_format);
      EXPECT_EQ(packet->start().Floor(), 0);
      EXPECT_EQ(packet->start().Fraction().raw_value(), 0);
      EXPECT_EQ(packet->length(), read_buffer_frames);

      const auto vec = ToVector(packet->payload(), 0, read_buffer_frames * output_channels);
      EXPECT_THAT(vec, Each(FloatEq(2.0f)));

      // If the process is in-place, input should be overwritten. Otherwise it should be unchanged.
      if (info.in_place) {
        const auto vec =
            ToVector(info.processor.input_data(), 0, read_buffer_frames * input_channels);
        EXPECT_THAT(vec, Each(FloatEq(2.0f)));
      } else {
        const auto vec =
            ToVector(info.processor.input_data(), 0, read_buffer_frames * input_channels);
        EXPECT_THAT(vec, Each(FloatEq(1.0f)));
      }
    }

    {
      // Read the next packet. This should be null, because there are no more packets.
      const auto packet = custom_stage->Read(DefaultCtx(), Fixed(packet_frames), packet_frames);
      ASSERT_FALSE(packet);
    }
  }

 private:
  Arena arena_;
  async::Loop fidl_loop_{&kAsyncLoopConfigNoAttachToCurrentThread};
};

// Processor that adds 1.0 to each sample.
class AddOneProcessor : public CustomStageTestProcessor {
 public:
  AddOneProcessor(const ConfigOptions& options, fidl::ServerEnd<Processor> server_end,
                  async_dispatcher_t* dispatcher)
      : CustomStageTestProcessor(options, std::move(server_end), dispatcher),
        num_channels_(options.input_format.channel_count) {
    FX_CHECK(options.input_format.channel_count == options.output_format.channel_count);
  }

  void Process(ProcessRequestView request, ProcessCompleter::Sync& completer) override {
    float* input = input_data();
    float* output = output_data();
    for (uint64_t i = 0; i < num_channels_ * request->num_frames; ++i) {
      output[i] = input[i] + 1;
    }
    completer.ReplySuccess(fidl::VectorView<fuchsia_audio_effects::wire::ProcessMetrics>());
  }

 private:
  const uint32_t num_channels_;
};

TEST_F(CustomStageTest, AddOneWithOneChanDifferentVmos) {
  auto processor_info = MakeProcessorWithDifferentVmos<AddOneProcessor>({
      .input_format = DefaultFormatWithChannels(1),
      .output_format = DefaultFormatWithChannels(1),
  });
  TestAddOneWithSinglePacket(std::move(processor_info));
}

TEST_F(CustomStageTest, AddOneWithTwoChanDifferentVmos) {
  auto processor_info = MakeProcessorWithDifferentVmos<AddOneProcessor>({
      .input_format = DefaultFormatWithChannels(2),
      .output_format = DefaultFormatWithChannels(2),
  });
  TestAddOneWithSinglePacket(std::move(processor_info));
}

TEST_F(CustomStageTest, AddOneWithOneChanSameRange) {
  auto processor_info = MakeProcessorWithSameRange<AddOneProcessor>({
      .input_format = DefaultFormatWithChannels(1),
      .output_format = DefaultFormatWithChannels(1),
  });
  TestAddOneWithSinglePacket(std::move(processor_info));
}

TEST_F(CustomStageTest, AddOneWithOneChanSameVmoDifferentRanges) {
  auto processor_info = MakeProcessorWithSameVmoDifferentRanges<AddOneProcessor>({
      .input_format = DefaultFormatWithChannels(1),
      .output_format = DefaultFormatWithChannels(1),
  });
  TestAddOneWithSinglePacket(std::move(processor_info));
}

TEST_F(CustomStageTest, AddOneWithSourceOffset) {
  constexpr auto kPacketFrames = 480;

  std::vector<Fixed> source_offsets = {
      Fixed(kPacketFrames / 2),
      Fixed(kPacketFrames / 2) + ffl::FromRatio(1, 2),
  };
  for (auto source_offset : source_offsets) {
    std::ostringstream os;
    os << "source_offset=" << ffl::String::DecRational << source_offset;
    SCOPED_TRACE(os.str());

    auto info = MakeProcessorWithSameRange<AddOneProcessor>({
        .input_format = DefaultFormatWithChannels(1),
        .output_format = DefaultFormatWithChannels(1),
    });

    const Format source_format = Format::CreateLegacyOrDie(info.config.inputs()[0].format());
    auto producer_stage = MakeDefaultPacketQueue(source_format);
    auto custom_stage = MakeCustomStage(info.config, producer_stage);

    // Push one packet with `source_offset`.
    std::vector<float> packet_payload(kPacketFrames, 1.0f);
    producer_stage->push(
        PacketView({source_format, source_offset, kPacketFrames, packet_payload.data()}));

    // Source frame 100.5 is sampled at dest frame 101.
    const int64_t dest_offset_frames = source_offset.Ceiling();

    {
      // Read the first packet. Since the first source packet is offset by `source_offset`, we
      // should read silence from the source followed by 1.0s. The effect adds one to these values,
      // so we should see 1.0s followed by 2.0s.
      const auto packet = custom_stage->Read(DefaultCtx(), Fixed(0), kPacketFrames);
      ASSERT_TRUE(packet);
      EXPECT_EQ(packet->start().Floor(), 0);
      EXPECT_EQ(packet->start().Fraction().raw_value(), 0);
      EXPECT_EQ(packet->length(), kPacketFrames);

      auto vec1 = ToVector(packet->payload(), 0, dest_offset_frames);
      auto vec2 = ToVector(packet->payload(), dest_offset_frames, kPacketFrames);
      EXPECT_THAT(vec1, Each(FloatEq(1.0f)));
      EXPECT_THAT(vec2, Each(FloatEq(2.0f)));
    }

    {
      // Read the second packet. This should contain the remainder of the 2.0s, followed by 1.0s.
      const auto packet = custom_stage->Read(DefaultCtx(), Fixed(kPacketFrames), kPacketFrames);
      ASSERT_TRUE(packet);
      EXPECT_EQ(packet->start().Floor(), kPacketFrames);
      EXPECT_EQ(packet->start().Fraction().raw_value(), 0);
      EXPECT_EQ(packet->length(), kPacketFrames);

      auto vec1 = ToVector(packet->payload(), 0, dest_offset_frames);
      auto vec2 = ToVector(packet->payload(), dest_offset_frames, kPacketFrames);
      EXPECT_THAT(vec1, Each(FloatEq(2.0f)));
      EXPECT_THAT(vec2, Each(FloatEq(1.0f)));
    }

    {
      // Read the next packet. This should be null, because there are no more packets.
      const auto packet = custom_stage->Read(DefaultCtx(), Fixed(2 * kPacketFrames), kPacketFrames);
      ASSERT_FALSE(packet);
    }
  }
}

TEST_F(CustomStageTest, AddOneWithReadSmallerThanProcessingBuffer) {
  auto info = MakeProcessorWithSameRange<AddOneProcessor>({
      .input_format = DefaultFormatWithChannels(1),
      .output_format = DefaultFormatWithChannels(1),
      .max_frames_per_call = 720,
      .block_size_frames = 720,
  });

  // Push one 480 frames packet.
  const Format source_format = Format::CreateLegacyOrDie(info.config.inputs()[0].format());
  auto producer_stage = MakeDefaultPacketQueue(source_format);
  auto custom_stage = MakeCustomStage(info.config, producer_stage);

  std::vector<float> packet_payload(480, 1.0f);
  producer_stage->push(PacketView({source_format, Fixed(0), 480, packet_payload.data()}));

  {
    // Read the first packet.
    const auto packet = custom_stage->Read(DefaultCtx(), Fixed(0), 480);
    ASSERT_TRUE(packet);
    EXPECT_EQ(packet->start().Floor(), 0);
    EXPECT_EQ(packet->start().Fraction().raw_value(), 0);
    EXPECT_EQ(packet->length(), 480);

    // Our effect adds 1.0, and the source packet is 1.0, so the payload should contain all 2.0.
    const auto vec = ToVector(packet->payload(), 0, 480);
    EXPECT_THAT(vec, Each(FloatEq(2.0f)));
  }

  {
    // The source stream does not have a second packet, however when we processed the first packet,
    // we processed 720 frames total (480 from the first packet + 240 of silence). This `Read`
    // should return those 240 frames.
    const auto packet = custom_stage->Read(DefaultCtx(), Fixed(480), 480);
    ASSERT_TRUE(packet);
    EXPECT_EQ(packet->start().Floor(), 480);
    EXPECT_EQ(packet->start().Fraction().raw_value(), 0);
    EXPECT_EQ(packet->length(), 240);

    // Since the source stream was silent, and our effect adds 1.0, the payload is 1.0.
    const auto vec = ToVector(packet->payload(), 0, 240);
    EXPECT_THAT(vec, Each(FloatEq(1.0f)));
  }

  {
    // Read again where we left off. This should be null, because our cache is exhausted and the
    // source has no more data.
    const auto packet = custom_stage->Read(DefaultCtx(), Fixed(720), 480);
    ASSERT_FALSE(packet);
  }
}

TEST_F(CustomStageTest, AddOneWithReadSmallerThanProcessingBufferAndSourceOffset) {
  auto info = MakeProcessorWithSameRange<AddOneProcessor>({
      .input_format = DefaultFormatWithChannels(1),
      .output_format = DefaultFormatWithChannels(1),
      .max_frames_per_call = 720,
      .block_size_frames = 720,
  });

  // Push one 480 frames packet starting at frame 720.
  const Format source_format = Format::CreateLegacyOrDie(info.config.inputs()[0].format());
  auto producer_stage = MakeDefaultPacketQueue(source_format);
  auto custom_stage = MakeCustomStage(info.config, producer_stage);

  std::vector<float> packet_payload(480, 1.0f);
  producer_stage->push(PacketView({source_format, Fixed(720), 480, packet_payload.data()}));

  {
    // This `Read` will attempt read 720 frames from the source, but the source is empty.
    const auto packet = custom_stage->Read(DefaultCtx(), Fixed(0), 480);
    ASSERT_FALSE(packet);
  }

  {
    // This `Read` should not read anything from the source because we know from the prior `Read`
    // that the source is empty until 720.
    const auto packet = custom_stage->Read(DefaultCtx(), Fixed(480), 240);
    ASSERT_FALSE(packet);
  }

  {
    // Now we have data.
    const auto packet = custom_stage->Read(DefaultCtx(), Fixed(720), 480);
    ASSERT_TRUE(packet);
    EXPECT_EQ(packet->start().Floor(), 720);
    EXPECT_EQ(packet->start().Fraction().raw_value(), 0);
    EXPECT_EQ(packet->length(), 480);

    // Our effect adds 1.0, and the source packet is 1.0, so the payload should contain all 2.0.
    const auto vec = ToVector(packet->payload(), 0, 480);
    EXPECT_THAT(vec, Each(FloatEq(2.0f)));
  }

  {
    // The source stream ends at frame 720+480=1200, however the last `Read` processed 240
    // additional frames from the source. This `Read` should return those 240 frames.
    const auto packet = custom_stage->Read(DefaultCtx(), Fixed(1200), 480);
    ASSERT_TRUE(packet);
    EXPECT_EQ(packet->start().Floor(), 1200);
    EXPECT_EQ(packet->start().Fraction().raw_value(), 0);
    EXPECT_EQ(packet->length(), 240);

    // Our effect adds 1.0, and the source range is silent, so the payload should contain all 1.0s.
    const auto vec = ToVector(packet->payload(), 0, 240);
    EXPECT_THAT(vec, Each(FloatEq(1.0f)));
  }

  {
    // Read again where we left off. This should be null, because our cache is exhausted and the
    // source has no more data.
    const auto packet = custom_stage->Read(DefaultCtx(), Fixed(1440), 480);
    ASSERT_FALSE(packet);
  }
}

TEST_F(CustomStageTest, AddOneMaxSizeWithoutBlockSize) {
  // First `Read` returns 31 frames.
  auto processor_info = MakeProcessorWithDifferentVmos<AddOneProcessor>({
      .input_format = DefaultFormatWithChannels(1),
      .output_format = DefaultFormatWithChannels(1),
      .max_frames_per_call = 31,
  });
  TestAddOneWithSinglePacket(std::move(processor_info), /*packet_frames=*/480,
                             /*read_buffer_frames=*/31);
}

TEST_F(CustomStageTest, AddOneWithBlockSizeEqualsMaxSize) {
  // First `Read` returns 8 frames.
  auto processor_info = MakeProcessorWithDifferentVmos<AddOneProcessor>({
      .input_format = DefaultFormatWithChannels(1),
      .output_format = DefaultFormatWithChannels(1),
      .max_frames_per_call = 8,
      .block_size_frames = 8,
  });
  TestAddOneWithSinglePacket(std::move(processor_info), /*packet_frames=*/480,
                             /*read_buffer_frames=*/8);
}

TEST_F(CustomStageTest, AddOneWithBlockSizeLessThanMaxSize) {
  // First `Read` returns 32 frames.
  auto processor_info = MakeProcessorWithDifferentVmos<AddOneProcessor>({
      .input_format = DefaultFormatWithChannels(1),
      .output_format = DefaultFormatWithChannels(1),
      .max_frames_per_call = 32,
      .block_size_frames = 8,
  });
  TestAddOneWithSinglePacket(std::move(processor_info), /*packet_frames=*/480,
                             /*read_buffer_frames=*/32);
}

// Test processor that adds 1.0 to each input sample with rechannelization from 1 to 2 channels,
// where the first sample of each output frame is duplicated to produce the second sample.
class AddOneAndDupChannelProcessor : public CustomStageTestProcessor {
 public:
  AddOneAndDupChannelProcessor(const ConfigOptions& options, fidl::ServerEnd<Processor> server_end,
                               async_dispatcher_t* dispatcher)
      : CustomStageTestProcessor(options, std::move(server_end), dispatcher) {
    FX_CHECK(options.input_format.channel_count == 1);
    FX_CHECK(options.output_format.channel_count == 2);
  }

  void Process(ProcessRequestView request, ProcessCompleter::Sync& completer) override {
    float* input = input_data();
    float* output = output_data();
    for (uint64_t i = 0; i < request->num_frames; ++i) {
      output[2 * i] = input[i] + 1;
      output[2 * i + 1] = input[i] + 1;
    }
    completer.ReplySuccess(fidl::VectorView<fuchsia_audio_effects::wire::ProcessMetrics>());
  }
};

TEST_F(CustomStageTest, AddOneAndDupChannelWithDifferentVmos) {
  auto processor_info = MakeProcessorWithDifferentVmos<AddOneAndDupChannelProcessor>({
      .input_format = DefaultFormatWithChannels(1),
      .output_format = DefaultFormatWithChannels(2),
  });
  TestAddOneWithSinglePacket(std::move(processor_info));
}

TEST_F(CustomStageTest, AddOneAndDupChannelWithSameVmoDifferentRanges) {
  auto processor_info = MakeProcessorWithSameVmoDifferentRanges<AddOneAndDupChannelProcessor>({
      .input_format = DefaultFormatWithChannels(1),
      .output_format = DefaultFormatWithChannels(2),
  });
  TestAddOneWithSinglePacket(std::move(processor_info));
}

// Test processor that adds 1.0 to each input sample with rechannelization from 2 to 1 channels,
// where the second sample of each input frame is simply dropped and unused.
class AddOneAndRemoveChannelProcessor : public CustomStageTestProcessor {
 public:
  AddOneAndRemoveChannelProcessor(const ConfigOptions& options,
                                  fidl::ServerEnd<Processor> server_end,
                                  async_dispatcher_t* dispatcher)
      : CustomStageTestProcessor(options, std::move(server_end), dispatcher) {
    FX_CHECK(options.input_format.channel_count == 2);
    FX_CHECK(options.output_format.channel_count == 1);
  }

  void Process(ProcessRequestView request, ProcessCompleter::Sync& completer) override {
    float* input = input_data();
    float* output = output_data();
    for (uint64_t i = 0; i < request->num_frames; ++i) {
      output[i] = input[2 * i] + 1;
    }
    completer.ReplySuccess(fidl::VectorView<fuchsia_audio_effects::wire::ProcessMetrics>());
  }
};

TEST_F(CustomStageTest, AddOneAndRemoveChannelWithDifferentVmos) {
  auto processor_info = MakeProcessorWithDifferentVmos<AddOneAndRemoveChannelProcessor>({
      .input_format = DefaultFormatWithChannels(2),
      .output_format = DefaultFormatWithChannels(1),
  });
  TestAddOneWithSinglePacket(std::move(processor_info));
}

TEST_F(CustomStageTest, AddOneAndRemoveChannelWithSameVmoDifferentRanges) {
  auto processor_info = MakeProcessorWithSameVmoDifferentRanges<AddOneAndRemoveChannelProcessor>({
      .input_format = DefaultFormatWithChannels(2),
      .output_format = DefaultFormatWithChannels(1),
  });
  TestAddOneWithSinglePacket(std::move(processor_info));
}

// Processor that adds 1.0 to each sample with latency, and ring out with a constant value of -2.0.
template <size_t LatencyFrames>
class AddOneWithLatencyProcessor : public CustomStageTestProcessor {
 public:
  AddOneWithLatencyProcessor(const ConfigOptions& options, fidl::ServerEnd<Processor> server_end,
                             async_dispatcher_t* dispatcher)
      : CustomStageTestProcessor(options, std::move(server_end), dispatcher),
        ring_out_frame_count_(static_cast<int64_t>(options.ring_out_frames)) {
    FX_CHECK(options.input_format.channel_count == 1);
    FX_CHECK(options.output_format.channel_count == 1);
    FX_CHECK(options.latency_frames == LatencyFrames);
    delayed_frames_.fill(0.0f);
  }

  void Process(ProcessRequestView request, ProcessCompleter::Sync& completer) override {
    float* input = input_data();
    float* output = output_data();
    for (uint64_t i = 0; i < request->num_frames; ++i) {
      output[i] = delayed_frames_[delayed_frame_index_] + 1.0f;

      if (input[i] > 0.0f) {
        delayed_frames_[delayed_frame_index_] = input[i];
        ring_out_index_ = 0;
      } else if (ring_out_index_ >= 0 && ring_out_index_ < ring_out_frame_count_) {
        delayed_frames_[delayed_frame_index_] = -2.0f;
        ++ring_out_index_;
      } else {
        delayed_frames_[delayed_frame_index_] = 0.0f;
      }

      delayed_frame_index_ = (delayed_frame_index_ + 1) % LatencyFrames;
    }
    completer.ReplySuccess(fidl::VectorView<fuchsia_audio_effects::wire::ProcessMetrics>());
  }

 private:
  const int64_t ring_out_frame_count_;
  int ring_out_index_ = -1;

  static inline std::array<float, LatencyFrames> delayed_frames_{0.0f};
  int delayed_frame_index_ = 0;
};

TEST_F(CustomStageTest, AddOneWithLatencyWithDifferentVmos) {
  auto processor_info = MakeProcessorWithDifferentVmos<AddOneWithLatencyProcessor<3>>({
      .input_format = DefaultFormatWithChannels(1),
      .output_format = DefaultFormatWithChannels(1),
      .latency_frames = 3,
  });
  TestAddOneWithSinglePacket(std::move(processor_info));
}

TEST_F(CustomStageTest, AddOneWithLatencyWithSameVmoDifferentRanges) {
  auto processor_info = MakeProcessorWithSameVmoDifferentRanges<AddOneWithLatencyProcessor<5>>({
      .input_format = DefaultFormatWithChannels(1),
      .output_format = DefaultFormatWithChannels(1),
      .latency_frames = 5,
  });
  TestAddOneWithSinglePacket(std::move(processor_info));
}

TEST_F(CustomStageTest, AddOneWithLatencyLessThanBlockSize) {
  // First `Read` processes exactly 32 frames, so no additional block is left to be read in the
  // second `Read` call.
  auto processor_info = MakeProcessorWithDifferentVmos<AddOneWithLatencyProcessor<6>>({
      .input_format = DefaultFormatWithChannels(1),
      .output_format = DefaultFormatWithChannels(1),
      .block_size_frames = 16,
      .latency_frames = 6,
  });
  TestAddOneWithSinglePacket(std::move(processor_info), /*packet_frames=*/26,
                             /*read_buffer_frames=*/26);
}

TEST_F(CustomStageTest, AddOneWithLatencyLessThanBlockSizeWithMaxFramesPerCall) {
  // First `Read` returns the first 6 frames, then test jumps to read frame 100 which has no data.
  auto processor_info = MakeProcessorWithDifferentVmos<AddOneWithLatencyProcessor<4>>({
      .input_format = DefaultFormatWithChannels(1),
      .output_format = DefaultFormatWithChannels(1),
      .max_frames_per_call = 10,
      .block_size_frames = 10,
      .latency_frames = 4,
  });
  TestAddOneWithSinglePacket(std::move(processor_info), /*packet_frames=*/100,
                             /*read_buffer_frames=*/6);
}

TEST_F(CustomStageTest, AddOneWithLatencyMoreThanMaxFramesPerCall) {
  auto processor_info = MakeProcessorWithDifferentVmos<AddOneWithLatencyProcessor<102>>({
      .input_format = DefaultFormatWithChannels(1),
      .output_format = DefaultFormatWithChannels(1),
      .max_frames_per_call = 10,
      .block_size_frames = 10,
      .latency_frames = 102,
  });
  const Format source_format =
      Format::CreateLegacyOrDie(processor_info.config.inputs()[0].format());
  auto producer_stage = MakeDefaultPacketQueue(source_format);
  auto custom_stage = MakeCustomStage(std::move(processor_info.config), producer_stage);

  // Push the packet.
  std::vector<float> packet_payload(15, 1.0f);
  producer_stage->push(PacketView({source_format, Fixed(0), 10, packet_payload.data()}));

  {
    // Attempt to read the first 10 frames. This will process all frames up to frame 110, to
    // compensate for latency, 10 at a time, which should return the first 8 frames of the packet.
    const auto packet = custom_stage->Read(DefaultCtx(), Fixed(0), 10);
    ASSERT_TRUE(packet);
    EXPECT_EQ(packet->start().Floor(), 0);
    EXPECT_EQ(packet->start().Fraction().raw_value(), 0);
    EXPECT_EQ(packet->length(), 8);
    EXPECT_THAT(ToVector(packet->payload(), 0, 8), Each(2.0f));
  }

  {
    // Read the remaining 2 frames.
    const auto packet = custom_stage->Read(DefaultCtx(), Fixed(8), 2);
    ASSERT_TRUE(packet);
    EXPECT_EQ(packet->start().Floor(), 8);
    EXPECT_EQ(packet->start().Fraction().raw_value(), 0);
    EXPECT_EQ(packet->length(), 2);
    EXPECT_THAT(ToVector(packet->payload(), 0, 2), Each(2.0f));
  }
}

TEST_F(CustomStageTest, AddOneWithLatencyReadOnePacketWithOffset) {
  auto processor_info = MakeProcessorWithDifferentVmos<AddOneWithLatencyProcessor<2>>({
      .input_format = DefaultFormatWithChannels(1),
      .output_format = DefaultFormatWithChannels(1),
      .max_frames_per_call = 150,
      .block_size_frames = 15,
      .latency_frames = 2,
  });
  const Format source_format =
      Format::CreateLegacyOrDie(processor_info.config.inputs()[0].format());
  auto producer_stage = MakeDefaultPacketQueue(source_format);
  auto custom_stage = MakeCustomStage(std::move(processor_info.config), producer_stage);

  // Push the packet.
  std::vector<float> packet_payload(15);
  for (int i = 0; i < 15; ++i) {
    packet_payload[i] = static_cast<float>(i);
  }
  producer_stage->push(PacketView({source_format, Fixed(16), 15, packet_payload.data()}));

  {
    // Read the first 10 frames, this will process the first 15 frames, which should not return
    // anything as the packet starts at frame 16.
    const auto packet = custom_stage->Read(DefaultCtx(), Fixed(0), 10);
    ASSERT_FALSE(packet);
  }

  {
    // Read the next 10 frames, this should process the next 15 frames up to frame 30, and return
    // one frame of silence starting at frame 15, followed by the first 4 frames of the packet
    // starting at frame 16.
    const auto packet = custom_stage->Read(DefaultCtx(), Fixed(10), 10);
    ASSERT_TRUE(packet);
    EXPECT_EQ(packet->start().Floor(), 15);
    EXPECT_EQ(packet->start().Fraction().raw_value(), 0);
    EXPECT_EQ(packet->length(), 5);

    const auto vec = ToVector(packet->payload(), 0, 5);
    EXPECT_FLOAT_EQ(vec[0], 1.0f);
    for (int i = 1; i < 5; ++i) {
      EXPECT_FLOAT_EQ(vec[i], static_cast<float>(i)) << i;
    }
  }

  {
    // Attempt to read another 10 frames, this should return the cached 8 frames of the packet
    // starting at frame 20.
    const auto packet = custom_stage->Read(DefaultCtx(), Fixed(20), 10);
    ASSERT_TRUE(packet);
    EXPECT_EQ(packet->start().Floor(), 20);
    EXPECT_EQ(packet->start().Fraction().raw_value(), 0);
    EXPECT_EQ(packet->length(), 8);

    const auto vec = ToVector(packet->payload(), 0, 8);
    for (int i = 0; i < 5; ++i) {
      EXPECT_FLOAT_EQ(vec[i], static_cast<float>(4 + i + 1)) << i;
    }
  }

  {
    // Finally attempt to read another 10 frames from frame 28, this should return the remaining 3
    // frames followed by silence.
    const auto packet = custom_stage->Read(DefaultCtx(), Fixed(28), 5);
    ASSERT_TRUE(packet);
    EXPECT_EQ(packet->start().Floor(), 28);
    EXPECT_EQ(packet->start().Fraction().raw_value(), 0);
    EXPECT_EQ(packet->length(), 5);

    const auto vec = ToVector(packet->payload(), 0, 5);
    for (int i = 0; i < 3; ++i) {
      EXPECT_FLOAT_EQ(vec[i], static_cast<float>(12 + i + 1)) << i;
    }
    for (int i = 3; i < 5; ++i) {
      EXPECT_FLOAT_EQ(vec[i], 1.0f) << i;
    }
  }
}

TEST_F(CustomStageTest, AddOneWithLatencyReadTwoPacketsWithGaps) {
  auto processor_info = MakeProcessorWithDifferentVmos<AddOneWithLatencyProcessor<2>>({
      .input_format = DefaultFormatWithChannels(1),
      .output_format = DefaultFormatWithChannels(1),
      .max_frames_per_call = 150,
      .block_size_frames = 15,
      .latency_frames = 2,
  });
  const Format source_format =
      Format::CreateLegacyOrDie(processor_info.config.inputs()[0].format());
  auto producer_stage = MakeDefaultPacketQueue(source_format);
  auto custom_stage = MakeCustomStage(std::move(processor_info.config), producer_stage);

  // Push two packets with a gap of 10 frames in between.
  std::vector<float> packet_payload_1(10);
  std::vector<float> packet_payload_2(10);
  for (int i = 0; i < 10; ++i) {
    packet_payload_1[i] = static_cast<float>(i);
    packet_payload_2[i] = static_cast<float>(20 + i);
  }
  producer_stage->push(PacketView({source_format, Fixed(0), 10, packet_payload_1.data()}));
  producer_stage->push(PacketView({source_format, Fixed(20), 10, packet_payload_2.data()}));

  {
    // Read the first 10 frames, this should return the first packet's frames.
    const auto packet = custom_stage->Read(DefaultCtx(), Fixed(0), 10);
    ASSERT_TRUE(packet);
    EXPECT_EQ(packet->start().Floor(), 0);
    EXPECT_EQ(packet->start().Fraction().raw_value(), 0);
    EXPECT_EQ(packet->length(), 10);

    const auto vec = ToVector(packet->payload(), 0, 10);
    for (int i = 0; i < 10; ++i) {
      EXPECT_FLOAT_EQ(vec[i], static_cast<float>(i + 1)) << i;
    }
  }

  {
    // Attempt to read the next 10 frames, this should return the cached 3 frames of silence that
    // was processed in the first read call.
    const auto packet = custom_stage->Read(DefaultCtx(), Fixed(10), 10);
    ASSERT_TRUE(packet);
    EXPECT_EQ(packet->start().Floor(), 10);
    EXPECT_EQ(packet->start().Fraction().raw_value(), 0);
    EXPECT_EQ(packet->length(), 3);

    const auto vec = ToVector(packet->payload(), 0, 3);
    for (int i = 0; i < 3; ++i) {
      EXPECT_FLOAT_EQ(vec[i], 1.0f) << i;
    }
  }

  {
    // Read the remaining 7 frames until the start of the second packet, this will read the next 15
    // frames as a result, which should return the first 7 frames of silence, since the remaining
    // frames contain the first 8 frames of the second packet.
    const auto packet = custom_stage->Read(DefaultCtx(), Fixed(13), 7);
    ASSERT_TRUE(packet);
    EXPECT_EQ(packet->start().Floor(), 13);
    EXPECT_EQ(packet->start().Fraction().raw_value(), 0);
    EXPECT_EQ(packet->length(), 7);

    const auto vec = ToVector(packet->payload(), 0, 7);
    for (int i = 0; i < 7; ++i) {
      EXPECT_FLOAT_EQ(vec[i], 1.0f) << i;
    }
  }

  {
    // Read the next 10 frames, this should return the cached first 8 frames of the second packet.
    const auto packet = custom_stage->Read(DefaultCtx(), Fixed(20), 30);
    ASSERT_TRUE(packet);
    EXPECT_EQ(packet->start().Floor(), 20);
    EXPECT_EQ(packet->start().Fraction().raw_value(), 0);
    EXPECT_EQ(packet->length(), 8);

    const auto vec = ToVector(packet->payload(), 0, 8);
    for (int i = 0; i < 8; ++i) {
      EXPECT_FLOAT_EQ(vec[i], static_cast<float>(20 + i + 1)) << i;
    }
  }
}

TEST_F(CustomStageTest, AddOneWithLatencyAndRingout) {
  auto processor_info = MakeProcessorWithDifferentVmos<AddOneWithLatencyProcessor<4>>({
      .input_format = DefaultFormatWithChannels(1),
      .output_format = DefaultFormatWithChannels(1),
      .max_frames_per_call = 100,
      .block_size_frames = 10,
      .latency_frames = 4,
      .ring_out_frames = 15,
  });
  const Format source_format =
      Format::CreateLegacyOrDie(processor_info.config.inputs()[0].format());
  auto producer_stage = MakeDefaultPacketQueue(source_format);
  auto custom_stage = MakeCustomStage(std::move(processor_info.config), producer_stage);

  // Push a single frame of impulse at frame 10.
  float impulse = 1.0f;
  producer_stage->push(PacketView({source_format, Fixed(10), 1, &impulse}));

  {
    // Read first 10 frames, which should return silence.
    const auto packet = custom_stage->Read(DefaultCtx(), Fixed(0), 10);
    ASSERT_TRUE(packet);
    EXPECT_EQ(packet->start().Floor(), 0);
    EXPECT_EQ(packet->start().Fraction().raw_value(), 0);
    EXPECT_EQ(packet->length(), 10);
    EXPECT_THAT(ToVector(packet->payload(), 0, 10), Each(1.0f));
  }

  {
    // Attempt to read another 10 frames, which should return the cached 6 frames, starting with the
    // the impulse followed by 5 ring out frames.
    const auto packet = custom_stage->Read(DefaultCtx(), Fixed(10), 10);
    ASSERT_TRUE(packet);
    EXPECT_EQ(packet->start().Floor(), 10);
    EXPECT_EQ(packet->start().Fraction().raw_value(), 0);
    EXPECT_EQ(packet->length(), 6);

    const auto vec = ToVector(packet->payload(), 0, 6);
    EXPECT_FLOAT_EQ(vec[0], 2.0f);
    for (int i = 1; i < 6; ++i) {
      EXPECT_FLOAT_EQ(vec[i], -1.0f) << i;
    }
  }

  {
    // Read 10 more frames which should return the remaining 10 ring out frames.
    const auto packet = custom_stage->Read(DefaultCtx(), Fixed(16), 10);
    ASSERT_TRUE(packet);
    EXPECT_EQ(packet->start().Floor(), 16);
    EXPECT_EQ(packet->start().Fraction().raw_value(), 0);
    EXPECT_EQ(packet->length(), 10);
    EXPECT_THAT(ToVector(packet->payload(), 0, 10), Each(-1.0f));
  }

  {
    // Attempt to read 10 more frames, which should not return any output beyond ring out frames.
    const auto packet = custom_stage->Read(DefaultCtx(), Fixed(26), 10);
    ASSERT_FALSE(packet);
  }
}

//
// ReturnMetricsProcessor
// Test an effect that returns metrics.
//

class ReturnMetricsProcessor : public CustomStageTestProcessor {
 public:
  ReturnMetricsProcessor(const ConfigOptions& options, fidl::ServerEnd<Processor> server_end,
                         async_dispatcher_t* dispatcher)
      : CustomStageTestProcessor(options, std::move(server_end), dispatcher) {}

  void Process(ProcessRequestView request, ProcessCompleter::Sync& completer) {
    completer.ReplySuccess(
        fidl::VectorView<fuchsia_audio_effects::wire::ProcessMetrics>::FromExternal(*metrics_));
  }

  void set_metrics(std::vector<fuchsia_audio_effects::wire::ProcessMetrics>* m) { metrics_ = m; }

 private:
  std::vector<fuchsia_audio_effects::wire::ProcessMetrics>* metrics_ = nullptr;
};

TEST_F(CustomStageTest, Metrics) {
  fidl::Arena arena;
  std::vector<fuchsia_audio_effects::wire::ProcessMetrics> expected_metrics(3);
  expected_metrics[0] = fuchsia_audio_effects::wire::ProcessMetrics::Builder(arena)
                            .name("CustomStage::Process")
                            .Build();
  expected_metrics[1] = fuchsia_audio_effects::wire::ProcessMetrics::Builder(arena)
                            .name("task1")
                            .wall_time(100)
                            .cpu_time(101)
                            .queue_time(102)
                            .Build();
  expected_metrics[2] = fuchsia_audio_effects::wire::ProcessMetrics::Builder(arena)
                            .name("task2")
                            .wall_time(200)
                            .cpu_time(201)
                            .queue_time(202)
                            .Build();

  auto info = MakeProcessorWithDifferentVmos<ReturnMetricsProcessor>({});
  info.processor.set_metrics(&expected_metrics);

  const Format input_format = Format::CreateLegacyOrDie(info.config.inputs()[0].format());
  const auto input_channels = info.config.inputs()[0].format().channel_count;

  // Enqueue one packet in the source packet queue.
  auto producer_stage = MakeDefaultPacketQueue(input_format);
  auto custom_stage = MakeCustomStage(std::move(info.config), producer_stage);

  constexpr auto kPacketFrames = 480;
  std::vector<float> packet_payload(kPacketFrames * input_channels, 1.0f);
  producer_stage->push(PacketView({input_format, Fixed(0), kPacketFrames, packet_payload.data()}));

  // Call Read and validate the metrics.
  MixJobContext ctx(DefaultClockSnapshots());
  auto packet = custom_stage->Read(ctx, Fixed(0), kPacketFrames);
  ASSERT_TRUE(packet);

  EXPECT_EQ(ctx.per_subtask_metrics().size(), expected_metrics.size());
  for (size_t k = 0; k < expected_metrics.size(); k++) {
    if (k >= ctx.per_subtask_metrics().size()) {
      break;
    }
    SCOPED_TRACE(fxl::StringPrintf("metrics[%lu]", k));
    auto& metrics = ctx.per_subtask_metrics()[k];
    EXPECT_EQ(static_cast<std::string_view>(metrics.name), expected_metrics[k].name().get());
    if (k == 0) {
      continue;
    }
    EXPECT_EQ(metrics.wall_time.to_nsecs(), expected_metrics[k].wall_time());
    EXPECT_EQ(metrics.cpu_time.to_nsecs(), expected_metrics[k].cpu_time());
    EXPECT_EQ(metrics.queue_time.to_nsecs(), expected_metrics[k].queue_time());
    EXPECT_EQ(metrics.page_fault_time.to_nsecs(), 0);
    EXPECT_EQ(metrics.kernel_lock_contention_time.to_nsecs(), 0);
  }
}

}  // namespace media_audio
