// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/media/audio/audio_core/v1/audio_output.h"

#include <lib/fit/defer.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>
#include <lib/zx/clock.h>

#include <iomanip>
#include <limits>

#include "src/media/audio/audio_core/shared/mixer/mixer.h"
#include "src/media/audio/audio_core/shared/mixer/no_op.h"
#include "src/media/audio/audio_core/shared/pin_executable_memory.h"
#include "src/media/audio/audio_core/v1/audio_driver.h"
#include "src/media/audio/audio_core/v1/base_renderer.h"
#include "src/media/audio/audio_core/v1/stage_metrics.h"

namespace media::audio {

namespace {
void DumpStageMetrics(std::ostringstream& os, const StageMetrics& metrics) {
  os << std::string_view(metrics.name) << ": "
     << "wall_time = " << metrics.wall_time.to_nsecs() << " ns, "
     << "cpu_time = " << metrics.cpu_time.to_nsecs() << " ns, "
     << "queue_time = " << metrics.queue_time.to_nsecs() << " ns, "
     << "page_fault_time = " << metrics.page_fault_time.to_nsecs() << " ns, "
     << "kernel_lock_contention_time = " << metrics.kernel_lock_contention_time.to_nsecs()
     << " ns\n";
}
}  // namespace

// This MONOTONIC-based duration is the maximum interval between trim operations.
static constexpr zx::duration kMaxTrimPeriod = zx::msec(10);

// TODO(fxbug.dev/49345): We should not need driver to be set for all Audio Devices.
AudioOutput::AudioOutput(const std::string& name, const DeviceConfig& config,
                         ThreadingModel* threading_model, DeviceRegistry* registry,
                         LinkMatrix* link_matrix,
                         std::shared_ptr<AudioCoreClockFactory> clock_factory,
                         EffectsLoaderV2* effects_loader_v2, std::unique_ptr<AudioDriver> driver)
    : AudioDevice(Type::Output, name, config, threading_model, registry, link_matrix, clock_factory,
                  std::move(driver)),
      reporter_(Reporter::Singleton().CreateOutputDevice(name, mix_domain().name())),
      effects_loader_v2_(effects_loader_v2) {
  SetNextSchedTimeMono(async::Now(mix_domain().dispatcher()));
}

void AudioOutput::Process() {
  auto mono_now = async::Now(mix_domain().dispatcher());
  int64_t trace_wake_delta =
      next_sched_time_mono_.has_value() ? (mono_now - next_sched_time_mono_.value()).get() : 0;
  TRACE_DURATION("audio", "AudioOutput::Process", "wake delta", TA_INT64(trace_wake_delta));

  FX_DCHECK(pipeline_);

  // At this point, we should always know when our implementation would like to be called to do some
  // mixing work next. If we do not know, then we should have already shut down.
  //
  // If the next sched time has not arrived yet, don't attempt to mix anything. Just trim the queues
  // and move on.
  FX_DCHECK(next_sched_time_mono_);
  if (mono_now >= next_sched_time_mono_.value()) {
    // Clear the flag. If the implementation does not set it during the cycle by calling
    // SetNextSchedTimeMono, we consider it an error and shut down.
    ClearNextSchedTime();
    auto ref_now = reference_clock()->ReferenceTimeFromMonotonicTime(mono_now);

    ReadableStream::ReadLockContext ctx;
    StageMetricsTimer timer("AudioOutput::Process");
    timer.Start();

    if (auto mix_frames = StartMixJob(ref_now); mix_frames) {
      ProcessMixJob(ctx, *mix_frames);
      FinishMixJob(*mix_frames);
    } else {
      pipeline_->Trim(
          Fixed::FromRaw(driver_ref_time_to_frac_safe_read_or_write_frame().Apply(ref_now.get())));
    }

    auto mono_end = async::Now(mix_domain().dispatcher());
    if (auto dt = mono_end - mono_now; dt > MixDeadline()) {
      timer.Stop();
      TRACE_INSTANT("audio", "AudioOutput::MIX_UNDERFLOW", TRACE_SCOPE_THREAD);
      TRACE_ALERT("audio", "audiounderflow");

      std::ostringstream os;
      DumpStageMetrics(os, timer.Metrics());
      for (auto& metrics : ctx.per_stage_metrics()) {
        DumpStageMetrics(os, metrics);
      }

      FX_LOGS(ERROR) << "PIPELINE UNDERFLOW: Mixer ran for " << std::setprecision(4)
                     << static_cast<double>(dt.to_nsecs()) / ZX_MSEC(1) << " ms, overran goal of "
                     << static_cast<double>(MixDeadline().to_nsecs()) / ZX_MSEC(1)
                     << " ms. Detailed metrics:\n"
                     << os.str();

      reporter().PipelineUnderflow(mono_now + MixDeadline(), mono_end);
    }
  }

  if (!next_sched_time_mono_) {
    FX_LOGS(ERROR) << "Output failed to schedule next service time. Shutting down!";
    ShutdownSelf();
    return;
  }

  // Figure out when we should wake up to do more work again. No matter how long our implementation
  // wants to wait, we need to make sure to wake up and periodically trim our input queues.
  auto max_sched_time_mono = mono_now + kMaxTrimPeriod;
  if (next_sched_time_mono_.value() > max_sched_time_mono) {
    SetNextSchedTimeMono(max_sched_time_mono);
  }
  zx_status_t status =
      mix_timer_.PostForTime(mix_domain().dispatcher(), next_sched_time_mono_.value());
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to schedule mix";
    ShutdownSelf();
  }
}

void AudioOutput::ProcessMixJob(ReadableStream::ReadLockContext& ctx, FrameSpan mix_span) {
  // If the span is muted, the output is muted, so we can write silence and trim the pipeline.
  if (mix_span.is_mute) {
    WriteMixOutput(mix_span.start, mix_span.length, nullptr);
    pipeline_->Trim(Fixed(mix_span.start + mix_span.length));
    return;
  }

  while (mix_span.length > 0) {
    auto buf = pipeline_->ReadLock(ctx, Fixed(mix_span.start), mix_span.length);
    if (!buf) {
      // The pipeline has no data for this range, so write silence.
      WriteMixOutput(mix_span.start, mix_span.length, nullptr);
      return;
    }

    // Although the ReadLock API allows it, in practice an OutputPipeline pipeline should never
    // return a buffer with a fractional start frame.
    FX_CHECK(buf->start().Fraction() == Fixed(0));
    int64_t buf_start = buf->start().Floor();

    // Write silence before the buffer, if any.
    if (int64_t gap = buf_start - mix_span.start; gap > 0) {
      WriteMixOutput(mix_span.start, gap, nullptr);
    }

    // Write the buffer. OutputPipelines always produce float samples.
    WriteMixOutput(buf_start, buf->length(), reinterpret_cast<float*>(buf->payload()));

    // ReadLock is not required to return the full range.
    int64_t frames_advanced = (buf_start + buf->length()) - mix_span.start;
    mix_span.start += frames_advanced;
    mix_span.length -= frames_advanced;
  }
}

fpromise::result<std::pair<std::shared_ptr<Mixer>, ExecutionDomain*>, zx_status_t>
AudioOutput::InitializeSourceLink(const AudioObject& source,
                                  std::shared_ptr<ReadableStream> source_stream) {
  TRACE_DURATION("audio", "AudioOutput::InitializeSourceLink");

  // If there's no source, use a Mixer that only trims, and no execution domain.
  if (!source_stream) {
    return fpromise::ok(std::make_pair(std::make_shared<audio::mixer::NoOp>(), nullptr));
  }

  auto usage = source.usage();
  FX_DCHECK(usage) << "Source has no assigned usage";
  if (!usage) {
    usage = {StreamUsage::WithRenderUsage(RenderUsage::MEDIA)};
  }

  // For now, at least one clock should be unadjustable.
  FX_CHECK(!source_stream->reference_clock()->adjustable() || !reference_clock()->adjustable());

  auto mixer = pipeline_->AddInput(std::move(source_stream), *usage);
  return fpromise::ok(std::make_pair(std::move(mixer), &mix_domain()));
}

void AudioOutput::CleanupSourceLink(const AudioObject& source,
                                    std::shared_ptr<ReadableStream> source_stream) {
  TRACE_DURATION("audio", "AudioOutput::CleanupSourceLink");
  if (source_stream) {
    pipeline_->RemoveInput(*source_stream);
  }
}

fpromise::result<std::shared_ptr<ReadableStream>, zx_status_t> AudioOutput::InitializeDestLink(
    const AudioObject& dest) {
  TRACE_DURATION("audio", "AudioOutput::InitializeDestLink");
  if (!pipeline_) {
    return fpromise::error(ZX_ERR_BAD_STATE);
  }
  // Ring buffers can be read concurrently by multiple streams, while each ReadableRingBuffer
  // object contains state for a single stream. Hence, create a duplicate object for each
  // destination link.
  return fpromise::ok(pipeline_->dup_loopback());
}

std::shared_ptr<OutputPipeline> AudioOutput::CreateOutputPipeline(
    const PipelineConfig& config, const VolumeCurve& volume_curve, size_t max_block_size_frames,
    TimelineFunction device_reference_clock_to_fractional_frame, std::shared_ptr<Clock> ref_clock) {
  auto pipeline = std::make_unique<OutputPipelineImpl>(
      config, volume_curve, effects_loader_v2_, max_block_size_frames,
      device_reference_clock_to_fractional_frame, ref_clock);
  pipeline->SetPresentationDelay(presentation_delay());
  return pipeline;
}

void AudioOutput::SetupMixTask(const DeviceConfig::OutputDeviceProfile& profile,
                               size_t max_block_size_frames,
                               TimelineFunction device_reference_clock_to_fractional_frame) {
  DeviceConfig updated_config = config();
  updated_config.SetOutputDeviceProfile(driver()->persistent_unique_id(), profile);
  set_config(updated_config);

  max_block_size_frames_ = max_block_size_frames;
  pipeline_ =
      CreateOutputPipeline(profile.pipeline_config(), profile.volume_curve(), max_block_size_frames,
                           device_reference_clock_to_fractional_frame, reference_clock());

  // OutputPipelines must always produce float samples.
  FX_CHECK(pipeline_->format().sample_format() == fuchsia::media::AudioSampleFormat::FLOAT);

  // In case the pipeline needs shared libraries, ensure those are paged in.
  PinExecutableMemory::Singleton().Pin();
}

void AudioOutput::Cleanup() {
  AudioDevice::Cleanup();
  mix_timer_.Cancel();
}

fpromise::promise<void, fuchsia::media::audio::UpdateEffectError> AudioOutput::UpdateEffect(
    const std::string& instance_name, const std::string& config) {
  fpromise::bridge<void, fuchsia::media::audio::UpdateEffectError> bridge;
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

fpromise::promise<void, zx_status_t> AudioOutput::UpdateDeviceProfile(
    const DeviceConfig::OutputDeviceProfile::Parameters& params) {
  fpromise::bridge<void, zx_status_t> bridge;
  mix_domain().PostTask([this, params, completer = std::move(bridge.completer)]() mutable {
    OBTAIN_EXECUTION_DOMAIN_TOKEN(token, &mix_domain());
    DeviceConfig device_config = config();
    auto current_profile = config().output_device_profile(driver()->persistent_unique_id());
    auto updated_profile = DeviceConfig::OutputDeviceProfile(
        params.eligible_for_loopback.value_or(current_profile.eligible_for_loopback()),
        params.supported_usages.value_or(current_profile.supported_usages()),
        params.volume_curve.value_or(current_profile.volume_curve()),
        params.independent_volume_control.value_or(current_profile.independent_volume_control()),
        params.pipeline_config.value_or(current_profile.pipeline_config()),
        params.driver_gain_db.value_or(current_profile.driver_gain_db()),
        params.software_gain_db.value_or(current_profile.software_gain_db()));
    device_config.SetOutputDeviceProfile(driver()->persistent_unique_id(), updated_profile);
    set_config(device_config);

    auto snapshot = pipeline_->ref_time_to_frac_presentation_frame();
    pipeline_ =
        CreateOutputPipeline(updated_profile.pipeline_config(), updated_profile.volume_curve(),
                             max_block_size_frames_, snapshot.timeline_function, reference_clock());
    FX_DCHECK(pipeline_);
    completer.complete_ok();
  });
  return bridge.consumer.promise();
}

void AudioOutput::SetGainInfo(const fuchsia::media::AudioGainInfo& info,
                              fuchsia::media::AudioGainValidFlags set_flags) {
  reporter_->SetGainInfo(info, set_flags);
  AudioDevice::SetGainInfo(info, set_flags);
}

}  // namespace media::audio
