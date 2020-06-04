// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/media/audio/audio_core/audio_output.h"

#include <lib/fit/defer.h>
#include <lib/trace/event.h>
#include <lib/zx/clock.h>

#include <limits>

#include "src/media/audio/audio_core/audio_driver.h"
#include "src/media/audio/audio_core/base_renderer.h"
#include "src/media/audio/audio_core/mixer/mixer.h"
#include "src/media/audio/audio_core/mixer/no_op.h"
#include "src/media/audio/lib/logging/logging.h"

namespace media::audio {

static constexpr zx::duration kMaxTrimPeriod = zx::msec(10);

// TODO(49345): We should not need driver to be set for all Audio Devices.
AudioOutput::AudioOutput(ThreadingModel* threading_model, DeviceRegistry* registry,
                         LinkMatrix* link_matrix)
    : AudioDevice(Type::Output, threading_model, registry, link_matrix,
                  std::make_unique<AudioDriverV1>(this)) {
  next_sched_time_ = async::Now(mix_domain().dispatcher());
  next_sched_time_known_ = true;
}

AudioOutput::AudioOutput(ThreadingModel* threading_model, DeviceRegistry* registry,
                         LinkMatrix* link_matrix, std::unique_ptr<AudioDriver> driver)
    : AudioDevice(Type::Output, threading_model, registry, link_matrix, std::move(driver)) {
  next_sched_time_ = async::Now(mix_domain().dispatcher());
  next_sched_time_known_ = true;
}

void AudioOutput::Process() {
  FX_CHECK(pipeline_);
  auto now = async::Now(mix_domain().dispatcher());

  int64_t trace_wake_delta = next_sched_time_known_ ? (now - next_sched_time_).get() : 0;
  TRACE_DURATION("audio", "AudioOutput::Process", "wake delta", TA_INT64(trace_wake_delta));

  // At this point, we should always know when our implementation would like to be called to do some
  // mixing work next. If we do not know, then we should have already shut down.
  //
  // If the next sched time has not arrived yet, don't attempt to mix anything. Just trim the queues
  // and move on.
  FX_DCHECK(next_sched_time_known_);
  if (now >= next_sched_time_) {
    // Clear the flag. If the implementation does not set it during the cycle by calling
    // SetNextSchedTime, we consider it an error and shut down.
    next_sched_time_known_ = false;

    auto mix_frames = StartMixJob(now);
    if (mix_frames) {
      auto buf = pipeline_->ReadLock(now, mix_frames->start, mix_frames->length);
      FX_CHECK(buf);
      FinishMixJob(*mix_frames, reinterpret_cast<float*>(buf->payload()));
    } else {
      pipeline_->Trim(now);
    }
  }

  if (!next_sched_time_known_) {
    FX_LOGS(ERROR) << "Output failed to schedule next service time. Shutting down!";
    ShutdownSelf();
    return;
  }

  // Figure out when we should wake up to do more work again. No matter how long our implementation
  // wants to wait, we need to make sure to wake up and periodically trim our input queues.
  auto max_sched_time = now + kMaxTrimPeriod;
  if (next_sched_time_ > max_sched_time) {
    next_sched_time_ = max_sched_time;
  }
  zx_status_t status = mix_timer_.PostForTime(mix_domain().dispatcher(), next_sched_time_);
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to schedule mix";
    ShutdownSelf();
  }
}

fit::result<std::shared_ptr<Mixer>, zx_status_t> AudioOutput::InitializeSourceLink(
    const AudioObject& source, std::shared_ptr<ReadableStream> stream) {
  TRACE_DURATION("audio", "AudioOutput::InitializeSourceLink");

  auto usage = source.usage();
  FX_DCHECK(usage) << "Source has no assigned usage";
  if (!usage) {
    usage = {StreamUsage::WithRenderUsage(RenderUsage::MEDIA)};
  }

  if (stream) {
    auto mixer = pipeline_->AddInput(std::move(stream), *usage);
    const auto& settings = device_settings();
    if (settings != nullptr) {
      auto [flags, cur_gain_state] = settings->SnapshotGainState();

      mixer->bookkeeping().gain.SetDestGain(
          cur_gain_state.muted
              ? fuchsia::media::audio::MUTED_GAIN_DB
              : std::clamp(cur_gain_state.gain_db, Gain::kMinGainDb, Gain::kMaxGainDb));
    }
    return fit::ok(std::move(mixer));
  }

  return fit::ok(std::make_shared<audio::mixer::NoOp>());
}

void AudioOutput::CleanupSourceLink(const AudioObject& source,
                                    std::shared_ptr<ReadableStream> stream) {
  TRACE_DURATION("audio", "AudioOutput::CleanupSourceLink");
  if (stream) {
    pipeline_->RemoveInput(*stream);
  }
}

fit::result<std::shared_ptr<ReadableStream>, zx_status_t> AudioOutput::InitializeDestLink(
    const AudioObject& dest) {
  TRACE_DURATION("audio", "AudioOutput::InitializeDestLink");
  if (!pipeline_) {
    return fit::error(ZX_ERR_BAD_STATE);
  }
  return fit::ok(pipeline_->loopback());
}

void AudioOutput::SetupMixTask(const PipelineConfig& config, const VolumeCurve& volume_curve,
                               uint32_t channels, size_t max_block_size_frames,
                               TimelineFunction device_reference_clock_to_fractional_frame) {
  pipeline_ =
      std::make_unique<OutputPipelineImpl>(config, volume_curve, channels, max_block_size_frames,
                                           device_reference_clock_to_fractional_frame);
  pipeline_->SetMinLeadTime(min_lead_time_);
}

void AudioOutput::Cleanup() {
  AudioDevice::Cleanup();
  mix_timer_.Cancel();
}

fit::promise<void, fuchsia::media::audio::UpdateEffectError> AudioOutput::UpdateEffect(
    const std::string& instance_name, const std::string& config) {
  fit::bridge<void, fuchsia::media::audio::UpdateEffectError> bridge;
  mix_domain().PostTask([this, self = shared_from_this(), instance_name, config,
                         completer = std::move(bridge.completer)]() mutable {
    OBTAIN_EXECUTION_DOMAIN_TOKEN(token, &mix_domain());
    if (pipeline_ && !is_shutting_down()) {
      completer.complete_or_abandon(pipeline_->UpdateEffect(instance_name, config));
      return;
    }
    completer.complete_error(fuchsia::media::audio::UpdateEffectError::NOT_FOUND);
  });
  return bridge.consumer.promise();
}

}  // namespace media::audio
