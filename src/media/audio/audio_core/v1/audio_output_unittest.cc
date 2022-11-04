// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/v1/audio_output.h"

#include <lib/fit/defer.h>

#include "src/media/audio/audio_core/v1/audio_device_manager.h"
#include "src/media/audio/audio_core/v1/clock.h"
#include "src/media/audio/audio_core/v1/device_config.h"
#include "src/media/audio/audio_core/v1/loudness_transform.h"
#include "src/media/audio/audio_core/v1/testing/fake_audio_driver.h"
#include "src/media/audio/audio_core/v1/testing/fake_audio_renderer.h"
#include "src/media/audio/audio_core/v1/testing/fake_stream.h"
#include "src/media/audio/audio_core/v1/testing/threading_model_fixture.h"
#include "src/media/audio/lib/clock/clone_mono.h"
#include "src/media/audio/lib/effects_loader/testing/test_effects_v1.h"
#include "src/media/audio/lib/processing/gain.h"

namespace media::audio {
namespace {

// Used when the ReadLockContext is unused by the test.
static media::audio::ReadableStream::ReadLockContext rlctx;

static constexpr size_t kFramesPerSecond = 48000;
static const TimelineFunction kDriverRefPtsToFractionalFrames =
    TimelineFunction(0, 0, Fixed(kFramesPerSecond).raw_value(), zx::sec(1).get());

// An OutputPipeline that always returns std::nullopt from |ReadLock|.
class TestOutputPipeline : public OutputPipeline {
 public:
  TestOutputPipeline(const Format& format, std::shared_ptr<AudioCoreClockFactory> clock_factory)
      : OutputPipeline(format),
        audio_clock_(clock_factory->CreateClientFixed(clock::AdjustableCloneOfMonotonic())) {}

  void EnqueueBuffer(Fixed start_frame, int64_t frame_count, void* payload) {
    buffers_.push_back(MakeCachedBuffer(start_frame, frame_count, payload, StreamUsageMask(),
                                        media_audio::kUnityGainDb)
                           .value());
  }

  // |media::audio::ReadableStream|
  std::optional<ReadableStream::Buffer> ReadLockImpl(ReadLockContext& ctx, Fixed frame,
                                                     int64_t frame_count) override {
    if (buffers_.empty()) {
      return std::nullopt;
    }
    auto buffer = std::move(buffers_.front());
    buffers_.pop_front();
    return buffer;
  }
  void TrimImpl(Fixed frame) override {}
  TimelineFunctionSnapshot ref_time_to_frac_presentation_frame() const override {
    return TimelineFunctionSnapshot{
        .timeline_function = kDriverRefPtsToFractionalFrames,
        .generation = 1,
    };
  }
  std::shared_ptr<Clock> reference_clock() override { return audio_clock_; }

  // |media::audio::OutputPipeline|
  std::shared_ptr<ReadableRingBuffer> dup_loopback() const override { return nullptr; }
  std::shared_ptr<Mixer> AddInput(
      std::shared_ptr<ReadableStream> stream, const StreamUsage& usage,
      std::optional<float> initial_dest_gain_db = std::nullopt,
      Mixer::Resampler sampler_hint = Mixer::Resampler::Default) override {
    return nullptr;
  }
  void RemoveInput(const ReadableStream& stream) override {}
  fpromise::result<void, fuchsia::media::audio::UpdateEffectError> UpdateEffect(
      const std::string& instance_name, const std::string& config) override {
    return fpromise::error(fuchsia::media::audio::UpdateEffectError::NOT_FOUND);
  }

 private:
  std::deque<ReadableStream::Buffer> buffers_;

  std::shared_ptr<Clock> audio_clock_;
};

class StubDriver : public AudioDriver {
 public:
  static constexpr size_t kSafeWriteDelayFrames = 480;
  static constexpr auto kSafeWriteDelayDuration = zx::msec(10);
  static constexpr size_t kRingBufferFrames = 48000;

  StubDriver(AudioDevice* owner) : AudioDriver(owner) {}

  const TimelineFunction& ref_time_to_frac_presentation_frame() const override {
    return kDriverRefPtsToFractionalFrames;
  }
  const TimelineFunction& ref_time_to_frac_safe_read_or_write_frame() const override {
    return ref_time_to_safe_read_or_write_frame_;
  }

 private:
  const TimelineFunction ref_time_to_safe_read_or_write_frame_ =
      TimelineFunction(Fixed(kSafeWriteDelayFrames).raw_value(), 0,
                       Fixed(kFramesPerSecond).raw_value(), zx::sec(1).get());
};

class TestAudioOutput : public AudioOutput {
 public:
  TestAudioOutput(const DeviceConfig& config, ThreadingModel* threading_model,
                  DeviceRegistry* registry, LinkMatrix* link_matrix,
                  std::shared_ptr<AudioCoreClockFactory> clock_factory)
      : AudioOutput("", config, threading_model, registry, link_matrix, clock_factory,
                    nullptr /* EffectsLoaderV2 */, std::make_unique<StubDriver>(this)) {
    SetPresentationDelay(StubDriver::kSafeWriteDelayDuration);
  }

  using AudioOutput::FrameSpan;
  using AudioOutput::SetNextSchedTimeMono;
  void SetupMixTask(const DeviceConfig::OutputDeviceProfile& profile, uint32_t max_frames,
                    TimelineFunction clock_mono_to_output_frame) {
    OBTAIN_EXECUTION_DOMAIN_TOKEN(token, &mix_domain());
    AudioOutput::SetupMixTask(profile, max_frames, clock_mono_to_output_frame);
  }
  void Process() {
    OBTAIN_EXECUTION_DOMAIN_TOKEN(token, &mix_domain());
    AudioOutput::Process();
  }
  std::shared_ptr<OutputPipeline> CreateOutputPipeline(
      const PipelineConfig& config, const VolumeCurve& volume_curve, size_t max_block_size_frames,
      TimelineFunction device_reference_clock_to_fractional_frame,
      std::shared_ptr<Clock> ref_clock) override {
    if (output_pipeline_) {
      return output_pipeline_;
    }
    return AudioOutput::CreateOutputPipeline(config, volume_curve, max_block_size_frames,
                                             device_reference_clock_to_fractional_frame, ref_clock);
  }

  // Allow a test to provide a delegate to handle |AudioOutput::StartMixJob| invocations.
  using StartMixDelegate = fit::function<std::optional<AudioOutput::FrameSpan>(zx::time)>;
  void set_start_mix_delegate(StartMixDelegate delegate) {
    start_mix_delegate_ = std::move(delegate);
  }

  // Allow a test to provide a delegate to handle |AudioOutput::WriteMixJob| invocations.
  using WriteMixDelegate = fit::function<void(int64_t start, int64_t length, const float* payload)>;
  void set_write_mix_delegate(WriteMixDelegate delegate) {
    write_mix_delegate_ = std::move(delegate);
  }

  // Allow a test to provide a delegate to handle |AudioOutput::FinishMixJob| invocations.
  using FinishMixDelegate = fit::function<void(const AudioOutput::FrameSpan&)>;
  void set_finish_mix_delegate(FinishMixDelegate delegate) {
    finish_mix_delegate_ = std::move(delegate);
  }

  void set_output_pipeline(std::shared_ptr<OutputPipeline> output_pipeline) {
    output_pipeline_ = output_pipeline;
  }

  // |AudioOutput|
  std::optional<AudioOutput::FrameSpan> StartMixJob(zx::time device_ref_time) override {
    if (start_mix_delegate_) {
      return start_mix_delegate_(device_ref_time);
    } else {
      return std::nullopt;
    }
  }
  void WriteMixOutput(int64_t start, int64_t length, const float* buffer) override {
    if (write_mix_delegate_) {
      write_mix_delegate_(start, length, buffer);
    }
  }
  void FinishMixJob(const AudioOutput::FrameSpan& span) override {
    if (finish_mix_delegate_) {
      finish_mix_delegate_(span);
    }
  }
  zx::duration MixDeadline() const override { return zx::msec(10); }
  // |AudioDevice|
  void ApplyGainLimits(fuchsia::media::AudioGainInfo* in_out_info,
                       fuchsia::media::AudioGainValidFlags set_flags) override {}
  // TestAudioOutput does not implement enough state machine to fully initialize an AudioDriver .
  // It gets far enough for the AudioDriver to establish and expose its reference AudioClock.
  void OnWakeup() override { driver()->GetDriverInfo(); }

 private:
  StartMixDelegate start_mix_delegate_;
  WriteMixDelegate write_mix_delegate_;
  FinishMixDelegate finish_mix_delegate_;
  std::shared_ptr<OutputPipeline> output_pipeline_;
};

class AudioOutputTest : public testing::ThreadingModelFixture {
 protected:
  void SetUp() override {
    // Establish and start a remote driver, to respond to a GetDriverInfo request with the clock
    // domain, so that AudioDriver establishes and passes on an AudioClock for this device.
    zx::channel c1, c2;
    ASSERT_EQ(ZX_OK, zx::channel::create(0, &c1, &c2));
    remote_driver_ = std::make_unique<testing::FakeAudioDriver>(std::move(c1), dispatcher());
    audio_output_->driver()->Init(std::move(c2));
    remote_driver_->Start();

    threading_model().FidlDomain().ScheduleTask(audio_output_->Startup());
    RunLoopUntilIdle();
  }

  void SetupMixTask() {
    audio_output_->SetupMixTask(DeviceConfig::OutputDeviceProfile(), StubDriver::kRingBufferFrames,
                                stub_driver()->ref_time_to_frac_presentation_frame());
  }

  void CheckBuffer(void* buffer, float expected_sample, size_t num_samples) {
    float* floats = reinterpret_cast<float*>(buffer);
    for (size_t i = 0; i < num_samples; ++i) {
      ASSERT_FLOAT_EQ(expected_sample, floats[i]);
    }
  }

  StubDriver* stub_driver() { return static_cast<StubDriver*>(audio_output_->driver()); }

  VolumeCurve volume_curve_ = VolumeCurve::DefaultForMinGain(media_audio::kMinGainDb);
  std::shared_ptr<TestAudioOutput> audio_output_ = std::make_shared<TestAudioOutput>(
      context().process_config().device_config(), &threading_model(), &context().device_manager(),
      &context().link_matrix(), context().clock_factory());

  std::unique_ptr<testing::FakeAudioDriver> remote_driver_;
};

TEST_F(AudioOutputTest, ProcessTrimsInputStreamsIfNoMixJobProvided) {
  auto renderer = testing::FakeAudioRenderer::CreateWithDefaultFormatInfo(
      dispatcher(), &context().link_matrix(), context().clock_factory());
  SetupMixTask();
  context().link_matrix().LinkObjects(renderer, audio_output_,
                                      std::make_shared<MappedLoudnessTransform>(volume_curve_));

  // StartMixJob always returns nullopt (no work) and schedules another mix 1ms in the future.
  audio_output_->set_start_mix_delegate([this, audio_output = audio_output_.get()](zx::time) {
    audio_output->SetNextSchedTimeMono(Now() + zx::msec(1));
    return std::nullopt;
  });

  // Enqueue 2 packets:
  //   * packet 1 from 0ms -> 5ms.
  //   * packet 2 from 5ms -> 10ms.
  bool packet1_released = false;
  bool packet2_released = false;
  renderer->EnqueueAudioPacket(1.0, zx::msec(5), [&packet1_released] {
    FX_LOGS(INFO) << "Release packet 1";
    packet1_released = true;
  });
  renderer->EnqueueAudioPacket(1.0, zx::msec(5), [&packet2_released] {
    FX_LOGS(INFO) << "Release packet 2";
    packet2_released = true;
  });

  // Process kicks off the periodic mix task.
  audio_output_->Process();

  // After 4ms we should still be retaining packet1.
  RunLoopFor(zx::msec(4));
  ASSERT_FALSE(packet1_released);
  ASSERT_FALSE(packet2_released);

  // 5ms; all the audio from packet1 is consumed and it should be released. We should still have
  // packet2, however.
  RunLoopFor(zx::msec(1));
  ASSERT_TRUE(packet1_released);
  ASSERT_FALSE(packet2_released);

  // After 9ms we should still be retaining packet2.
  RunLoopFor(zx::msec(4));
  ASSERT_FALSE(packet2_released);

  // Finally after 10ms we will have released packet2.
  RunLoopFor(zx::msec(1));
  ASSERT_TRUE(packet2_released);
}

TEST_F(AudioOutputTest, ProcessRequestsSilenceIfNoSourceBuffer) {
  auto format = Format::Create({
                                   .sample_format = fuchsia::media::AudioSampleFormat::FLOAT,
                                   .channels = 2,
                                   .frames_per_second = 48000,
                               })
                    .take_value();

  // Use an output pipeline that will always return nullopt from ReadLock.
  auto pipeline = std::make_shared<TestOutputPipeline>(format, context().clock_factory());
  audio_output_->set_output_pipeline(pipeline);
  SetupMixTask();

  // Return some valid, non-silent frame range from StartMixJob.
  audio_output_->set_start_mix_delegate([](zx::time now) {
    return TestAudioOutput::FrameSpan{
        .start = 0,
        .length = 100,
        .is_mute = false,
    };
  });

  int64_t frames_written = 0;
  audio_output_->set_write_mix_delegate([&frames_written](auto start, auto length, auto payload) {
    EXPECT_EQ(start, 0);
    EXPECT_EQ(length, 100);
    EXPECT_EQ(payload, nullptr);  // null means silent
    frames_written += length;
  });

  bool finish_called = false;
  audio_output_->set_finish_mix_delegate([&frames_written, &finish_called](auto span) {
    EXPECT_EQ(span.start, 0);
    EXPECT_EQ(span.length, 100);
    EXPECT_FALSE(span.is_mute);
    EXPECT_EQ(frames_written, 100);
    finish_called = true;
  });

  // Now do a mix.
  audio_output_->Process();
  EXPECT_TRUE(finish_called);
}

// Test a case where ReadLock's first buffer is smaller than mix_job.length.
TEST_F(AudioOutputTest, ProcessSmallReadLocks) {
  const Format format =
      Format::Create({
                         .sample_format = fuchsia::media::AudioSampleFormat::FLOAT,
                         .channels = 2,
                         .frames_per_second = 48000,
                     })
          .take_value();

  // Use an output pipeline that will always return nullopt from ReadLock.
  auto pipeline = std::make_shared<TestOutputPipeline>(format, context().clock_factory());
  audio_output_->set_output_pipeline(pipeline);
  SetupMixTask();

  static constexpr int64_t kBufferFrames = 10;
  static constexpr int64_t kBufferSamples = kBufferFrames * 2;
  static constexpr int64_t kNumBuffers = 4;
  // Setup our buffer with data that is just the value of frame 'N' is 'N'.
  std::vector<float> buffer(kBufferSamples);
  for (size_t sample = 0; sample < kBufferSamples; ++sample) {
    buffer[sample] = static_cast<float>(sample);
  }
  // Enqueue several buffers, each with the same payload buffer.
  for (auto i = 0; i < kNumBuffers; ++i) {
    pipeline->EnqueueBuffer(Fixed(i * kBufferFrames), kBufferFrames, buffer.data());
  }

  // The mix job covers all four buffers.
  static constexpr auto kMixJob = TestAudioOutput::FrameSpan{
      .start = 0,
      .length = kBufferFrames * kNumBuffers,
      .is_mute = false,
  };
  audio_output_->set_start_mix_delegate([](zx::time now) { return kMixJob; });

  int64_t frames_written = 0;
  audio_output_->set_write_mix_delegate([&frames_written](auto start, auto length, auto payload) {
    EXPECT_EQ(start, frames_written);
    EXPECT_EQ(length, kBufferFrames);
    EXPECT_NE(payload, nullptr);
    for (auto sample = 0; sample < length; ++sample) {
      EXPECT_FLOAT_EQ(static_cast<float>(sample), payload[sample]);
    }
    frames_written += length;
  });

  bool called_finish_mix = false;
  audio_output_->set_finish_mix_delegate([&frames_written, &called_finish_mix](auto span) {
    EXPECT_EQ(span.start, kMixJob.start);
    EXPECT_EQ(span.length, kMixJob.length);
    EXPECT_EQ(span.is_mute, kMixJob.is_mute);
    EXPECT_EQ(frames_written, kMixJob.length);
    called_finish_mix = true;
  });

  // Now do a mix.
  audio_output_->Process();
  EXPECT_TRUE(called_finish_mix);
}

// Test a case where ReadLock's first buffer has a gap after mix_job.start.
TEST_F(AudioOutputTest, ProcessReadLockWithGap) {
  const Format format =
      Format::Create({
                         .sample_format = fuchsia::media::AudioSampleFormat::FLOAT,
                         .channels = 2,
                         .frames_per_second = 48000,
                     })
          .take_value();

  // Use an output pipeline that will always return nullopt from ReadLock.
  auto pipeline = std::make_shared<TestOutputPipeline>(format, context().clock_factory());
  audio_output_->set_output_pipeline(pipeline);
  SetupMixTask();

  static constexpr int64_t kBufferOffset = 5;
  static constexpr int64_t kBufferFrames = 10;
  static constexpr int64_t kBufferSamples = kBufferFrames * 2;
  // Setup our buffer with data that is just the value of frame 'N' is 'N'.
  std::vector<float> buffer(kBufferSamples);
  for (size_t sample = 0; sample < kBufferSamples; ++sample) {
    buffer[sample] = static_cast<float>(sample);
  }
  pipeline->EnqueueBuffer(Fixed(kBufferOffset), kBufferFrames, buffer.data());

  // The mix job covers all four buffers.
  static constexpr auto kMixJob = TestAudioOutput::FrameSpan{
      .start = 0,
      .length = kBufferOffset + kBufferFrames,
      .is_mute = false,
  };
  audio_output_->set_start_mix_delegate([](zx::time now) { return kMixJob; });

  int64_t frames_written = 0;
  audio_output_->set_write_mix_delegate([&frames_written](auto start, auto length, auto payload) {
    if (start == 0) {
      EXPECT_EQ(length, kBufferOffset);
      EXPECT_EQ(payload, nullptr);
    } else {
      EXPECT_EQ(frames_written, kBufferOffset);
      EXPECT_EQ(start, kBufferOffset);
      EXPECT_EQ(length, kBufferFrames);
      EXPECT_NE(payload, nullptr);
      for (auto sample = 0; sample < length; ++sample) {
        EXPECT_FLOAT_EQ(static_cast<float>(sample), payload[sample]);
      }
    }
    frames_written += length;
  });

  bool called_finish_mix = false;
  audio_output_->set_finish_mix_delegate([&frames_written, &called_finish_mix](auto span) {
    EXPECT_EQ(span.start, kMixJob.start);
    EXPECT_EQ(span.length, kMixJob.length);
    EXPECT_EQ(span.is_mute, kMixJob.is_mute);
    EXPECT_EQ(frames_written, kMixJob.length);
    called_finish_mix = true;
  });

  // Now do a mix.
  audio_output_->Process();
  EXPECT_TRUE(called_finish_mix);
}

// Verify AudioOutput loudness transform is updated with the |volume_curve| used in SetupMixTask.
TEST_F(AudioOutputTest, UpdateLoudnessTransformOnSetupMixTask) {
  static const TimelineFunction kOneFramePerMs = TimelineFunction(TimelineRate(1, 1'000'000));
  static const VolumeCurve volume_curve = VolumeCurve::DefaultForMinGain(-10.);
  static DeviceConfig::OutputDeviceProfile profile = DeviceConfig::OutputDeviceProfile(
      /*eligible_for_loopback=*/true,
      /*supported_usages=*/StreamUsageSetFromRenderUsages(kFidlRenderUsages), volume_curve,
      /*independent_volume_control=*/false, /*pipeline_config=*/PipelineConfig::Default(),
      /*driver_gain_db=*/0.0, /*software_gain_db=*/0.0);
  audio_output_->SetupMixTask(profile, zx::msec(1).to_msecs(), kOneFramePerMs);

  auto output_transform = audio_output_->profile().loudness_transform();
  auto expected_transform = std::make_shared<MappedLoudnessTransform>(volume_curve);
  EXPECT_FLOAT_EQ(output_transform->Evaluate<1>({VolumeValue{.5}}),
                  expected_transform->Evaluate<1>({VolumeValue{.5}}));
}

// Verify loudness_transform_ is NoOpLoudnessTransform to honor IndependentVolumeControl.
TEST_F(AudioOutputTest, HonorIndpendentVolumeControlLoudnessTransform) {
  static const TimelineFunction kOneFramePerMs = TimelineFunction(TimelineRate(1, 1'000'000));
  audio_output_->SetupMixTask(
      DeviceConfig::OutputDeviceProfile(
          /*eligible_for_loopback=*/true,
          /*supported_usages=*/StreamUsageSetFromRenderUsages(kFidlRenderUsages),
          VolumeCurve::DefaultForMinGain(VolumeCurve::kDefaultGainForMinVolume),
          /*independent_volume_control=*/true, PipelineConfig::Default(), /*driver_gain_db=*/0.0,
          /*software_gain_db=*/0.0),
      zx::msec(1).to_msecs(), kOneFramePerMs);

  auto transform = audio_output_->profile().loudness_transform();
  EXPECT_FLOAT_EQ(transform->Evaluate<1>({VolumeValue{0.}}), media_audio::kUnityGainDb);
  EXPECT_FLOAT_EQ(transform->Evaluate<1>({VolumeValue{1.}}), media_audio::kUnityGainDb);
}

TEST_F(AudioOutputTest, UpdateOutputPipeline) {
  // Setup test.
  auto test_effects = testing::TestEffectsV1Module::Open();
  test_effects.AddEffect("add_1.0").WithAction(TEST_EFFECTS_ACTION_ADD, 1.0);

  // Create OutputPipeline with no effects and verify output.
  SetupMixTask();
  auto pipeline = audio_output_->output_pipeline();

  // Add an input into our pipeline. Without this we won't run any effects as the stream will be
  // silent. This actually sends silence through the pipeline, but it's flagged with a gain > MUTE
  // so that it still gets mixed.
  auto format = Format::Create({
                                   .sample_format = fuchsia::media::AudioSampleFormat::FLOAT,
                                   .channels = 2,
                                   .frames_per_second = 48000,
                               })
                    .take_value();
  const auto stream_usage = StreamUsage::WithRenderUsage(RenderUsage::MEDIA);

  auto make_fake_stream = [this, format, stream_usage]() {
    auto fs = std::make_shared<testing::FakeStream>(format, context().clock_factory());
    fs->set_usage_mask({stream_usage});
    fs->set_gain_db(0.0);
    fs->timeline_function()->Update(TimelineFunction(
        TimelineRate(Fixed(format.frames_per_second()).raw_value(), zx::sec(1).to_nsecs())));
    return fs;
  };

  pipeline->AddInput(make_fake_stream(), stream_usage);

  {
    auto buf = pipeline->ReadLock(rlctx, Fixed(0), 48);

    EXPECT_TRUE(buf);
    EXPECT_EQ(buf->start().Floor(), 0);
    EXPECT_EQ(buf->length(), 48);
    CheckBuffer(buf->payload(), 0.0, 96);
  }

  // Update OutputPipeline and VolumeCurve, and verify output.
  PipelineConfig::MixGroup root{
      .name = "linearize",
      .input_streams =
          {
              RenderUsage::BACKGROUND,
          },
      .effects_v1 =
          {
              {
                  .lib_name = "test_effects_v1.so",
                  .effect_name = "add_1.0",
                  .instance_name = "",
                  .effect_config = "",
              },
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
          .effects_v1 =
              {
                  {
                      .lib_name = "test_effects_v1.so",
                      .effect_name = "add_1.0",
                      .instance_name = "",
                      .effect_config = "",
                  },
              },
          .output_rate = kFramesPerSecond,
          .output_channels = 2,
      }},
      .output_rate = kFramesPerSecond,
      .output_channels = 2,
  };
  auto volume_curve = VolumeCurve::DefaultForMinGain(-10.);
  auto profile_params = DeviceConfig::OutputDeviceProfile::Parameters{
      .pipeline_config = PipelineConfig(root), .volume_curve = volume_curve};

  bool updated_device_profile = false;
  auto promise = audio_output_->UpdateDeviceProfile(profile_params);
  // |audio_output_| now holds an active effect instance. It must be destroyed *before*
  // |test_effects| is dropped to allow the latter's destructor to clean up the list of effects and
  // avoid test pollution.
  auto cleanup = fit::defer([this]() { audio_output_.reset(); });
  context().threading_model().FidlDomain().executor()->schedule_task(
      promise.then([&updated_device_profile](fpromise::result<void, zx_status_t>& result) {
        updated_device_profile = true;
      }));
  RunLoopUntilIdle();
  EXPECT_TRUE(updated_device_profile);
  pipeline = audio_output_->output_pipeline();
  pipeline->AddInput(make_fake_stream(), stream_usage);

  {
    auto buf = pipeline->ReadLock(rlctx, Fixed(0), 48);
    EXPECT_TRUE(buf);
    EXPECT_EQ(buf->start().Floor(), 0);
    EXPECT_EQ(buf->length(), 48);
    CheckBuffer(buf->payload(), 2.0, 96);
  }

  auto result_transform = audio_output_->profile().loudness_transform();
  auto expected_transform = std::make_shared<MappedLoudnessTransform>(volume_curve);
  EXPECT_FLOAT_EQ(result_transform->Evaluate<1>({VolumeValue{.5}}),
                  expected_transform->Evaluate<1>({VolumeValue{.5}}));
}

}  // namespace
}  // namespace media::audio
