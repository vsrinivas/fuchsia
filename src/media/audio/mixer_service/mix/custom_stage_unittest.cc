// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/mixer_service/mix/custom_stage.h"

#include <fuchsia/audio/effects/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async-testing/test_loop.h>
#include <lib/fidl/llcpp/arena.h>
#include <lib/fzl/vmo-mapper.h>

#include <memory>

#include <fbl/algorithm.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "fidl/fuchsia.audio.effects/cpp/wire_types.h"
#include "lib/fidl/llcpp/internal/transport.h"
#include "lib/fidl/llcpp/object_view.h"
#include "lib/fidl/llcpp/vector_view.h"
#include "src/lib/fxl/strings/string_printf.h"
#include "src/lib/testing/loop_fixture/test_loop_fixture.h"
#include "src/media/audio/lib/clock/clone_mono.h"
#include "src/media/audio/mixer_service/common/basic_types.h"
#include "src/media/audio/mixer_service/common/thread_checker.h"
#include "src/media/audio/mixer_service/mix/detached_thread.h"
#include "src/media/audio/mixer_service/mix/packet_queue_producer_stage.h"
#include "src/media/audio/mixer_service/mix/ptr_decls.h"

namespace media_audio_mixer_service {

namespace {

using ::fuchsia_audio_effects::Processor;
using ::fuchsia_audio_effects::wire::ProcessorConfiguration;
using ::fuchsia_mediastreams::wire::AudioSampleFormat;
using ::media::audio::clock::CloneOfMonotonic;
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
      .sample_format = AudioSampleFormat::kFloat,
      .channel_count = 1,
      .frames_per_second = 48000,
  };
  fuchsia_mediastreams::wire::AudioFormat output_format = {
      .sample_format = AudioSampleFormat::kFloat,
      .channel_count = 1,
      .frames_per_second = 48000,
  };
  uint64_t max_frames_per_call = 0;
  uint64_t block_size_frames = 1;
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
                      .latency_frames(0)
                      .ring_out_frames(0)
                      .Build();
  builder.outputs(fidl::ObjectView{arena, outputs});

  builder.processor(std::move(client));

  return builder.Build();
}

fuchsia_mediastreams::wire::AudioFormat DefaultFormatWithChannels(uint32_t channels) {
  return {
      .sample_format = AudioSampleFormat::kFloat,
      .channel_count = channels,
      .frames_per_second = 48000,
  };
}

PipelineStagePtr MakeCustomStage(ProcessorConfiguration config, PipelineStagePtr source_stage) {
  PipelineStagePtr custom_stage = std::make_shared<CustomStage>(config);
  custom_stage->set_thread(DetachedThread::Create());
  ScopedThreadChecker checker(custom_stage->thread()->checker());
  custom_stage->AddSource(std::move(source_stage));
  return custom_stage;
}

std::shared_ptr<PacketQueueProducerStage> MakePacketQueueProducerStage(Format format) {
  return std::make_shared<PacketQueueProducerStage>(
      format, std::make_unique<AudioClock>(AudioClock::ClientFixed(CloneOfMonotonic())));
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
                                      FX_PLOGS(ERROR, info.status())
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

class CustomStageTest : public gtest::TestLoopFixture {
 public:
  template <class ProcessorType>
  struct ProcessorInfo {
    ProcessorType processor;
    bool in_place;
    ProcessorConfiguration config;
  };

  void SetUp() override {
    gtest::TestLoopFixture::SetUp();
    fidl_loop_.StartThread("fidl");
  }

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
    const Format source_format = Format::CreateOrDie(info.config.inputs()[0].format());

    auto producer_stage = MakePacketQueueProducerStage(source_format);
    auto custom_stage = MakeCustomStage(std::move(info.config), producer_stage);

    // Push one packet of the requested size.
    std::vector<float> packet_payload(packet_frames * input_channels, 1.0f);
    producer_stage->push(PacketView(
        PacketView::Args{source_format, Fixed(0), packet_frames, packet_payload.data()}));

    {
      // Read the first packet. Since our effect adds 1.0 to each sample, and we populated the
      // packet with 1.0 samples, we expect to see only 2.0 samples in the result.
      auto packet = custom_stage->Read(Fixed(0), packet_frames);
      ASSERT_TRUE(packet);
      EXPECT_EQ(packet->start().Floor(), 0);
      EXPECT_EQ(packet->start().Fraction().raw_value(), 0);
      EXPECT_EQ(packet->length(), read_buffer_frames);

      auto vec = ToVector(packet->payload(), 0, read_buffer_frames * output_channels);
      EXPECT_THAT(vec, Each(FloatEq(2.0f)));

      // If the process is in-place, input should be overwritten. Otherwise it should be unchanged.
      if (info.in_place) {
        auto vec = ToVector(info.processor.input_data(), 0, read_buffer_frames * input_channels);
        EXPECT_THAT(vec, Each(FloatEq(2.0f)));
      } else {
        auto vec = ToVector(info.processor.input_data(), 0, read_buffer_frames * input_channels);
        EXPECT_THAT(vec, Each(FloatEq(1.0f)));
      }
    }

    {
      // Read the next packet. This should be null, because there are no more packets.
      auto packet = custom_stage->Read(Fixed(packet_frames), packet_frames);
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
    auto num_frames = request->num_frames;
    for (; num_frames > 0; num_frames--) {
      for (uint32_t k = 0; k < num_channels_; k++, input++, output++) {
        *output = (*input) + 1;
      }
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

    const Format source_format = Format::CreateOrDie(info.config.inputs()[0].format());
    auto producer_stage = MakePacketQueueProducerStage(source_format);
    auto custom_stage = MakeCustomStage(info.config, producer_stage);

    // Push one packet with `source_offset`.
    std::vector<float> packet_payload(kPacketFrames, 1.0f);
    producer_stage->push(PacketView(
        PacketView::Args{source_format, source_offset, kPacketFrames, packet_payload.data()}));

    // Source frame 100.5 is sampled at dest frame 101.
    const int64_t dest_offset_frames = source_offset.Ceiling();

    {
      // Read the first packet. Since the first source packet is offset by `source_offset`, we
      // should read silence from the source followed by 1.0s. The effect adds one to these values,
      // so we should see 1.0s followed by 2.0s.
      auto packet = custom_stage->Read(Fixed(0), kPacketFrames);
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
      auto packet = custom_stage->Read(Fixed(kPacketFrames), kPacketFrames);
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
      auto packet = custom_stage->Read(Fixed(2 * kPacketFrames), kPacketFrames);
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
  const Format source_format = Format::CreateOrDie(info.config.inputs()[0].format());
  auto producer_stage = MakePacketQueueProducerStage(source_format);
  auto custom_stage = MakeCustomStage(info.config, producer_stage);

  std::vector<float> packet_payload(480, 1.0f);
  producer_stage->push(
      PacketView(PacketView::Args{source_format, Fixed(0), 480, packet_payload.data()}));

  {
    // Read the first packet.
    auto packet = custom_stage->Read(Fixed(0), 480);
    ASSERT_TRUE(packet);
    EXPECT_EQ(packet->start().Floor(), 0);
    EXPECT_EQ(packet->start().Fraction().raw_value(), 0);
    EXPECT_EQ(packet->length(), 480);

    // Our effect adds 1.0, and the source packet is 1.0, so the payload should contain all 2.0.
    auto vec = ToVector(packet->payload(), 0, 480);
    EXPECT_THAT(vec, Each(FloatEq(2.0f)));
  }

  {
    // The source stream does not have a second packet, however when we processed the first packet,
    // we processed 720 frames total (480 from the first packet + 240 of silence). This `Read`
    // should return those 240 frames.
    auto packet = custom_stage->Read(Fixed(480), 480);
    ASSERT_TRUE(packet);
    EXPECT_EQ(packet->start().Floor(), 480);
    EXPECT_EQ(packet->start().Fraction().raw_value(), 0);
    EXPECT_EQ(packet->length(), 240);

    // Since the source stream was silent, and our effect adds 1.0, the payload is 1.0.
    auto vec = ToVector(packet->payload(), 0, 240);
    EXPECT_THAT(vec, Each(FloatEq(1.0f)));
  }

  {
    // Read again where we left off. This should be null, because our cache is exhausted and the
    // source has no more data.
    auto packet = custom_stage->Read(Fixed(720), 480);
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
  const Format source_format = Format::CreateOrDie(info.config.inputs()[0].format());
  auto producer_stage = MakePacketQueueProducerStage(source_format);
  auto custom_stage = MakeCustomStage(info.config, producer_stage);

  std::vector<float> packet_payload(480, 1.0f);
  producer_stage->push(
      PacketView(PacketView::Args{source_format, Fixed(720), 480, packet_payload.data()}));

  {
    // This `Read` will attempt read 720 frames from the source, but the source is empty.
    auto packet = custom_stage->Read(Fixed(0), 480);
    ASSERT_FALSE(packet);
  }

  {
    // This `Read` should not read anything from the source because we know from the prior `Read`
    // that the source is empty until 720.
    auto packet = custom_stage->Read(Fixed(480), 240);
    ASSERT_FALSE(packet);
  }

  {
    // Now we have data.
    auto packet = custom_stage->Read(Fixed(720), 480);
    ASSERT_TRUE(packet);
    EXPECT_EQ(packet->start().Floor(), 720);
    EXPECT_EQ(packet->start().Fraction().raw_value(), 0);
    EXPECT_EQ(packet->length(), 480);

    // Our effect adds 1.0, and the source packet is 1.0, so the payload should contain all 2.0.
    auto vec = ToVector(packet->payload(), 0, 480);
    EXPECT_THAT(vec, Each(FloatEq(2.0f)));
  }

  {
    // The source stream ends at frame 720+480=1200, however the last `Read` processed 240
    // additional frames from the source. This `Read` should return those 240 frames.
    auto packet = custom_stage->Read(Fixed(1200), 480);
    ASSERT_TRUE(packet);
    EXPECT_EQ(packet->start().Floor(), 1200);
    EXPECT_EQ(packet->start().Fraction().raw_value(), 0);
    EXPECT_EQ(packet->length(), 240);

    // Our effect adds 1.0, and the source range is silent, so the payload should contain all 1.0s.
    auto vec = ToVector(packet->payload(), 0, 240);
    EXPECT_THAT(vec, Each(FloatEq(1.0f)));
  }

  {
    // Read again where we left off. This should be null, because our cache is exhausted and the
    // source has no more data.
    auto packet = custom_stage->Read(Fixed(1440), 480);
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
    auto num_frames = request->num_frames;
    for (; num_frames > 0; num_frames--, input++, output += 2) {
      output[0] = input[0] + 1;
      output[1] = input[0] + 1;
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
    auto num_frames = request->num_frames;
    for (; num_frames > 0; num_frames--, input += 2, output++) {
      output[0] = input[0] + 1;
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

}  // namespace media_audio_mixer_service
