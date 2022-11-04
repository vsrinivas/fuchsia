// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/v1/effects_stage_v2.h"

#include <fuchsia/audio/effects/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fzl/vmo-mapper.h>

#include <gmock/gmock.h>

#include "src/lib/fxl/strings/string_printf.h"
#include "src/media/audio/audio_core/v1/packet_queue.h"
#include "src/media/audio/audio_core/v1/testing/fake_packet_queue.h"
#include "src/media/audio/audio_core/v1/testing/packet_factory.h"
#include "src/media/audio/audio_core/v1/testing/threading_model_fixture.h"
#include "src/media/audio/lib/clock/clone_mono.h"

using testing::Each;
using testing::FloatEq;
using ASF = fuchsia_mediastreams::wire::AudioSampleFormat;

// Arena type used by test code. The initial size does not matter since
// this is a test (it's ok to dynamically allocate).
using Arena = fidl::Arena<512>;

namespace media::audio {
namespace {

// Used when the ReadLockContext is unused by the test.
static media::audio::ReadableStream::ReadLockContext rlctx;

const Format k48k1ChanFloatFormat =
    Format::Create(fuchsia::media::AudioStreamType{
                       .sample_format = fuchsia::media::AudioSampleFormat::FLOAT,
                       .channels = 1,
                       .frames_per_second = 48000,
                   })
        .take_value();

const Format k48k2ChanFloatFormat =
    Format::Create(fuchsia::media::AudioStreamType{
                       .sample_format = fuchsia::media::AudioSampleFormat::FLOAT,
                       .channels = 2,
                       .frames_per_second = 48000,
                   })
        .take_value();

std::vector<float> as_vec(void* payload, size_t sample_start_idx, size_t sample_end_idx) {
  float* p = reinterpret_cast<float*>(payload);
  return std::vector<float>(p + sample_start_idx, p + sample_end_idx);
}

fuchsia_mediastreams::wire::AudioFormat DefaultFormatWithChannels(uint32_t channels) {
  return {
      .sample_format = ASF::kFloat,
      .channel_count = channels,
      .frames_per_second = 48000,
  };
}

Format ToOldFormat(const fuchsia_mediastreams::wire::AudioFormat& new_format) {
  FX_CHECK(new_format.sample_format == fuchsia_mediastreams::wire::AudioSampleFormat::kFloat);
  return Format::Create(fuchsia::media::AudioStreamType{
                            .sample_format = fuchsia::media::AudioSampleFormat::FLOAT,
                            .channels = new_format.channel_count,
                            .frames_per_second = new_format.frames_per_second,
                        })
      .take_value();
}

zx::vmo CreateVmoOrDie(uint64_t size_bytes) {
  zx::vmo vmo;
  if (auto status = zx::vmo::create(size_bytes, 0, &vmo); status != ZX_OK) {
    FX_PLOGS(FATAL, status) << "failed to create VMO with size " << size_bytes;
  }
  return vmo;
}

zx::vmo DupVmoOrDie(const zx::vmo& vmo, zx_rights_t rights) {
  zx::vmo dup;
  if (auto status = vmo.duplicate(rights, &dup); status != ZX_OK) {
    FX_PLOGS(FATAL, status) << "failed to duplicate VMO with rights 0x" << std::hex << rights;
  }
  return dup;
}

//
// ConfigOptions
// This struct is a shorthand for specifying a ProcessorConfiguration.
//

struct ConfigOptions {
  bool in_place = false;
  fuchsia_mem::wire::Range input_buffer = {.offset = 0, .size = 0};
  fuchsia_mem::wire::Range output_buffer = {.offset = 0, .size = 0};
  fuchsia_mediastreams::wire::AudioFormat input_format = {
      .sample_format = ASF::kFloat,
      .channel_count = 1,
      .frames_per_second = 48000,
  };
  fuchsia_mediastreams::wire::AudioFormat output_format = {
      .sample_format = ASF::kFloat,
      .channel_count = 1,
      .frames_per_second = 48000,
  };
  uint64_t latency_frames = 0;
  uint64_t ring_out_frames = 0;
  uint64_t max_frames_per_call = 0;
  uint64_t block_size_frames = 0;
};

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
      .latency_frames = options.latency_frames,
      .ring_out_frames = options.ring_out_frames,
      .max_frames_per_call = options.max_frames_per_call,
      .block_size_frames = options.block_size_frames,
  };
}

fuchsia_audio_effects::wire::ProcessorConfiguration MakeProcessorConfig(Arena& arena,
                                                                        ConfigOptions options) {
  fuchsia_audio_effects::wire::ProcessorConfiguration config(arena);

  if (options.max_frames_per_call) {
    config.set_max_frames_per_call(arena, options.max_frames_per_call);
  }
  if (options.block_size_frames) {
    config.set_block_size_frames(arena, options.block_size_frames);
  }

  if (auto& buffer = options.input_buffer; buffer.vmo.is_valid()) {
    buffer.vmo = DupVmoOrDie(buffer.vmo, ZX_RIGHT_MAP | ZX_RIGHT_READ | ZX_RIGHT_WRITE);
  }
  if (auto& buffer = options.output_buffer; buffer.vmo.is_valid()) {
    buffer.vmo = DupVmoOrDie(buffer.vmo, ZX_RIGHT_MAP | ZX_RIGHT_READ | ZX_RIGHT_WRITE);
  }

  fidl::VectorView<fuchsia_audio_effects::wire::InputConfiguration> inputs(arena, 1);
  inputs[0].Allocate(arena);
  inputs[0].set_buffer(arena, std::move(options.input_buffer));
  inputs[0].set_format(arena, std::move(options.input_format));

  fidl::VectorView<fuchsia_audio_effects::wire::OutputConfiguration> outputs(arena, 1);
  outputs[0].Allocate(arena);
  outputs[0].set_buffer(arena, std::move(options.output_buffer));
  outputs[0].set_format(arena, std::move(options.output_format));
  if (options.latency_frames) {
    outputs[0].set_latency_frames(arena, options.latency_frames);
  }
  if (options.ring_out_frames) {
    outputs[0].set_ring_out_frames(arena, options.ring_out_frames);
  }

  config.set_inputs(fidl::ObjectView{arena, inputs});
  config.set_outputs(fidl::ObjectView{arena, outputs});
  return config;
}

fidl::ServerEnd<fuchsia_audio_effects::Processor> AttachProcessorChannel(
    fuchsia_audio_effects::wire::ProcessorConfiguration& config) {
  auto endpoints = fidl::CreateEndpoints<fuchsia_audio_effects::Processor>();
  if (!endpoints.is_ok()) {
    FX_PLOGS(FATAL, endpoints.status_value()) << "failed to construct a zx::channel";
  }

  config.set_processor(std::move(endpoints->client));
  return std::move(endpoints->server);
}

fuchsia_audio_effects::wire::ProcessorConfiguration DefaultGoodProcessorConfig(Arena& arena) {
  constexpr auto kBytes = 480 * sizeof(float);

  ConfigOptions options;
  CreateSeparateVmos(options, kBytes, kBytes);

  auto config = MakeProcessorConfig(arena, std::move(options));
  auto unused_server_end = AttachProcessorChannel(config);
  return config;
}

//
// Processors
//

class BaseProcessor : public fidl::WireServer<fuchsia_audio_effects::Processor> {
 public:
  BaseProcessor(const ConfigOptions& options,
                fidl::ServerEnd<fuchsia_audio_effects::Processor> server_end,
                async_dispatcher_t* dispatcher)
      : binding_(fidl::BindServer(dispatcher, std::move(server_end), this,
                                  [](BaseProcessor* impl, fidl::UnbindInfo info,
                                     fidl::ServerEnd<fuchsia_audio_effects::Processor> server_end) {
                                    if (!info.is_user_initiated() && !info.is_peer_closed()) {
                                      FX_PLOGS(ERROR, info.status())
                                          << "Client disconnected unexpectedly: " << info;
                                    }
                                  })),
        buffers_(EffectsStageV2::FidlBuffers::Create(options.input_buffer, options.output_buffer)) {
  }

  float* input_data() const { return reinterpret_cast<float*>(buffers_.input); }
  float* output_data() const { return reinterpret_cast<float*>(buffers_.output); }

 private:
  fidl::ServerBindingRef<fuchsia_audio_effects::Processor> binding_;
  EffectsStageV2::FidlBuffers buffers_;
};

//
// Test Cases
//

class EffectsStageV2Test : public testing::ThreadingModelFixture {
 public:
  void SetUp() override {
    testing::ThreadingModelFixture::SetUp();
    fidl_loop_.StartThread("fidl-processor-thread");
  }

  async_dispatcher_t* fidl_dispatcher() const { return fidl_loop_.dispatcher(); }

  std::shared_ptr<testing::FakePacketQueue> MakePacketQueue(const Format& format) {
    auto timeline_function = fbl::MakeRefCounted<VersionedTimelineFunction>(TimelineFunction(
        TimelineRate(Fixed(format.frames_per_second()).raw_value(), zx::sec(1).to_nsecs())));
    return std::make_shared<testing::FakePacketQueue>(
        std::vector<fbl::RefPtr<Packet>>(), format, timeline_function,
        context().clock_factory()->CreateClientFixed(clock::AdjustableCloneOfMonotonic()));
  }

  // Every test reads from a FakePacketQueue.
  std::tuple<std::unique_ptr<testing::PacketFactory>, std::shared_ptr<testing::FakePacketQueue>,
             std::shared_ptr<EffectsStageV2>>
  MakeEffectsStage(fuchsia_audio_effects::wire::ProcessorConfiguration config) {
    const Format source_format = ToOldFormat(config.inputs()[0].format());
    auto packet_factory = std::make_unique<testing::PacketFactory>(dispatcher(), source_format,
                                                                   zx_system_get_page_size());
    auto stream = MakePacketQueue(source_format);
    auto effects_stage = EffectsStageV2::Create(std::move(config), stream).take_value();
    return std::make_tuple(std::move(packet_factory), std::move(stream), std::move(effects_stage));
  }

  Arena& arena() { return arena_; }

  // By default, the MakeProcessorWith*() functions create input and output buffers
  // that are large enough to process at most this many frames.
  static constexpr auto kProcessingBufferMaxFrames = 1024;

  template <class ProcessorT>
  struct ProcessorInfo {
    ProcessorT processor;
    bool in_place;
    fuchsia_audio_effects::wire::ProcessorConfiguration config;
  };

  template <class T>
  ProcessorInfo<T> MakeProcessor(ConfigOptions options);

  template <class T>
  ProcessorInfo<T> MakeProcessorWithDifferentVmos(ConfigOptions options);

  template <class T>
  ProcessorInfo<T> MakeProcessorWithSameRange(ConfigOptions options);

  template <class T>
  ProcessorInfo<T> MakeProcessorWithSameVmoDifferentRanges(ConfigOptions options);

  // A simple test case where the source is a packet queue with a single
  // packet of the given size. The test makes two ReadLock calls:
  //
  //   1. ReadLock(0, packet_frames), which should return a buffer of size
  //      read_lock_buffer_frames containing data processed by the AddOne effect.
  //
  //   2. ReadLock(packet_frames, packet_frames), which should return std::nullopt.
  //
  template <class T>
  void TestAddOneWithSinglePacket(ProcessorInfo<T> info, int64_t packet_frames = 480,
                                  int64_t read_lock_buffer_frames = 480);

 private:
  Arena arena_;
  async::Loop fidl_loop_{&kAsyncLoopConfigNoAttachToCurrentThread};
};

template <class T>
EffectsStageV2Test::ProcessorInfo<T> EffectsStageV2Test::MakeProcessor(ConfigOptions options) {
  if (options.max_frames_per_call) {
    FX_CHECK(options.max_frames_per_call < kProcessingBufferMaxFrames);
  }
  if (options.block_size_frames) {
    FX_CHECK(options.block_size_frames < kProcessingBufferMaxFrames);
  }

  auto config = MakeProcessorConfig(arena(), DupConfigOptions(options));
  auto server_end = AttachProcessorChannel(config);
  return {
      .processor = T(options, std::move(server_end), fidl_dispatcher()),
      .in_place = options.in_place,
      .config = std::move(config),
  };
}

// The processor uses different VMOs for the input and output.
template <class T>
EffectsStageV2Test::ProcessorInfo<T> EffectsStageV2Test::MakeProcessorWithDifferentVmos(
    ConfigOptions options) {
  const auto kInputChannels = options.input_format.channel_count;
  const auto kOutputChannels = options.output_format.channel_count;

  const auto kInputBufferBytes = kProcessingBufferMaxFrames * kInputChannels * sizeof(float);
  const auto kOutputBufferBytes = kProcessingBufferMaxFrames * kOutputChannels * sizeof(float);
  CreateSeparateVmos(options, kInputBufferBytes, kOutputBufferBytes);

  return MakeProcessor<T>(std::move(options));
}

// The processor uses the same fuchsia.mem.Range for the input and output.
// This is an in-place update.
template <class T>
EffectsStageV2Test::ProcessorInfo<T> EffectsStageV2Test::MakeProcessorWithSameRange(
    ConfigOptions options) {
  FX_CHECK(options.input_format.channel_count == options.output_format.channel_count)
      << "In-place updates requires matched input and output channels";

  const auto kVmoSamples = kProcessingBufferMaxFrames * options.input_format.channel_count;
  const auto kVmoBytes = kVmoSamples * sizeof(float);

  CreateSharedVmo(options, kVmoBytes,  // VMO size
                  0, kVmoBytes,        // input buffer offset & size
                  0, kVmoBytes);       // output buffer offset & size

  return MakeProcessor<T>(std::move(options));
}

// The processor uses non-overlapping ranges of the same VMO for the input and output.
template <class T>
EffectsStageV2Test::ProcessorInfo<T> EffectsStageV2Test::MakeProcessorWithSameVmoDifferentRanges(
    ConfigOptions options) {
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

  return MakeProcessor<T>(std::move(options));
}

// Generic test for a processor that adds one to each input sample.
template <class T>
void EffectsStageV2Test::TestAddOneWithSinglePacket(ProcessorInfo<T> info, int64_t packet_frames,
                                                    int64_t read_lock_buffer_frames) {
  const auto input_channels = info.config.inputs()[0].format().channel_count;
  const auto output_channels = info.config.outputs()[0].format().channel_count;
  const Format source_format = ToOldFormat(info.config.inputs()[0].format());

  auto [packet_factory, stream, effects_stage] = MakeEffectsStage(std::move(info.config));

  // Enqueue one packet of the requested size.
  const auto packet_duration =
      zx::duration(source_format.frames_per_ns().Inverse().Scale(packet_frames));
  stream->PushPacket(packet_factory->CreatePacket(1.0, packet_duration));

  {
    // Read the first packet. Since our effect adds 1.0 to each sample, and we populated the
    // packet with 1.0 samples, we expect to see only 2.0 samples in the result.
    auto buf = effects_stage->ReadLock(rlctx, Fixed(0), packet_frames);
    ASSERT_TRUE(buf);
    EXPECT_EQ(buf->start().Floor(), 0);
    EXPECT_EQ(buf->start().Fraction().raw_value(), 0);
    EXPECT_EQ(buf->length(), read_lock_buffer_frames);

    auto vec = as_vec(buf->payload(), 0, read_lock_buffer_frames * output_channels);
    EXPECT_THAT(vec, Each(FloatEq(2.0f)));

    // If the update was in-place, the input should have been overwritten.
    // Otherwise it should be unchanged.
    if (info.in_place) {
      auto vec = as_vec(info.processor.input_data(), 0, read_lock_buffer_frames * input_channels);
      EXPECT_THAT(vec, Each(FloatEq(2.0f)));
    } else {
      auto vec = as_vec(info.processor.input_data(), 0, read_lock_buffer_frames * input_channels);
      EXPECT_THAT(vec, Each(FloatEq(1.0f)));
    }
  }

  {
    // Read the next packet. This should be null, because there are no more packets.
    auto buf = effects_stage->ReadLock(rlctx, Fixed(packet_frames), packet_frames);
    ASSERT_FALSE(buf);
  }
}

//
// AddOneProcessor
// Basic tests for an N chan -> N chan effect
//

class AddOneProcessor : public BaseProcessor {
 public:
  AddOneProcessor(const ConfigOptions& options,
                  fidl::ServerEnd<fuchsia_audio_effects::Processor> server_end,
                  async_dispatcher_t* dispatcher)
      : BaseProcessor(options, std::move(server_end), dispatcher),
        num_channels_(options.input_format.channel_count) {
    FX_CHECK(options.input_format.channel_count == options.output_format.channel_count);
  }

  void Process(ProcessRequestView request, ProcessCompleter::Sync& completer) {
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

TEST_F(EffectsStageV2Test, AddOneWithOneChanDifferentVmos) {
  auto processor_info = MakeProcessorWithDifferentVmos<AddOneProcessor>({
      .input_format = DefaultFormatWithChannels(1),
      .output_format = DefaultFormatWithChannels(1),
  });
  TestAddOneWithSinglePacket(std::move(processor_info));
}

TEST_F(EffectsStageV2Test, AddOneWithTwoChanDifferentVmos) {
  auto processor_info = MakeProcessorWithDifferentVmos<AddOneProcessor>({
      .input_format = DefaultFormatWithChannels(2),
      .output_format = DefaultFormatWithChannels(2),
  });
  TestAddOneWithSinglePacket(std::move(processor_info));
}

TEST_F(EffectsStageV2Test, AddOneWithOneChanSameRange) {
  auto processor_info = MakeProcessorWithSameRange<AddOneProcessor>({
      .input_format = DefaultFormatWithChannels(1),
      .output_format = DefaultFormatWithChannels(1),
  });
  TestAddOneWithSinglePacket(std::move(processor_info));
}

TEST_F(EffectsStageV2Test, AddOneWithOneChanSameVmoDifferentRanges) {
  auto processor_info = MakeProcessorWithSameVmoDifferentRanges<AddOneProcessor>({
      .input_format = DefaultFormatWithChannels(1),
      .output_format = DefaultFormatWithChannels(1),
  });
  TestAddOneWithSinglePacket(std::move(processor_info));
}

TEST_F(EffectsStageV2Test, AddOneWithSourceOffset) {
  constexpr auto kPacketFrames = 480;
  constexpr auto kPacketDuration = zx::msec(10);

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

    auto [packet_factory, stream, effects_stage] = MakeEffectsStage(std::move(info.config));
    packet_factory->SeekToFrame(source_offset);
    stream->PushPacket(packet_factory->CreatePacket(1.0, kPacketDuration));

    // Source frame 100.5 is sampled at dest frame 101.
    const int64_t dest_offset_frames = source_offset.Ceiling();

    {
      // Read the first packet. Since the first source packet is offset by source_offset,
      // we should read silence from the source followed by 1.0s. The effect adds one to these
      // values, so we should see 1.0s followed by 2.0s.
      auto buf = effects_stage->ReadLock(rlctx, Fixed(0), kPacketFrames);
      ASSERT_TRUE(buf);
      EXPECT_EQ(buf->start().Floor(), 0);
      EXPECT_EQ(buf->start().Fraction().raw_value(), 0);
      EXPECT_EQ(buf->length(), kPacketFrames);

      auto vec1 = as_vec(buf->payload(), 0, dest_offset_frames);
      auto vec2 = as_vec(buf->payload(), dest_offset_frames, kPacketFrames);
      EXPECT_THAT(vec1, Each(FloatEq(1.0f)));
      EXPECT_THAT(vec2, Each(FloatEq(2.0f)));
    }

    {
      // Read the second packet. This should contain the remainder of the 2.0s, followed
      // by 1.0s.
      auto buf = effects_stage->ReadLock(rlctx, Fixed(kPacketFrames), kPacketFrames);
      ASSERT_TRUE(buf);
      EXPECT_EQ(buf->start().Floor(), kPacketFrames);
      EXPECT_EQ(buf->start().Fraction().raw_value(), 0);
      EXPECT_EQ(buf->length(), kPacketFrames);

      auto vec1 = as_vec(buf->payload(), 0, dest_offset_frames);
      auto vec2 = as_vec(buf->payload(), dest_offset_frames, kPacketFrames);
      EXPECT_THAT(vec1, Each(FloatEq(2.0f)));
      EXPECT_THAT(vec2, Each(FloatEq(1.0f)));
    }

    {
      // Read the next packet. This should be null, because there are no more packets.
      auto buf = effects_stage->ReadLock(rlctx, Fixed(2 * kPacketFrames), kPacketFrames);
      ASSERT_FALSE(buf);
    }
  }
}

TEST_F(EffectsStageV2Test, AddOneWithReadLockSmallerThanProcessingBuffer) {
  auto info = MakeProcessorWithSameRange<AddOneProcessor>({
      .input_format = DefaultFormatWithChannels(1),
      .output_format = DefaultFormatWithChannels(1),
      .max_frames_per_call = 720,
      .block_size_frames = 720,
  });

  // Queue one 10ms packet (480 frames).
  auto [packet_factory, stream, effects_stage] = MakeEffectsStage(std::move(info.config));
  stream->PushPacket(packet_factory->CreatePacket(1.0, zx::msec(10)));

  {
    // Read the first packet.
    auto buf = effects_stage->ReadLock(rlctx, Fixed(0), 480);
    ASSERT_TRUE(buf);
    EXPECT_EQ(buf->start().Floor(), 0);
    EXPECT_EQ(buf->start().Fraction().raw_value(), 0);
    EXPECT_EQ(buf->length(), 480);

    // Our effect adds 1.0, and the source packet is 1.0, so the payload should contain all 2.0.
    auto vec = as_vec(buf->payload(), 0, 480);
    EXPECT_THAT(vec, Each(FloatEq(2.0f)));
  }

  {
    // The source stream does not have a second packet, however when we processed the first
    // packet, we processed 720 frames total (480 from the first packet + 240 of silence).
    // This ReadLock should return those 240 frames.
    auto buf = effects_stage->ReadLock(rlctx, Fixed(480), 480);
    ASSERT_TRUE(buf);
    EXPECT_EQ(buf->start().Floor(), 480);
    EXPECT_EQ(buf->start().Fraction().raw_value(), 0);
    EXPECT_EQ(buf->length(), 240);

    // Since the source stream was silent, and our effect adds 1.0, the payload is 1.0.
    auto vec = as_vec(buf->payload(), 0, 240);
    EXPECT_THAT(vec, Each(FloatEq(1.0f)));
  }

  {
    // Read again where we left off. This should be null, because our cache is exhausted
    // and the source has no more data.
    auto buf = effects_stage->ReadLock(rlctx, Fixed(720), 480);
    ASSERT_FALSE(buf);
  }
}

TEST_F(EffectsStageV2Test, AddOneWithReadLockSmallerThanProcessingBufferAndSourceOffset) {
  auto info = MakeProcessorWithSameRange<AddOneProcessor>({
      .input_format = DefaultFormatWithChannels(1),
      .output_format = DefaultFormatWithChannels(1),
      .max_frames_per_call = 720,
      .block_size_frames = 720,
  });

  // Queue one 10ms packet (480 frames) starting at frame 720.
  auto [packet_factory, stream, effects_stage] = MakeEffectsStage(std::move(info.config));
  packet_factory->SeekToFrame(Fixed(720));
  stream->PushPacket(packet_factory->CreatePacket(1.0, zx::msec(10)));

  {
    // This ReadLock will attempt read 720 frames from the source, but the source is empty.
    auto buf = effects_stage->ReadLock(rlctx, Fixed(0), 480);
    ASSERT_FALSE(buf);
  }

  {
    // This ReadLock should not read anything from the source because we know
    // from the prior ReadLock that the source is empty until 720.
    auto buf = effects_stage->ReadLock(rlctx, Fixed(480), 240);
    ASSERT_FALSE(buf);
  }

  {
    // Now we have data.
    auto buf = effects_stage->ReadLock(rlctx, Fixed(720), 480);
    ASSERT_TRUE(buf);
    EXPECT_EQ(buf->start().Floor(), 720);
    EXPECT_EQ(buf->start().Fraction().raw_value(), 0);
    EXPECT_EQ(buf->length(), 480);

    // Our effect adds 1.0, and the source packet is 1.0, so the payload should contain all 2.0.
    auto vec = as_vec(buf->payload(), 0, 480);
    EXPECT_THAT(vec, Each(FloatEq(2.0f)));
  }

  {
    // The source stream ends at frame 720+480=1200, however the last ReadLock processed
    // 240 additional frames from the source. This ReadLock should return those 240 frames.
    auto buf = effects_stage->ReadLock(rlctx, Fixed(1200), 480);
    ASSERT_TRUE(buf);
    EXPECT_EQ(buf->start().Floor(), 1200);
    EXPECT_EQ(buf->start().Fraction().raw_value(), 0);
    EXPECT_EQ(buf->length(), 240);

    // Our effect adds 1.0, and the source range is silent, so the payload should contain all 1.0s.
    auto vec = as_vec(buf->payload(), 0, 240);
    EXPECT_THAT(vec, Each(FloatEq(1.0f)));
  }

  {
    // Read again where we left off. This should be null, because our cache is exhausted
    // and the source has no more data.
    auto buf = effects_stage->ReadLock(rlctx, Fixed(1440), 480);
    ASSERT_FALSE(buf);
  }
}

//
// AddOneAndDupChannelProcessor
// Test rechannelization from 1 chan -> 2 chan
//
// Since we're adding a channel, we can't (easily) write an in-place processor,
// so we don't test that configuration.
//

class AddOneAndDupChannelProcessor : public BaseProcessor {
 public:
  AddOneAndDupChannelProcessor(const ConfigOptions& options,
                               fidl::ServerEnd<fuchsia_audio_effects::Processor> server_end,
                               async_dispatcher_t* dispatcher)
      : BaseProcessor(options, std::move(server_end), dispatcher) {
    FX_CHECK(options.input_format.channel_count == 1);
    FX_CHECK(options.output_format.channel_count == 2);
  }

  void Process(ProcessRequestView request, ProcessCompleter::Sync& completer) {
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

TEST_F(EffectsStageV2Test, AddOneAndDupChannelWithDifferentVmos) {
  auto processor_info = MakeProcessorWithDifferentVmos<AddOneAndDupChannelProcessor>({
      .input_format = DefaultFormatWithChannels(1),
      .output_format = DefaultFormatWithChannels(2),
  });
}

TEST_F(EffectsStageV2Test, AddOneAndDupChannelWithSameVmoDifferentRanges) {
  auto processor_info = MakeProcessorWithSameVmoDifferentRanges<AddOneAndDupChannelProcessor>({
      .input_format = DefaultFormatWithChannels(1),
      .output_format = DefaultFormatWithChannels(2),
  });
}

//
// AddOneAndRemoveChannelProcessor
// Test rechannelization from 2 chan -> 1 chan
//
// Since we're adding a channel, we can't (easily) write an in-place processor,
// so we don't test that configuration.
//

class AddOneAndRemoveChannelProcessor : public BaseProcessor {
 public:
  AddOneAndRemoveChannelProcessor(const ConfigOptions& options,
                                  fidl::ServerEnd<fuchsia_audio_effects::Processor> server_end,
                                  async_dispatcher_t* dispatcher)
      : BaseProcessor(options, std::move(server_end), dispatcher) {
    FX_CHECK(options.input_format.channel_count == 2);
    FX_CHECK(options.output_format.channel_count == 1);
  }

  void Process(ProcessRequestView request, ProcessCompleter::Sync& completer) {
    float* input = input_data();
    float* output = output_data();
    auto num_frames = request->num_frames;
    for (; num_frames > 0; num_frames--, input += 2, output++) {
      output[0] = input[0] + 1;
    }
    completer.ReplySuccess(fidl::VectorView<fuchsia_audio_effects::wire::ProcessMetrics>());
  }
};

TEST_F(EffectsStageV2Test, AddOneAndRemoveChannelWithDifferentVmos) {
  auto processor_info = MakeProcessorWithDifferentVmos<AddOneAndRemoveChannelProcessor>({
      .input_format = DefaultFormatWithChannels(2),
      .output_format = DefaultFormatWithChannels(1),
  });
  TestAddOneWithSinglePacket(std::move(processor_info));
}

TEST_F(EffectsStageV2Test, AddOneAndRemoveChannelWithSameVmoDifferentRanges) {
  auto processor_info = MakeProcessorWithSameVmoDifferentRanges<AddOneAndRemoveChannelProcessor>({
      .input_format = DefaultFormatWithChannels(2),
      .output_format = DefaultFormatWithChannels(1),
  });
  TestAddOneWithSinglePacket(std::move(processor_info));
}

//
// AddOneWithSizeLimits
// Test limits on the size of an input buffer
//

template <uint64_t MaxFramesPerCall, uint64_t BlockSizeFrames>
class AddOneWithSizeLimitsProcessor : public BaseProcessor {
 public:
  AddOneWithSizeLimitsProcessor(const ConfigOptions& options,
                                fidl::ServerEnd<fuchsia_audio_effects::Processor> server_end,
                                async_dispatcher_t* dispatcher)
      : BaseProcessor(options, std::move(server_end), dispatcher) {
    FX_CHECK(options.input_format.channel_count == 1);
    FX_CHECK(options.output_format.channel_count == 1);
  }

  void Process(ProcessRequestView request, ProcessCompleter::Sync& completer) {
    auto num_frames = request->num_frames;

    if (MaxFramesPerCall > 0) {
      FX_CHECK(num_frames <= MaxFramesPerCall)
          << "expected at most " << MaxFramesPerCall << " frames, got " << num_frames;
    }
    if (BlockSizeFrames > 0) {
      FX_CHECK(num_frames % BlockSizeFrames == 0)
          << "expected multiple of " << BlockSizeFrames << " frames, got " << num_frames;
    }

    float* input = input_data();
    float* output = output_data();
    for (uint64_t k = 0; k < num_frames; k++) {
      output[k] = input[k] + 1;
    }
    completer.ReplySuccess(fidl::VectorView<fuchsia_audio_effects::wire::ProcessMetrics>());
  }
};

TEST_F(EffectsStageV2Test, AddOneWithSizeLimitsMaxSizeWithoutBlockSize) {
  // First ReadLock returns 31 frames.
  auto processor_info = MakeProcessorWithDifferentVmos<AddOneWithSizeLimitsProcessor<31, 0>>({
      .input_format = DefaultFormatWithChannels(1),
      .output_format = DefaultFormatWithChannels(1),
      .max_frames_per_call = 31,
      .block_size_frames = 0,
  });
  TestAddOneWithSinglePacket(std::move(processor_info), 480 /* = packet_frames */,
                             31 /* = read_lock_buffer_frames */);
}

TEST_F(EffectsStageV2Test, AddOneWithSizeLimitsBlockSizeWithoutMax) {
  // First ReadLock returns floor(kProcessingBufferMaxFrames/7)*7 = 1022 frames.
  auto processor_info = MakeProcessorWithDifferentVmos<AddOneWithSizeLimitsProcessor<0, 7>>({
      .input_format = DefaultFormatWithChannels(1),
      .output_format = DefaultFormatWithChannels(1),
      .max_frames_per_call = 0,
      .block_size_frames = 7,
  });
  TestAddOneWithSinglePacket(std::move(processor_info),
                             kProcessingBufferMaxFrames /* = packet_frames */,
                             1022 /* = read_lock_buffer_frames */);
}

TEST_F(EffectsStageV2Test, AddOneWithSizeLimitsBlockSizeEqualsMax) {
  // First ReadLock returns 8 frames.
  auto processor_info = MakeProcessorWithDifferentVmos<AddOneWithSizeLimitsProcessor<8, 8>>({
      .input_format = DefaultFormatWithChannels(1),
      .output_format = DefaultFormatWithChannels(1),
      .max_frames_per_call = 8,
      .block_size_frames = 8,
  });
  TestAddOneWithSinglePacket(std::move(processor_info), 480 /* = packet_frames */,
                             8 /* = read_lock_buffer_frames */);
}

TEST_F(EffectsStageV2Test, AddOneWithSizeLimitsBlockSizeLessThanMaxNotDivisible) {
  // First ReadLock returns 8*3 = 24 frames.
  auto processor_info = MakeProcessorWithDifferentVmos<AddOneWithSizeLimitsProcessor<31, 8>>({
      .input_format = DefaultFormatWithChannels(1),
      .output_format = DefaultFormatWithChannels(1),
      .max_frames_per_call = 31,
      .block_size_frames = 8,
  });
  TestAddOneWithSinglePacket(std::move(processor_info), 480 /* = packet_frames */,
                             24 /* = read_lock_buffer_frames */);
}

TEST_F(EffectsStageV2Test, AddOneWithSizeLimitsBlockSizeLessThanMaxDivisible) {
  // First ReadLock returns 32 frames.
  auto processor_info = MakeProcessorWithDifferentVmos<AddOneWithSizeLimitsProcessor<32, 8>>({
      .input_format = DefaultFormatWithChannels(1),
      .output_format = DefaultFormatWithChannels(1),
      .max_frames_per_call = 32,
      .block_size_frames = 8,
  });
  TestAddOneWithSinglePacket(std::move(processor_info), 480 /* = packet_frames */,
                             32 /* = read_lock_buffer_frames */);
}

//
// CheckOptionsProcessor
// Test that ProcessOptions is set correctly.
//

class CheckOptionsProcessor : public BaseProcessor {
 public:
  static constexpr float kExpectedAppliedGainDb = -25.0f;
  static constexpr uint32_t kExpectedUsageMask =
      StreamUsageMask({StreamUsage::WithRenderUsage(RenderUsage::MEDIA),
                       StreamUsage::WithRenderUsage(RenderUsage::INTERRUPTION)})
          .mask();

  CheckOptionsProcessor(const ConfigOptions& options,
                        fidl::ServerEnd<fuchsia_audio_effects::Processor> server_end,
                        async_dispatcher_t* dispatcher)
      : BaseProcessor(options, std::move(server_end), dispatcher) {}

  void Process(ProcessRequestView request, ProcessCompleter::Sync& completer) {
    ASSERT_EQ(request->options.total_applied_gain_db_per_input().count(), 1u);
    EXPECT_EQ(request->options.total_applied_gain_db_per_input()[0], kExpectedAppliedGainDb);
    ASSERT_EQ(request->options.usage_mask_per_input().count(), 1u);
    ASSERT_EQ(request->options.usage_mask_per_input()[0], kExpectedUsageMask);
    completer.ReplySuccess(fidl::VectorView<fuchsia_audio_effects::wire::ProcessMetrics>());
  }
};

TEST_F(EffectsStageV2Test, PassOptions) {
  constexpr auto kPacketFrames = 480;
  constexpr auto kPacketDuration = zx::msec(10);
  auto info = MakeProcessorWithDifferentVmos<CheckOptionsProcessor>({});

  // Enqueue one packet in the source packet queue.
  auto [packet_factory, stream, effects_stage] = MakeEffectsStage(std::move(info.config));
  stream->PushPacket(packet_factory->CreatePacket(1.0, kPacketDuration));

  // Ensure that ULTRASOUND is removed.
  const auto usage_mask =
      CheckOptionsProcessor::kExpectedUsageMask | (1 << static_cast<int>(RenderUsage::ULTRASOUND));

  // Set options.
  stream->set_gain_db(CheckOptionsProcessor::kExpectedAppliedGainDb);
  stream->set_usage_mask(StreamUsageMask::FromMask(usage_mask));

  // Call ReadLock. Validate it returns a buffer, which ensures we invoked the effects processor.
  auto buf = effects_stage->ReadLock(rlctx, Fixed(0), kPacketFrames);
  ASSERT_TRUE(buf);
}

//
// ReturnMetricsProcessor
// Test an effect that returns metrics.
//

class ReturnMetricsProcessor : public BaseProcessor {
 public:
  ReturnMetricsProcessor(const ConfigOptions& options,
                         fidl::ServerEnd<fuchsia_audio_effects::Processor> server_end,
                         async_dispatcher_t* dispatcher)
      : BaseProcessor(options, std::move(server_end), dispatcher) {}

  void Process(ProcessRequestView request, ProcessCompleter::Sync& completer) {
    completer.ReplySuccess(
        fidl::VectorView<fuchsia_audio_effects::wire::ProcessMetrics>::FromExternal(*metrics_));
  }

  void set_metrics(std::vector<fuchsia_audio_effects::wire::ProcessMetrics>* m) { metrics_ = m; }

 private:
  std::vector<fuchsia_audio_effects::wire::ProcessMetrics>* metrics_ = nullptr;
};

TEST_F(EffectsStageV2Test, Metrics) {
  std::vector<fuchsia_audio_effects::wire::ProcessMetrics> expected_metrics(3);
  expected_metrics[0].Allocate(arena());
  expected_metrics[0].set_name(arena(), "EffectsStageV2::Process");
  expected_metrics[1].Allocate(arena());
  expected_metrics[1].set_name(arena(), "stage1");
  expected_metrics[1].set_wall_time(arena(), 100);
  expected_metrics[1].set_cpu_time(arena(), 101);
  expected_metrics[1].set_queue_time(arena(), 102);
  expected_metrics[2].Allocate(arena());
  expected_metrics[2].set_name(arena(), "stage2");
  expected_metrics[2].set_wall_time(arena(), 200);
  expected_metrics[2].set_cpu_time(arena(), 201);
  expected_metrics[2].set_queue_time(arena(), 201);

  constexpr auto kPacketFrames = 480;
  constexpr auto kPacketDuration = zx::msec(10);
  auto info = MakeProcessorWithDifferentVmos<ReturnMetricsProcessor>({});
  info.processor.set_metrics(&expected_metrics);

  // Enqueue one packet in the source packet queue.
  auto [packet_factory, stream, effects_stage] = MakeEffectsStage(std::move(info.config));
  stream->PushPacket(packet_factory->CreatePacket(1.0, kPacketDuration));

  // Call ReadLock and validate the metrics.
  ReadableStream::ReadLockContext ctx;
  auto buf = effects_stage->ReadLock(ctx, Fixed(0), kPacketFrames);
  ASSERT_TRUE(buf);

  EXPECT_EQ(ctx.per_stage_metrics().size(), expected_metrics.size());
  for (size_t k = 0; k < expected_metrics.size(); k++) {
    if (k >= ctx.per_stage_metrics().size()) {
      break;
    }
    SCOPED_TRACE(fxl::StringPrintf("metrics[%lu]", k));
    auto& metrics = ctx.per_stage_metrics()[k];
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

//
// Test that latency affects the stream timeline
//

TEST_F(EffectsStageV2Test, LatencyAffectStreamTimelineAndLeadTime) {
  auto config = DefaultGoodProcessorConfig(arena());
  config.outputs()[0].set_latency_frames(arena(), 13);

  // Create a source packet queue.
  auto [packet_factory, stream, effects_stage] = MakeEffectsStage(std::move(config));

  // Setup the timeline function so that time 0 aligns to frame 0 with a rate corresponding to the
  // streams format.
  stream->timeline_function()->Update(TimelineFunction(TimelineRate(
      Fixed(k48k2ChanFloatFormat.frames_per_second()).raw_value(), zx::sec(1).to_nsecs())));

  // Since our effect introduces 13 frames of latency, the incoming source frame at time 0 can only
  // emerge from the effect in output frame 13.
  // Conversely, output frame 0 was produced based on the source frame at time -13.
  auto ref_clock_to_output_frac_frame =
      effects_stage->ref_time_to_frac_presentation_frame().timeline_function;
  EXPECT_EQ(Fixed::FromRaw(ref_clock_to_output_frac_frame.Apply(0)), Fixed(13));

  // Similarly, at the time we produce output frame 0, we had to draw upon the source frame from
  // time -13. Use a fuzzy compare to allow for slight rounding errors.
  int64_t frame_13_time = (zx::sec(-13).to_nsecs()) / k48k2ChanFloatFormat.frames_per_second();
  auto frame_13_frac_frames =
      Fixed::FromRaw(ref_clock_to_output_frac_frame.Apply(frame_13_time)).Absolute();
  EXPECT_LE(frame_13_frac_frames.raw_value(), 1);

  // Check our initial lead time is only the effect latency.
  auto effect_lead_time =
      zx::duration(zx::sec(13).to_nsecs() / k48k2ChanFloatFormat.frames_per_second());
  EXPECT_EQ(effect_lead_time, effects_stage->GetPresentationDelay());

  // Check that setting an external min lead time includes our internal lead time.
  const auto external_lead_time = zx::usec(100);
  effects_stage->SetPresentationDelay(external_lead_time);
  EXPECT_EQ(effect_lead_time + external_lead_time, effects_stage->GetPresentationDelay());
}

//
// Error cases in EffectsStageV2::Create
//

TEST_F(EffectsStageV2Test, CreateSuccess) {
  auto config = DefaultGoodProcessorConfig(arena());
  auto stream = MakePacketQueue(k48k1ChanFloatFormat);
  auto result = EffectsStageV2::Create(std::move(config), stream);
  EXPECT_TRUE(result.is_ok()) << "failed with status: " << result.error();
}

TEST_F(EffectsStageV2Test, CreateFailsMissingProcessorHandle) {
  auto config = DefaultGoodProcessorConfig(arena());
  auto stream = MakePacketQueue(k48k1ChanFloatFormat);

  config.clear_processor();
  auto result = EffectsStageV2::Create(std::move(config), stream);
  EXPECT_FALSE(result.is_ok());
}

TEST_F(EffectsStageV2Test, CreateFailsNoInputs) {
  auto config = DefaultGoodProcessorConfig(arena());
  auto stream = MakePacketQueue(k48k1ChanFloatFormat);

  config.clear_inputs();
  auto result = EffectsStageV2::Create(std::move(config), stream);
  EXPECT_FALSE(result.is_ok());
}

TEST_F(EffectsStageV2Test, CreateFailsNoOutputs) {
  auto config = DefaultGoodProcessorConfig(arena());
  auto stream = MakePacketQueue(k48k1ChanFloatFormat);

  config.clear_outputs();
  auto result = EffectsStageV2::Create(std::move(config), stream);
  EXPECT_FALSE(result.is_ok());
}

TEST_F(EffectsStageV2Test, CreateFailsTooManyInputs) {
  auto config = DefaultGoodProcessorConfig(arena());
  auto stream = MakePacketQueue(k48k1ChanFloatFormat);

  std::vector<fuchsia_audio_effects::wire::InputConfiguration> inputs(2);
  inputs[0] = config.inputs()[0];
  config.inputs() =
      fidl::VectorView<fuchsia_audio_effects::wire::InputConfiguration>::FromExternal(inputs);

  auto result = EffectsStageV2::Create(std::move(config), stream);
  EXPECT_FALSE(result.is_ok());
}

TEST_F(EffectsStageV2Test, CreateFailsTooManyOutputs) {
  auto config = DefaultGoodProcessorConfig(arena());
  auto stream = MakePacketQueue(k48k1ChanFloatFormat);

  std::vector<fuchsia_audio_effects::wire::OutputConfiguration> outputs(2);
  outputs[0] = config.outputs()[0];
  config.outputs() =
      fidl::VectorView<fuchsia_audio_effects::wire::OutputConfiguration>::FromExternal(outputs);

  auto result = EffectsStageV2::Create(std::move(config), stream);
  EXPECT_FALSE(result.is_ok());
}

TEST_F(EffectsStageV2Test, CreateFailsInputMissingFormat) {
  auto config = DefaultGoodProcessorConfig(arena());
  auto stream = MakePacketQueue(k48k1ChanFloatFormat);

  config.inputs()[0].clear_format();
  auto result = EffectsStageV2::Create(std::move(config), stream);
  EXPECT_FALSE(result.is_ok());
}

TEST_F(EffectsStageV2Test, CreateFailsOutputMissingFormat) {
  auto config = DefaultGoodProcessorConfig(arena());
  auto stream = MakePacketQueue(k48k1ChanFloatFormat);

  config.outputs()[0].clear_format();
  auto result = EffectsStageV2::Create(std::move(config), stream);
  EXPECT_FALSE(result.is_ok());
}

TEST_F(EffectsStageV2Test, CreateFailsInputFormatNotFloat) {
  auto config = DefaultGoodProcessorConfig(arena());
  auto stream = MakePacketQueue(k48k1ChanFloatFormat);

  config.inputs()[0].format().sample_format = ASF::kUnsigned8;
  auto result = EffectsStageV2::Create(std::move(config), stream);
  EXPECT_FALSE(result.is_ok());
}

TEST_F(EffectsStageV2Test, CreateFailsOutputFormatNotFloat) {
  auto config = DefaultGoodProcessorConfig(arena());
  auto stream = MakePacketQueue(k48k1ChanFloatFormat);

  config.outputs()[0].format().sample_format = ASF::kUnsigned8;
  auto result = EffectsStageV2::Create(std::move(config), stream);
  EXPECT_FALSE(result.is_ok());
}

TEST_F(EffectsStageV2Test, CreateFailsInputOutputFpsMismatch) {
  auto config = DefaultGoodProcessorConfig(arena());
  auto stream = MakePacketQueue(k48k1ChanFloatFormat);

  config.inputs()[0].format().frames_per_second = 48000;
  config.outputs()[0].format().frames_per_second = 44100;
  auto result = EffectsStageV2::Create(std::move(config), stream);
  EXPECT_FALSE(result.is_ok());
}

TEST_F(EffectsStageV2Test, CreateFailsInputMissingBuffer) {
  auto config = DefaultGoodProcessorConfig(arena());
  auto stream = MakePacketQueue(k48k1ChanFloatFormat);

  config.inputs()[0].clear_buffer();
  auto result = EffectsStageV2::Create(std::move(config), stream);
  EXPECT_FALSE(result.is_ok());
}

TEST_F(EffectsStageV2Test, CreateFailsOutputMissingBuffer) {
  auto config = DefaultGoodProcessorConfig(arena());
  auto stream = MakePacketQueue(k48k1ChanFloatFormat);

  config.outputs()[0].clear_buffer();
  auto result = EffectsStageV2::Create(std::move(config), stream);
  EXPECT_FALSE(result.is_ok());
}

TEST_F(EffectsStageV2Test, CreateFailsInputBufferEmpty) {
  auto config = DefaultGoodProcessorConfig(arena());
  auto stream = MakePacketQueue(k48k1ChanFloatFormat);

  config.inputs()[0].buffer().size = 0;
  auto result = EffectsStageV2::Create(std::move(config), stream);
  EXPECT_FALSE(result.is_ok());
}

TEST_F(EffectsStageV2Test, CreateFailsOutputBufferEmpty) {
  auto config = DefaultGoodProcessorConfig(arena());
  auto stream = MakePacketQueue(k48k1ChanFloatFormat);

  config.outputs()[0].buffer().size = 0;
  auto result = EffectsStageV2::Create(std::move(config), stream);
  EXPECT_FALSE(result.is_ok());
}

TEST_F(EffectsStageV2Test, CreateFailsInputBufferVmoInvalid) {
  auto config = DefaultGoodProcessorConfig(arena());
  auto stream = MakePacketQueue(k48k1ChanFloatFormat);

  config.inputs()[0].buffer().vmo = zx::vmo();
  auto result = EffectsStageV2::Create(std::move(config), stream);
  EXPECT_FALSE(result.is_ok());
}

TEST_F(EffectsStageV2Test, CreateFailsOutputBufferVmoInvalid) {
  auto config = DefaultGoodProcessorConfig(arena());
  auto stream = MakePacketQueue(k48k1ChanFloatFormat);

  config.outputs()[0].buffer().vmo = zx::vmo();
  auto result = EffectsStageV2::Create(std::move(config), stream);
  EXPECT_FALSE(result.is_ok());
}

TEST_F(EffectsStageV2Test, CreateFailsInputBufferVmoMustBeMappable) {
  auto config = DefaultGoodProcessorConfig(arena());
  auto stream = MakePacketQueue(k48k1ChanFloatFormat);

  auto& buffer = config.inputs()[0].buffer();
  ASSERT_EQ(ZX_OK, buffer.vmo.replace(ZX_RIGHT_WRITE, &buffer.vmo));
  auto result = EffectsStageV2::Create(std::move(config), stream);
  EXPECT_FALSE(result.is_ok());
}

TEST_F(EffectsStageV2Test, CreateFailsOutputBufferVmoMustBeMappable) {
  auto config = DefaultGoodProcessorConfig(arena());
  auto stream = MakePacketQueue(k48k1ChanFloatFormat);

  auto& buffer = config.outputs()[0].buffer();
  ASSERT_EQ(ZX_OK, buffer.vmo.replace(ZX_RIGHT_READ, &buffer.vmo));
  auto result = EffectsStageV2::Create(std::move(config), stream);
  EXPECT_FALSE(result.is_ok());
}

TEST_F(EffectsStageV2Test, CreateFailsInputBufferVmoMustBeWritable) {
  auto config = DefaultGoodProcessorConfig(arena());
  auto stream = MakePacketQueue(k48k1ChanFloatFormat);

  auto& buffer = config.inputs()[0].buffer();
  ASSERT_EQ(ZX_OK, buffer.vmo.replace(ZX_RIGHT_MAP, &buffer.vmo));
  auto result = EffectsStageV2::Create(std::move(config), stream);
  EXPECT_FALSE(result.is_ok());
}

TEST_F(EffectsStageV2Test, CreateFailsOutputBufferVmoMustBeReadable) {
  auto config = DefaultGoodProcessorConfig(arena());
  auto stream = MakePacketQueue(k48k1ChanFloatFormat);

  auto& buffer = config.outputs()[0].buffer();
  ASSERT_EQ(ZX_OK, buffer.vmo.replace(ZX_RIGHT_MAP, &buffer.vmo));
  auto result = EffectsStageV2::Create(std::move(config), stream);
  EXPECT_FALSE(result.is_ok());
}

TEST_F(EffectsStageV2Test, CreateFailsInputBufferVmoTooSmall) {
  auto config = DefaultGoodProcessorConfig(arena());
  auto stream = MakePacketQueue(k48k1ChanFloatFormat);

  auto& buffer = config.inputs()[0].buffer();
  uint64_t vmo_size;
  ASSERT_EQ(ZX_OK, buffer.vmo.get_size(&vmo_size));
  buffer.size = vmo_size + 1;

  auto result = EffectsStageV2::Create(std::move(config), stream);
  EXPECT_FALSE(result.is_ok());  // too large by 1 byte
}

TEST_F(EffectsStageV2Test, CreateFailsOutputBufferVmoTooSmall) {
  auto config = DefaultGoodProcessorConfig(arena());
  auto stream = MakePacketQueue(k48k1ChanFloatFormat);

  auto& buffer = config.outputs()[0].buffer();
  uint64_t vmo_size;
  ASSERT_EQ(ZX_OK, buffer.vmo.get_size(&vmo_size));
  buffer.size = vmo_size + 1;  // too large by 1 byte

  auto result = EffectsStageV2::Create(std::move(config), stream);
  EXPECT_FALSE(result.is_ok());
}

TEST_F(EffectsStageV2Test, CreateFailsInputBufferOffsetTooLarge) {
  auto config = DefaultGoodProcessorConfig(arena());
  auto stream = MakePacketQueue(k48k1ChanFloatFormat);

  auto& buffer = config.inputs()[0].buffer();
  uint64_t vmo_size;
  ASSERT_EQ(ZX_OK, buffer.vmo.get_size(&vmo_size));
  buffer.offset = vmo_size - buffer.size + 1;  // too large by 1 byte

  auto result = EffectsStageV2::Create(std::move(config), stream);
  EXPECT_FALSE(result.is_ok());
}

TEST_F(EffectsStageV2Test, CreateFailsOutputBufferOffsetTooLarge) {
  auto config = DefaultGoodProcessorConfig(arena());
  auto stream = MakePacketQueue(k48k1ChanFloatFormat);

  auto& buffer = config.outputs()[0].buffer();
  uint64_t vmo_size;
  ASSERT_EQ(ZX_OK, buffer.vmo.get_size(&vmo_size));
  buffer.offset = vmo_size - buffer.size + 1;  // too large by 1 byte

  auto result = EffectsStageV2::Create(std::move(config), stream);
  EXPECT_FALSE(result.is_ok());
}

TEST_F(EffectsStageV2Test, CreateFailsInputBufferTooSmall) {
  auto config = DefaultGoodProcessorConfig(arena());
  auto stream = MakePacketQueue(k48k1ChanFloatFormat);

  config.set_max_frames_per_call(arena(), 10);
  config.inputs()[0].buffer().size = 9 * sizeof(float);

  auto result = EffectsStageV2::Create(std::move(config), stream);
  EXPECT_FALSE(result.is_ok());
}

TEST_F(EffectsStageV2Test, CreateFailsOutputBufferTooSmall) {
  auto config = DefaultGoodProcessorConfig(arena());
  auto stream = MakePacketQueue(k48k1ChanFloatFormat);

  config.set_max_frames_per_call(arena(), 10);
  config.outputs()[0].buffer().size = 9 * sizeof(float);

  auto result = EffectsStageV2::Create(std::move(config), stream);
  EXPECT_FALSE(result.is_ok());
}

TEST_F(EffectsStageV2Test, CreateFailsOutputBufferPartiallyOverlapsInputBuffer) {
  auto config = DefaultGoodProcessorConfig(arena());
  auto stream = MakePacketQueue(k48k1ChanFloatFormat);

  auto& input_buffer = config.inputs()[0].buffer();
  auto& output_buffer = config.outputs()[0].buffer();
  input_buffer.vmo = CreateVmoOrDie(1024);
  input_buffer.offset = 0;
  input_buffer.size = 256;
  output_buffer.vmo = DupVmoOrDie(input_buffer.vmo, ZX_RIGHT_SAME_RIGHTS);
  output_buffer.offset = 255;
  output_buffer.size = 256;

  auto result = EffectsStageV2::Create(std::move(config), stream);
  EXPECT_FALSE(result.is_ok());
}

TEST_F(EffectsStageV2Test, CreateFailsBlockSizeTooBig) {
  auto config = DefaultGoodProcessorConfig(arena());
  auto stream = MakePacketQueue(k48k1ChanFloatFormat);

  auto max_frames = config.inputs()[0].buffer().size / sizeof(float);
  config.set_block_size_frames(arena(), max_frames + 1);
  auto result = EffectsStageV2::Create(std::move(config), stream);
  EXPECT_FALSE(result.is_ok());
}

TEST_F(EffectsStageV2Test, CreateFailsMaxFramesPerCallTooBig) {
  auto config = DefaultGoodProcessorConfig(arena());
  auto stream = MakePacketQueue(k48k1ChanFloatFormat);

  auto max_frames = config.inputs()[0].buffer().size / sizeof(float);
  config.set_max_frames_per_call(arena(), max_frames + 1);
  auto result = EffectsStageV2::Create(std::move(config), stream);
  EXPECT_FALSE(result.is_ok());
}

TEST_F(EffectsStageV2Test, CreateFailsInputSampleFormatDoesNotMatchSource) {
  auto config = DefaultGoodProcessorConfig(arena());
  auto stream = MakePacketQueue(k48k1ChanFloatFormat);

  config.inputs()[0].format().sample_format = ASF::kUnsigned8;
  auto result = EffectsStageV2::Create(std::move(config), stream);
  EXPECT_FALSE(result.is_ok());
}

TEST_F(EffectsStageV2Test, CreateFailsInputChannelCountDoesNotMatchSource) {
  auto config = DefaultGoodProcessorConfig(arena());
  auto stream = MakePacketQueue(k48k1ChanFloatFormat);

  config.inputs()[0].format().channel_count = 2;
  auto result = EffectsStageV2::Create(std::move(config), stream);
  EXPECT_FALSE(result.is_ok());
}

TEST_F(EffectsStageV2Test, CreateFailsInputFpsDoesNotMatchSource) {
  auto config = DefaultGoodProcessorConfig(arena());
  auto stream = MakePacketQueue(k48k1ChanFloatFormat);

  config.inputs()[0].format().frames_per_second = 44100;
  auto result = EffectsStageV2::Create(std::move(config), stream);
  EXPECT_FALSE(result.is_ok());
}

//
// FidlBuffers
//

TEST(EffectsStageV2BuffersTest, CreateSeparate) {
  ConfigOptions options;
  CreateSeparateVmos(options, 128, 256);

  auto buffers = EffectsStageV2::FidlBuffers::Create(options.input_buffer, options.output_buffer);
  ASSERT_NE(buffers.input, nullptr);
  ASSERT_NE(buffers.output, nullptr);
  EXPECT_EQ(buffers.input_size, options.input_buffer.size);
  EXPECT_EQ(buffers.output_size, options.output_buffer.size);

  // Must not overlap.
  auto input_start = reinterpret_cast<char*>(buffers.input);
  auto output_start = reinterpret_cast<char*>(buffers.output);
  EXPECT_TRUE(input_start + buffers.input_size <= output_start ||
              output_start + buffers.output_size <= input_start)
      << "input_start=0x" << buffers.input << ", input_size=" << buffers.input_size
      << "output_start=0x" << buffers.output << ", output_size=" << buffers.output_size;

  // Must be readable and writable.
  // These loops should crash if not readable nor writable.
  for (char* p = input_start; p < input_start + buffers.input_size; p++) {
    (*p) += 1;
  }
  for (char* p = output_start; p < output_start + buffers.output_size; p++) {
    (*p) += 1;
  }
}

TEST(EffectsStageV2BuffersTest, CreateSharedOverlappingZeroOffsets) {
  ConfigOptions options;
  CreateSharedVmo(options,
                  10,      // vmo_size_bytes
                  0, 10,   // input_offset_bytes, input_size_bytes
                  0, 10);  // output_offset_bytes output_size_bytes

  auto buffers = EffectsStageV2::FidlBuffers::Create(options.input_buffer, options.output_buffer);
  ASSERT_NE(buffers.input, nullptr);
  ASSERT_NE(buffers.output, nullptr);
  EXPECT_EQ(buffers.input_size, options.input_buffer.size);
  EXPECT_EQ(buffers.output_size, options.output_buffer.size);

  // Must be overlapping.
  auto input_start = reinterpret_cast<char*>(buffers.input);
  auto output_start = reinterpret_cast<char*>(buffers.output);
  EXPECT_EQ(input_start, output_start)
      << "input_start=0x" << buffers.input << ", input_size=" << buffers.input_size
      << "output_start=0x" << buffers.output << ", output_size=" << buffers.output_size;

  // Must be readable and writable.
  // This loop should crash if not readable nor writable.
  for (char* p = input_start; p < input_start + buffers.input_size; p++) {
    (*p) += 1;
  }
}

TEST(EffectsStageV2BuffersTest, CreateSharedOverlappingNonzeroOffsets) {
  // Offsets must be a multiple of the page size.
  const auto page_size = zx_system_get_page_size();

  ConfigOptions options;
  CreateSharedVmo(options,
                  page_size * 2,          // vmo_size_bytes
                  page_size, page_size,   // input_offset_bytes, input_size_bytes
                  page_size, page_size);  // output_offset_bytes output_size_bytes

  auto buffers = EffectsStageV2::FidlBuffers::Create(options.input_buffer, options.output_buffer);
  ASSERT_NE(buffers.input, nullptr);
  ASSERT_NE(buffers.output, nullptr);
  EXPECT_EQ(buffers.input_size, options.input_buffer.size);
  EXPECT_EQ(buffers.output_size, options.output_buffer.size);

  // Must be overlapping.
  auto input_start = reinterpret_cast<char*>(buffers.input);
  auto output_start = reinterpret_cast<char*>(buffers.output);
  EXPECT_EQ(input_start, output_start)
      << "input_start=0x" << buffers.input << ", input_size=" << buffers.input_size
      << "output_start=0x" << buffers.output << ", output_size=" << buffers.output_size;

  // Must be readable and writable.
  // This loop should crash if not readable nor writable.
  for (char* p = input_start; p < input_start + buffers.input_size; p++) {
    (*p) += 1;
  }
}

TEST(EffectsStageV2BuffersTest, CreateSharedNonOverlapping) {
  // Offsets must be a multiple of the page size.
  const auto page_size = zx_system_get_page_size();

  ConfigOptions options;
  CreateSharedVmo(options,
                  page_size * 2,          // vmo_size_bytes
                  0, page_size,           // input_offset_bytes, input_size_bytes
                  page_size, page_size);  // output_offset_bytes output_size_bytes

  auto buffers = EffectsStageV2::FidlBuffers::Create(options.input_buffer, options.output_buffer);
  ASSERT_NE(buffers.input, nullptr);
  ASSERT_NE(buffers.output, nullptr);
  EXPECT_EQ(buffers.input_size, options.input_buffer.size);
  EXPECT_EQ(buffers.output_size, options.output_buffer.size);

  // Must be adjacent.
  auto input_start = reinterpret_cast<char*>(buffers.input);
  auto output_start = reinterpret_cast<char*>(buffers.output);
  EXPECT_EQ(input_start + buffers.input_size, output_start)
      << "input_start=0x" << buffers.input << ", input_size=" << buffers.input_size
      << "output_start=0x" << buffers.output << ", output_size=" << buffers.output_size;

  // Must be readable and writable.
  // These loops should crash if not readable nor writable.
  for (char* p = input_start; p < input_start + buffers.input_size; p++) {
    (*p) += 1;
  }
  for (char* p = output_start; p < output_start + buffers.output_size; p++) {
    (*p) += 1;
  }
}

}  // namespace
}  // namespace media::audio
