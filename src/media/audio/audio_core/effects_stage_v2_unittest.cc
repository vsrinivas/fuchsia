// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/effects_stage_v2.h"

#include <fuchsia/audio/effects/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fzl/vmo-mapper.h>

#include <gmock/gmock.h>

#include "src/lib/fxl/strings/string_printf.h"
#include "src/media/audio/audio_core/packet_queue.h"
#include "src/media/audio/audio_core/testing/fake_packet_queue.h"
#include "src/media/audio/audio_core/testing/packet_factory.h"
#include "src/media/audio/audio_core/testing/threading_model_fixture.h"
#include "src/media/audio/lib/clock/audio_clock.h"
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

template <typename T, size_t N>
std::array<T, N>& as_array(void* ptr, size_t offset = 0) {
  return reinterpret_cast<std::array<T, N>&>(static_cast<T*>(ptr)[offset]);
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
  zx::channel local;
  zx::channel remote;
  if (auto status = zx::channel::create(0, &local, &remote); status != ZX_OK) {
    FX_PLOGS(FATAL, status) << "failed to construct a zx::channel";
  }

  config.set_processor(fidl::ClientEnd<fuchsia_audio_effects::Processor>{std::move(local)});
  return fidl::ServerEnd<fuchsia_audio_effects::Processor>{std::move(remote)};
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
                                    if (info.reason() != fidl::Reason::kClose &&
                                        info.reason() != fidl::Reason::kPeerClosed &&
                                        info.reason() != fidl::Reason::kUnbind) {
                                      FX_PLOGS(ERROR, info.status())
                                          << "Client disconnected unexpectedly: ";
                                    }
                                  })),
        buffers_(EffectsStageV2::Buffers::Create(options.input_buffer, options.output_buffer)) {}

  float* input_data() const { return reinterpret_cast<float*>(buffers_.input); }
  float* output_data() const { return reinterpret_cast<float*>(buffers_.output); }

 private:
  fidl::ServerBindingRef<fuchsia_audio_effects::Processor> binding_;
  EffectsStageV2::Buffers buffers_;
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

  std::shared_ptr<testing::FakePacketQueue> MakePacketQueue(
      const Format& format, std::vector<fbl::RefPtr<Packet>> packets) {
    auto timeline_function = fbl::MakeRefCounted<VersionedTimelineFunction>(TimelineFunction(
        TimelineRate(Fixed(format.frames_per_second()).raw_value(), zx::sec(1).to_nsecs())));
    return std::make_shared<testing::FakePacketQueue>(
        std::move(packets), format, timeline_function,
        context().clock_factory()->CreateClientFixed(clock::AdjustableCloneOfMonotonic()));
  }

  Arena& arena() { return arena_; }

  static constexpr auto kPacketFrames = 480;
  static constexpr auto kPacketDuration = zx::msec(10);

  // ProcessorT     :: a BaseProcessor that implements the effect
  // InputChannels  :: number of channels in the input (source) stream
  // OutputChannels :: number of channels in the output (destination) stream
  // ReadLockFrames :: how many frames should be returned by call to ReadLock(0, kPacketFrames)

  template <class ProcessorT, int64_t InputChannels, int64_t OutputChannels,
            int64_t ReadLockFrames = kPacketFrames>
  void TestAddOne(const Format& source_format, ConfigOptions options);

  template <class ProcessorT, int64_t InputChannels, int64_t OutputChannels,
            int64_t ReadLockFrames = kPacketFrames>
  void TestAddOneWithDifferentVmos(ConfigOptions base_options);

  template <class ProcessorT, int64_t InputChannels, int64_t OutputChannels,
            int64_t ReadLockFrames = kPacketFrames>
  void TestAddOneWithSameRange(ConfigOptions base_options);

  template <class ProcessorT, int64_t InputChannels, int64_t OutputChannels,
            int64_t ReadLockFrames = kPacketFrames>
  void TestAddOneWithSameVmoDifferentRanges(ConfigOptions base_options);

 private:
  Arena arena_;
  async::Loop fidl_loop_{&kAsyncLoopConfigNoAttachToCurrentThread};
};

// Generic test for a processor that adds one to each input sample.
template <class ProcessorT, int64_t InputChannels, int64_t OutputChannels, int64_t ReadLockFrames>
void EffectsStageV2Test::TestAddOne(const Format& source_format, ConfigOptions options) {
  auto config = MakeProcessorConfig(arena(), DupConfigOptions(options));
  auto server_end = AttachProcessorChannel(config);
  ProcessorT processor(options, std::move(server_end), fidl_dispatcher());

  // Enqueue 10ms of frames in the source packet queue.
  auto packet_factory = std::make_unique<testing::PacketFactory>(dispatcher(), source_format,
                                                                 zx_system_get_page_size());
  auto stream =
      MakePacketQueue(source_format, {packet_factory->CreatePacket(1.0, kPacketDuration)});
  auto effects_stage = EffectsStageV2::Create(std::move(config), stream).take_value();

  {
    // Read the first packet. Since our effect adds 1.0 to each sample, and we populated the
    // packet with 1.0 samples, we expect to see only 2.0 samples in the result.
    auto buf = effects_stage->ReadLock(rlctx, Fixed(0), kPacketFrames);
    ASSERT_TRUE(buf);
    ASSERT_EQ(0, buf->start().Floor());
    ASSERT_EQ(ReadLockFrames, buf->length().Floor());

    auto& arr = as_array<float, ReadLockFrames * OutputChannels>(buf->payload());
    EXPECT_THAT(arr, Each(FloatEq(2.0f)));

    // If the update was in-place, the input should have been overwritten.
    // Otherwise it should be unchanged.
    if (options.in_place) {
      auto& arr = as_array<float, ReadLockFrames * InputChannels>(processor.input_data());
      EXPECT_THAT(arr, Each(FloatEq(2.0f)));
    } else {
      auto& arr = as_array<float, ReadLockFrames * InputChannels>(processor.input_data());
      EXPECT_THAT(arr, Each(FloatEq(1.0f)));
    }
  }

  {
    // TODO(fxbug.dev/50669): This will be unnecessary after we update ReadLock implementations
    // to never return an out-of-bounds packet.
    stream->Trim(Fixed(kPacketFrames));
    // Read the next packet. This should be null, because there are no more packets.
    auto buf = effects_stage->ReadLock(rlctx, Fixed(kPacketFrames), kPacketFrames);
    ASSERT_FALSE(buf);
  }
}

// Calls TestAddOne with different VMOs for the input and output.
template <class ProcessorT, int64_t InputChannels, int64_t OutputChannels, int64_t ReadLockFrames>
void EffectsStageV2Test::TestAddOneWithDifferentVmos(ConfigOptions base_options) {
  constexpr auto kInputPacketSamples = kPacketFrames * InputChannels;
  constexpr auto kOutputPacketSamples = kPacketFrames * OutputChannels;
  constexpr auto kInputPacketBytes = kInputPacketSamples * sizeof(float);
  constexpr auto kOutputPacketBytes = kOutputPacketSamples * sizeof(float);

  const Format kSourceFormat =
      Format::Create(fuchsia::media::AudioStreamType{
                         .sample_format = fuchsia::media::AudioSampleFormat::FLOAT,
                         .channels = InputChannels,
                         .frames_per_second = 48000,
                     })
          .take_value();

  base_options.input_format.channel_count = InputChannels;
  base_options.output_format.channel_count = OutputChannels;
  CreateSeparateVmos(base_options, kInputPacketBytes, kOutputPacketBytes);

  TestAddOne<ProcessorT, InputChannels, OutputChannels, ReadLockFrames>(kSourceFormat,
                                                                        std::move(base_options));
}

// Calls TestAddOne with the same fuchsia.mem.Range for the input and output.
// This is an in-place update.
template <class ProcessorT, int64_t InputChannels, int64_t OutputChannels, int64_t ReadLockFrames>
void EffectsStageV2Test::TestAddOneWithSameRange(ConfigOptions base_options) {
  constexpr auto kPacketSamples = kPacketFrames * InputChannels;
  constexpr auto kPacketBytes = kPacketSamples * sizeof(float);

  const Format kSourceFormat =
      Format::Create(fuchsia::media::AudioStreamType{
                         .sample_format = fuchsia::media::AudioSampleFormat::FLOAT,
                         .channels = InputChannels,
                         .frames_per_second = 48000,
                     })
          .take_value();

  base_options.input_format.channel_count = InputChannels;
  base_options.output_format.channel_count = OutputChannels;
  CreateSharedVmo(base_options, kPacketBytes,  // VMO size
                  0, kPacketBytes,             // input buffer offset & size
                  0, kPacketBytes);            // output buffer offset & size

  TestAddOne<ProcessorT, InputChannels, OutputChannels, ReadLockFrames>(kSourceFormat,
                                                                        std::move(base_options));
}

// Calls TestAddOne with the input and output referencing non-overlapping ranges of the same VMO.
template <class ProcessorT, int64_t InputChannels, int64_t OutputChannels, int64_t ReadLockFrames>
void EffectsStageV2Test::TestAddOneWithSameVmoDifferentRanges(ConfigOptions base_options) {
  constexpr auto kInputPacketSamples = kPacketFrames * InputChannels;
  constexpr auto kOutputPacketSamples = kPacketFrames * OutputChannels;

  const Format kSourceFormat =
      Format::Create(fuchsia::media::AudioStreamType{
                         .sample_format = fuchsia::media::AudioSampleFormat::FLOAT,
                         .channels = InputChannels,
                         .frames_per_second = 48000,
                     })
          .take_value();

  // To map input and output separately, the offset must be page-aligned.
  // We assume one page is suffient to hold one packet.
  const auto page_size = zx_system_get_page_size();
  constexpr auto kInputPacketBytes = kInputPacketSamples * sizeof(float);
  constexpr auto kOutputPacketBytes = kOutputPacketSamples * sizeof(float);
  FX_CHECK(kInputPacketBytes <= page_size);
  FX_CHECK(kOutputPacketBytes <= page_size);

  base_options.input_format.channel_count = InputChannels;
  base_options.output_format.channel_count = OutputChannels;
  CreateSharedVmo(base_options, page_size * 2,     // VMO size
                  0, kInputPacketBytes,            // input buffer offset & size
                  page_size, kOutputPacketBytes);  // output buffer offset & size

  TestAddOne<ProcessorT, InputChannels, OutputChannels, ReadLockFrames>(kSourceFormat,
                                                                        std::move(base_options));
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
  TestAddOneWithDifferentVmos<AddOneProcessor, 1, 1>(ConfigOptions{});
}

TEST_F(EffectsStageV2Test, AddOneWithTwoChanDifferentVmos) {
  TestAddOneWithDifferentVmos<AddOneProcessor, 2, 2>(ConfigOptions{});
}

TEST_F(EffectsStageV2Test, AddOneWithOneChanSameRange) {
  TestAddOneWithSameRange<AddOneProcessor, 1, 1>(ConfigOptions{});
}

TEST_F(EffectsStageV2Test, AddOneWithOneChanSameVmoDifferentRanges) {
  TestAddOneWithSameVmoDifferentRanges<AddOneProcessor, 1, 1>(ConfigOptions{});
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
  TestAddOneWithDifferentVmos<AddOneAndDupChannelProcessor, 1, 2>(ConfigOptions{});
}

TEST_F(EffectsStageV2Test, AddOneAndDupChannelWithSameVmoDifferentRanges) {
  TestAddOneWithSameVmoDifferentRanges<AddOneAndDupChannelProcessor, 1, 2>(ConfigOptions{});
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
  TestAddOneWithDifferentVmos<AddOneAndRemoveChannelProcessor, 2, 1>(ConfigOptions{});
}

TEST_F(EffectsStageV2Test, AddOneAndRemoveChannelWithSameVmoDifferentRanges) {
  TestAddOneWithSameVmoDifferentRanges<AddOneAndRemoveChannelProcessor, 2, 1>(ConfigOptions{});
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
  // ReadLock returns 31 frames.
  TestAddOneWithDifferentVmos<AddOneWithSizeLimitsProcessor<31, 0>, 1, 1, 31>(ConfigOptions{
      .max_frames_per_call = 31,
      .block_size_frames = 0,
  });
}

TEST_F(EffectsStageV2Test, AddOneWithSizeLimitsBlockSizeWithoutMax) {
  // ReadLock returns floor(480/7)*7 = 476 frames.
  TestAddOneWithDifferentVmos<AddOneWithSizeLimitsProcessor<0, 7>, 1, 1, 476>(ConfigOptions{
      .max_frames_per_call = 0,
      .block_size_frames = 7,
  });
}

TEST_F(EffectsStageV2Test, AddOneWithSizeLimitsBlockSizeEqualsMax) {
  // ReadLock returns 8 frames.
  TestAddOneWithDifferentVmos<AddOneWithSizeLimitsProcessor<8, 8>, 1, 1, 8>(ConfigOptions{
      .max_frames_per_call = 8,
      .block_size_frames = 8,
  });
}

TEST_F(EffectsStageV2Test, AddOneWithSizeLimitsBlockSizeLessThanMaxNotDivisible) {
  // ReadLock returns 24 frames.
  TestAddOneWithDifferentVmos<AddOneWithSizeLimitsProcessor<31, 8>, 1, 1, 24>(ConfigOptions{
      .max_frames_per_call = 31,
      .block_size_frames = 8,
  });
}

TEST_F(EffectsStageV2Test, AddOneWithSizeLimitsBlockSizeLessThanMaxDivisible) {
  // ReadLock returns 32 frames.
  TestAddOneWithDifferentVmos<AddOneWithSizeLimitsProcessor<32, 8>, 1, 1, 32>(ConfigOptions{
      .max_frames_per_call = 32,
      .block_size_frames = 8,
  });
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
  constexpr auto kInputPacketBytes = kPacketFrames * sizeof(float);
  constexpr auto kOutputPacketBytes = kPacketFrames * sizeof(float);

  ConfigOptions options;
  CreateSeparateVmos(options, kInputPacketBytes, kOutputPacketBytes);
  auto config = MakeProcessorConfig(arena(), DupConfigOptions(options));
  auto server_end = AttachProcessorChannel(config);
  CheckOptionsProcessor processor(options, std::move(server_end), fidl_dispatcher());

  // Enqueue one packet in the source packet queue.
  auto packet_factory = std::make_unique<testing::PacketFactory>(dispatcher(), k48k1ChanFloatFormat,
                                                                 zx_system_get_page_size());
  auto stream =
      MakePacketQueue(k48k1ChanFloatFormat, {packet_factory->CreatePacket(1.0, kPacketDuration)});
  auto effects_stage = EffectsStageV2::Create(std::move(config), stream).take_value();

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
                         async_dispatcher_t* dispatcher,
                         std::vector<fuchsia_audio_effects::wire::ProcessMetrics>& metrics)
      : BaseProcessor(options, std::move(server_end), dispatcher), metrics_(metrics) {}

  void Process(ProcessRequestView request, ProcessCompleter::Sync& completer) {
    completer.ReplySuccess(
        fidl::VectorView<fuchsia_audio_effects::wire::ProcessMetrics>::FromExternal(metrics_));
  }

 private:
  std::vector<fuchsia_audio_effects::wire::ProcessMetrics>& metrics_;
};

TEST_F(EffectsStageV2Test, Metrics) {
  std::vector<fuchsia_audio_effects::wire::ProcessMetrics> expected_metrics(2);
  expected_metrics[0].Allocate(arena());
  expected_metrics[0].set_name(arena(), "stage1");
  expected_metrics[0].set_wall_time(arena(), 100);
  expected_metrics[0].set_cpu_time(arena(), 101);
  expected_metrics[0].set_queue_time(arena(), 102);
  expected_metrics[1].Allocate(arena());
  expected_metrics[1].set_name(arena(), "stage2");
  expected_metrics[1].set_wall_time(arena(), 200);
  expected_metrics[1].set_cpu_time(arena(), 201);
  expected_metrics[1].set_queue_time(arena(), 201);

  constexpr auto kInputPacketBytes = kPacketFrames * sizeof(float);
  constexpr auto kOutputPacketBytes = kPacketFrames * sizeof(float);

  ConfigOptions options;
  CreateSeparateVmos(options, kInputPacketBytes, kOutputPacketBytes);
  auto config = MakeProcessorConfig(arena(), DupConfigOptions(options));
  auto server_end = AttachProcessorChannel(config);
  ReturnMetricsProcessor processor(options, std::move(server_end), fidl_dispatcher(),
                                   expected_metrics);

  // Enqueue one packet in the source packet queue.
  auto packet_factory = std::make_unique<testing::PacketFactory>(dispatcher(), k48k1ChanFloatFormat,
                                                                 zx_system_get_page_size());
  auto stream =
      MakePacketQueue(k48k1ChanFloatFormat, {packet_factory->CreatePacket(1.0, kPacketDuration)});
  auto effects_stage = EffectsStageV2::Create(std::move(config), stream).take_value();

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
  auto stream = MakePacketQueue(k48k1ChanFloatFormat, {});
  auto effects_stage = EffectsStageV2::Create(std::move(config), stream).take_value();

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
  auto stream = MakePacketQueue(k48k1ChanFloatFormat, {});
  auto result = EffectsStageV2::Create(std::move(config), stream);
  EXPECT_TRUE(result.is_ok()) << "failed with status: " << result.error();
}

TEST_F(EffectsStageV2Test, CreateFailsMissingProcessorHandle) {
  auto config = DefaultGoodProcessorConfig(arena());
  auto stream = MakePacketQueue(k48k1ChanFloatFormat, {});

  config.clear_processor();
  auto result = EffectsStageV2::Create(std::move(config), stream);
  EXPECT_FALSE(result.is_ok());
}

TEST_F(EffectsStageV2Test, CreateFailsNoInputs) {
  auto config = DefaultGoodProcessorConfig(arena());
  auto stream = MakePacketQueue(k48k1ChanFloatFormat, {});

  config.clear_inputs();
  auto result = EffectsStageV2::Create(std::move(config), stream);
  EXPECT_FALSE(result.is_ok());
}

TEST_F(EffectsStageV2Test, CreateFailsNoOutputs) {
  auto config = DefaultGoodProcessorConfig(arena());
  auto stream = MakePacketQueue(k48k1ChanFloatFormat, {});

  config.clear_outputs();
  auto result = EffectsStageV2::Create(std::move(config), stream);
  EXPECT_FALSE(result.is_ok());
}

TEST_F(EffectsStageV2Test, CreateFailsTooManyInputs) {
  auto config = DefaultGoodProcessorConfig(arena());
  auto stream = MakePacketQueue(k48k1ChanFloatFormat, {});

  std::vector<fuchsia_audio_effects::wire::InputConfiguration> inputs(2);
  inputs[0] = config.inputs()[0];
  config.inputs() =
      fidl::VectorView<fuchsia_audio_effects::wire::InputConfiguration>::FromExternal(inputs);

  auto result = EffectsStageV2::Create(std::move(config), stream);
  EXPECT_FALSE(result.is_ok());
}

TEST_F(EffectsStageV2Test, CreateFailsTooManyOutputs) {
  auto config = DefaultGoodProcessorConfig(arena());
  auto stream = MakePacketQueue(k48k1ChanFloatFormat, {});

  std::vector<fuchsia_audio_effects::wire::OutputConfiguration> outputs(2);
  outputs[0] = config.outputs()[0];
  config.outputs() =
      fidl::VectorView<fuchsia_audio_effects::wire::OutputConfiguration>::FromExternal(outputs);

  auto result = EffectsStageV2::Create(std::move(config), stream);
  EXPECT_FALSE(result.is_ok());
}

TEST_F(EffectsStageV2Test, CreateFailsInputMissingFormat) {
  auto config = DefaultGoodProcessorConfig(arena());
  auto stream = MakePacketQueue(k48k1ChanFloatFormat, {});

  config.inputs()[0].clear_format();
  auto result = EffectsStageV2::Create(std::move(config), stream);
  EXPECT_FALSE(result.is_ok());
}

TEST_F(EffectsStageV2Test, CreateFailsOutputMissingFormat) {
  auto config = DefaultGoodProcessorConfig(arena());
  auto stream = MakePacketQueue(k48k1ChanFloatFormat, {});

  config.outputs()[0].clear_format();
  auto result = EffectsStageV2::Create(std::move(config), stream);
  EXPECT_FALSE(result.is_ok());
}

TEST_F(EffectsStageV2Test, CreateFailsInputFormatNotFloat) {
  auto config = DefaultGoodProcessorConfig(arena());
  auto stream = MakePacketQueue(k48k1ChanFloatFormat, {});

  config.inputs()[0].format().sample_format = ASF::kUnsigned8;
  auto result = EffectsStageV2::Create(std::move(config), stream);
  EXPECT_FALSE(result.is_ok());
}

TEST_F(EffectsStageV2Test, CreateFailsOutputFormatNotFloat) {
  auto config = DefaultGoodProcessorConfig(arena());
  auto stream = MakePacketQueue(k48k1ChanFloatFormat, {});

  config.outputs()[0].format().sample_format = ASF::kUnsigned8;
  auto result = EffectsStageV2::Create(std::move(config), stream);
  EXPECT_FALSE(result.is_ok());
}

TEST_F(EffectsStageV2Test, CreateFailsInputOutputFpsMismatch) {
  auto config = DefaultGoodProcessorConfig(arena());
  auto stream = MakePacketQueue(k48k1ChanFloatFormat, {});

  config.inputs()[0].format().frames_per_second = 48000;
  config.outputs()[0].format().frames_per_second = 44100;
  auto result = EffectsStageV2::Create(std::move(config), stream);
  EXPECT_FALSE(result.is_ok());
}

TEST_F(EffectsStageV2Test, CreateFailsInputMissingBuffer) {
  auto config = DefaultGoodProcessorConfig(arena());
  auto stream = MakePacketQueue(k48k1ChanFloatFormat, {});

  config.inputs()[0].clear_buffer();
  auto result = EffectsStageV2::Create(std::move(config), stream);
  EXPECT_FALSE(result.is_ok());
}

TEST_F(EffectsStageV2Test, CreateFailsOutputMissingBuffer) {
  auto config = DefaultGoodProcessorConfig(arena());
  auto stream = MakePacketQueue(k48k1ChanFloatFormat, {});

  config.outputs()[0].clear_buffer();
  auto result = EffectsStageV2::Create(std::move(config), stream);
  EXPECT_FALSE(result.is_ok());
}

TEST_F(EffectsStageV2Test, CreateFailsInputBufferEmpty) {
  auto config = DefaultGoodProcessorConfig(arena());
  auto stream = MakePacketQueue(k48k1ChanFloatFormat, {});

  config.inputs()[0].buffer().size = 0;
  auto result = EffectsStageV2::Create(std::move(config), stream);
  EXPECT_FALSE(result.is_ok());
}

TEST_F(EffectsStageV2Test, CreateFailsOutputBufferEmpty) {
  auto config = DefaultGoodProcessorConfig(arena());
  auto stream = MakePacketQueue(k48k1ChanFloatFormat, {});

  config.outputs()[0].buffer().size = 0;
  auto result = EffectsStageV2::Create(std::move(config), stream);
  EXPECT_FALSE(result.is_ok());
}

TEST_F(EffectsStageV2Test, CreateFailsInputBufferVmoInvalid) {
  auto config = DefaultGoodProcessorConfig(arena());
  auto stream = MakePacketQueue(k48k1ChanFloatFormat, {});

  config.inputs()[0].buffer().vmo = zx::vmo();
  auto result = EffectsStageV2::Create(std::move(config), stream);
  EXPECT_FALSE(result.is_ok());
}

TEST_F(EffectsStageV2Test, CreateFailsOutputBufferVmoInvalid) {
  auto config = DefaultGoodProcessorConfig(arena());
  auto stream = MakePacketQueue(k48k1ChanFloatFormat, {});

  config.outputs()[0].buffer().vmo = zx::vmo();
  auto result = EffectsStageV2::Create(std::move(config), stream);
  EXPECT_FALSE(result.is_ok());
}

TEST_F(EffectsStageV2Test, CreateFailsInputBufferVmoMustBeMappable) {
  auto config = DefaultGoodProcessorConfig(arena());
  auto stream = MakePacketQueue(k48k1ChanFloatFormat, {});

  auto& buffer = config.inputs()[0].buffer();
  ASSERT_EQ(ZX_OK, buffer.vmo.replace(ZX_RIGHT_WRITE, &buffer.vmo));
  auto result = EffectsStageV2::Create(std::move(config), stream);
  EXPECT_FALSE(result.is_ok());
}

TEST_F(EffectsStageV2Test, CreateFailsOutputBufferVmoMustBeMappable) {
  auto config = DefaultGoodProcessorConfig(arena());
  auto stream = MakePacketQueue(k48k1ChanFloatFormat, {});

  auto& buffer = config.outputs()[0].buffer();
  ASSERT_EQ(ZX_OK, buffer.vmo.replace(ZX_RIGHT_READ, &buffer.vmo));
  auto result = EffectsStageV2::Create(std::move(config), stream);
  EXPECT_FALSE(result.is_ok());
}

TEST_F(EffectsStageV2Test, CreateFailsInputBufferVmoMustBeWritable) {
  auto config = DefaultGoodProcessorConfig(arena());
  auto stream = MakePacketQueue(k48k1ChanFloatFormat, {});

  auto& buffer = config.inputs()[0].buffer();
  ASSERT_EQ(ZX_OK, buffer.vmo.replace(ZX_RIGHT_MAP, &buffer.vmo));
  auto result = EffectsStageV2::Create(std::move(config), stream);
  EXPECT_FALSE(result.is_ok());
}

TEST_F(EffectsStageV2Test, CreateFailsOutputBufferVmoMustBeReadable) {
  auto config = DefaultGoodProcessorConfig(arena());
  auto stream = MakePacketQueue(k48k1ChanFloatFormat, {});

  auto& buffer = config.outputs()[0].buffer();
  ASSERT_EQ(ZX_OK, buffer.vmo.replace(ZX_RIGHT_MAP, &buffer.vmo));
  auto result = EffectsStageV2::Create(std::move(config), stream);
  EXPECT_FALSE(result.is_ok());
}

TEST_F(EffectsStageV2Test, CreateFailsInputBufferVmoTooSmall) {
  auto config = DefaultGoodProcessorConfig(arena());
  auto stream = MakePacketQueue(k48k1ChanFloatFormat, {});

  auto& buffer = config.inputs()[0].buffer();
  uint64_t vmo_size;
  ASSERT_EQ(ZX_OK, buffer.vmo.get_size(&vmo_size));
  buffer.size = vmo_size + 1;

  auto result = EffectsStageV2::Create(std::move(config), stream);
  EXPECT_FALSE(result.is_ok());  // too large by 1 byte
}

TEST_F(EffectsStageV2Test, CreateFailsOutputBufferVmoTooSmall) {
  auto config = DefaultGoodProcessorConfig(arena());
  auto stream = MakePacketQueue(k48k1ChanFloatFormat, {});

  auto& buffer = config.outputs()[0].buffer();
  uint64_t vmo_size;
  ASSERT_EQ(ZX_OK, buffer.vmo.get_size(&vmo_size));
  buffer.size = vmo_size + 1;  // too large by 1 byte

  auto result = EffectsStageV2::Create(std::move(config), stream);
  EXPECT_FALSE(result.is_ok());
}

TEST_F(EffectsStageV2Test, CreateFailsInputBufferOffsetTooLarge) {
  auto config = DefaultGoodProcessorConfig(arena());
  auto stream = MakePacketQueue(k48k1ChanFloatFormat, {});

  auto& buffer = config.inputs()[0].buffer();
  uint64_t vmo_size;
  ASSERT_EQ(ZX_OK, buffer.vmo.get_size(&vmo_size));
  buffer.offset = vmo_size - buffer.size + 1;  // too large by 1 byte

  auto result = EffectsStageV2::Create(std::move(config), stream);
  EXPECT_FALSE(result.is_ok());
}

TEST_F(EffectsStageV2Test, CreateFailsOutputBufferOffsetTooLarge) {
  auto config = DefaultGoodProcessorConfig(arena());
  auto stream = MakePacketQueue(k48k1ChanFloatFormat, {});

  auto& buffer = config.outputs()[0].buffer();
  uint64_t vmo_size;
  ASSERT_EQ(ZX_OK, buffer.vmo.get_size(&vmo_size));
  buffer.offset = vmo_size - buffer.size + 1;  // too large by 1 byte

  auto result = EffectsStageV2::Create(std::move(config), stream);
  EXPECT_FALSE(result.is_ok());
}

TEST_F(EffectsStageV2Test, CreateFailsInputBufferTooSmall) {
  auto config = DefaultGoodProcessorConfig(arena());
  auto stream = MakePacketQueue(k48k1ChanFloatFormat, {});

  config.set_max_frames_per_call(arena(), 10);
  config.inputs()[0].buffer().size = 9 * sizeof(float);

  auto result = EffectsStageV2::Create(std::move(config), stream);
  EXPECT_FALSE(result.is_ok());
}

TEST_F(EffectsStageV2Test, CreateFailsOutputBufferTooSmall) {
  auto config = DefaultGoodProcessorConfig(arena());
  auto stream = MakePacketQueue(k48k1ChanFloatFormat, {});

  config.set_max_frames_per_call(arena(), 10);
  config.outputs()[0].buffer().size = 9 * sizeof(float);

  auto result = EffectsStageV2::Create(std::move(config), stream);
  EXPECT_FALSE(result.is_ok());
}

TEST_F(EffectsStageV2Test, CreateFailsOutputBufferPartiallyOverlapsInputBuffer) {
  auto config = DefaultGoodProcessorConfig(arena());
  auto stream = MakePacketQueue(k48k1ChanFloatFormat, {});

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
  auto stream = MakePacketQueue(k48k1ChanFloatFormat, {});

  auto max_frames = config.inputs()[0].buffer().size / sizeof(float);
  config.set_block_size_frames(arena(), max_frames + 1);
  auto result = EffectsStageV2::Create(std::move(config), stream);
  EXPECT_FALSE(result.is_ok());
}

TEST_F(EffectsStageV2Test, CreateFailsMaxFramesPerCallTooBig) {
  auto config = DefaultGoodProcessorConfig(arena());
  auto stream = MakePacketQueue(k48k1ChanFloatFormat, {});

  auto max_frames = config.inputs()[0].buffer().size / sizeof(float);
  config.set_max_frames_per_call(arena(), max_frames + 1);
  auto result = EffectsStageV2::Create(std::move(config), stream);
  EXPECT_FALSE(result.is_ok());
}

TEST_F(EffectsStageV2Test, CreateFailsInputSampleFormatDoesNotMatchSource) {
  auto config = DefaultGoodProcessorConfig(arena());
  auto stream = MakePacketQueue(k48k1ChanFloatFormat, {});

  config.inputs()[0].format().sample_format = ASF::kUnsigned8;
  auto result = EffectsStageV2::Create(std::move(config), stream);
  EXPECT_FALSE(result.is_ok());
}

TEST_F(EffectsStageV2Test, CreateFailsInputChannelCountDoesNotMatchSource) {
  auto config = DefaultGoodProcessorConfig(arena());
  auto stream = MakePacketQueue(k48k1ChanFloatFormat, {});

  config.inputs()[0].format().channel_count = 2;
  auto result = EffectsStageV2::Create(std::move(config), stream);
  EXPECT_FALSE(result.is_ok());
}

TEST_F(EffectsStageV2Test, CreateFailsInputFpsDoesNotMatchSource) {
  auto config = DefaultGoodProcessorConfig(arena());
  auto stream = MakePacketQueue(k48k1ChanFloatFormat, {});

  config.inputs()[0].format().frames_per_second = 44100;
  auto result = EffectsStageV2::Create(std::move(config), stream);
  EXPECT_FALSE(result.is_ok());
}

//
// Buffers
//

TEST(EffectsStageV2BuffersTest, CreateSeparate) {
  ConfigOptions options;
  CreateSeparateVmos(options, 128, 256);

  auto buffers = EffectsStageV2::Buffers::Create(options.input_buffer, options.output_buffer);
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

  auto buffers = EffectsStageV2::Buffers::Create(options.input_buffer, options.output_buffer);
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

  auto buffers = EffectsStageV2::Buffers::Create(options.input_buffer, options.output_buffer);
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

  auto buffers = EffectsStageV2::Buffers::Create(options.input_buffer, options.output_buffer);
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

//
// RingOut
//

struct RingOutTestParameters {
  uint32_t ring_out_frames;
  uint32_t max_frames_per_call;
  // The expected number of frames returned by each ReadLock call.
  uint32_t read_lock_frames;
};

class EffectsStageV2RingOutTest : public EffectsStageV2Test,
                                  public ::testing::WithParamInterface<RingOutTestParameters> {};

TEST_P(EffectsStageV2RingOutTest, RingoutFrames) {
  constexpr auto kInputPacketBytes = kPacketFrames * sizeof(float);
  constexpr auto kOutputPacketBytes = kPacketFrames * sizeof(float);

  ConfigOptions options;
  options.ring_out_frames = GetParam().ring_out_frames;
  options.max_frames_per_call = GetParam().max_frames_per_call;
  CreateSeparateVmos(options, kInputPacketBytes, kOutputPacketBytes);

  // Use a simple AddOneProcessor.
  auto config = MakeProcessorConfig(arena(), DupConfigOptions(options));
  auto server_end = AttachProcessorChannel(config);
  AddOneProcessor processor(options, std::move(server_end), fidl_dispatcher());

  auto packet_factory = std::make_unique<testing::PacketFactory>(dispatcher(), k48k1ChanFloatFormat,
                                                                 zx_system_get_page_size());
  auto stream = MakePacketQueue(k48k1ChanFloatFormat, {});
  auto effects_stage = EffectsStageV2::Create(std::move(config), stream).take_value();

  // Add 48 frames to our source.
  stream->PushPacket(packet_factory->CreatePacket(1.0, zx::msec(1)));

  // Read the first packet.
  {
    auto buf = effects_stage->ReadLock(rlctx, Fixed(0), 480);
    ASSERT_TRUE(buf);
    EXPECT_EQ(0, buf->start().Floor());
    EXPECT_EQ(48, buf->length().Floor());
  }

  // TODO(fxbug.dev/50669): This will be unnecessary after we update ReadLock implementations
  // to never return an out-of-bounds packet.
  stream->Trim(Fixed(48));

  // Now we expect our ringout to be split across many buffers.
  int64_t start_frame = 48;
  uint32_t ringout_frames = 0;
  {
    while (ringout_frames < GetParam().ring_out_frames) {
      auto buf = effects_stage->ReadLock(rlctx, Fixed(start_frame), GetParam().ring_out_frames);
      ASSERT_TRUE(buf);
      EXPECT_EQ(start_frame, buf->start().Floor());
      EXPECT_EQ(GetParam().read_lock_frames, buf->length().Floor());
      start_frame += GetParam().read_lock_frames;
      ringout_frames += GetParam().read_lock_frames;
    }
  }

  {
    auto buf = effects_stage->ReadLock(rlctx, Fixed(start_frame), 480);
    EXPECT_FALSE(buf);
  }

  // Add another data packet to verify we correctly reset the ringout when the source goes silent
  // again.
  start_frame += 480;
  packet_factory->SeekToFrame(start_frame);
  stream->PushPacket(packet_factory->CreatePacket(1.0, zx::msec(1)));

  // Read the next packet.
  {
    auto buf = effects_stage->ReadLock(rlctx, Fixed(start_frame), 48);
    ASSERT_TRUE(buf);
    EXPECT_EQ(start_frame, buf->start().Floor());
    EXPECT_EQ(48, buf->length().Floor());
    start_frame += buf->length().Floor();
  }

  // TODO(fxbug.dev/50669): This will be unnecessary after we update ReadLock implementations
  // to never return an out-of-bounds packet.
  stream->Trim(Fixed(start_frame));

  // Now we expect our ringout to be split across many buffers.
  ringout_frames = 0;
  {
    while (ringout_frames < GetParam().ring_out_frames) {
      auto buf = effects_stage->ReadLock(rlctx, Fixed(start_frame), GetParam().ring_out_frames);
      ASSERT_TRUE(buf);
      EXPECT_EQ(start_frame, buf->start().Floor());
      EXPECT_EQ(GetParam().read_lock_frames, buf->length().Floor());
      start_frame += GetParam().read_lock_frames;
      ringout_frames += GetParam().read_lock_frames;
    }
  }

  {
    auto buf = effects_stage->ReadLock(rlctx, Fixed(48), 480);
    EXPECT_FALSE(buf);
  }
}

const RingOutTestParameters kNoRingout{
    .ring_out_frames = 0,
    .max_frames_per_call = 0,
    .read_lock_frames = 0,
};

const RingOutTestParameters kSmallRingOutNoBlockSize{
    .ring_out_frames = 4,
    .max_frames_per_call = 0,
    .read_lock_frames = 4,
};

const RingOutTestParameters kLargeRingOutNoBlockSize{
    .ring_out_frames = 8192,
    .max_frames_per_call = 0,
    .read_lock_frames = 480,  // VMO buffer size
};

const RingOutTestParameters kMaxFramesPerBufferLowerThanRingOutFrames{
    .ring_out_frames = 8192,
    .max_frames_per_call = 128,
    .read_lock_frames = 128,
};

std::string PrintRingOutParam(
    const ::testing::TestParamInfo<EffectsStageV2RingOutTest::ParamType>& info) {
  std::ostringstream os;
  os << "ring_out_frames_" << info.param.ring_out_frames << "_"
     << "max_frames_per_call_" << info.param.max_frames_per_call << "_"
     << "read_lock_frames_" << info.param.read_lock_frames;
  return os.str();
}

INSTANTIATE_TEST_SUITE_P(EffectsStageV2RingOutTestInstance, EffectsStageV2RingOutTest,
                         ::testing::Values(kNoRingout, kSmallRingOutNoBlockSize,
                                           kLargeRingOutNoBlockSize,
                                           kMaxFramesPerBufferLowerThanRingOutFrames),
                         PrintRingOutParam);

}  // namespace
}  // namespace media::audio
