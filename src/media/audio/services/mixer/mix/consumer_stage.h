// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_SERVICES_MIXER_MIX_CONSUMER_STAGE_H_
#define SRC_MEDIA_AUDIO_SERVICES_MIXER_MIX_CONSUMER_STAGE_H_

#include <fidl/fuchsia.audio.mixer/cpp/common_types.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>

#include <optional>
#include <variant>

#include "src/media/audio/services/common/logging.h"
#include "src/media/audio/services/mixer/common/atomic_optional.h"
#include "src/media/audio/services/mixer/common/basic_types.h"
#include "src/media/audio/services/mixer/mix/base_consumer_stage.h"
#include "src/media/audio/services/mixer/mix/packet_view.h"
#include "src/media/audio/services/mixer/mix/start_stop_control.h"

namespace media_audio {

// A consumer that can run mix jobs.
class ConsumerStage : public BaseConsumerStage {
 public:
  using StartCommand = StartStopControl::StartCommand;
  using StopCommand = StartStopControl::StopCommand;
  using PendingStartStopCommand = AtomicOptional<StartStopControl::Command>;

  struct Args {
    // Name of this stage.
    std::string_view name;

    // Whether this ConsumerStage participates in an input pipeline or an output pipeline.
    PipelineDirection pipeline_direction;

    // Format of audio consumed by this stage.
    Format format;

    // Reference clock used by this consumer.
    UnreadableClock reference_clock;

    // Ticks of media time per nanoseconds of reference time.
    TimelineRate media_ticks_per_ns;

    // Slot to hold a pending start/stop command.
    std::shared_ptr<PendingStartStopCommand> pending_start_stop_command;

    // How to write all consumed packets.
    std::shared_ptr<Writer> writer;
  };

  explicit ConsumerStage(Args args);

  // Returned by RunMixJob to signal that the consumer is still running at the end of the mix job.
  // Caller should continuously run mix jobs until receiving StoppedStatus.
  struct StartedStatus {};

  // Returned by RunMixJob to signal that the consumer is stopped at the end of the mix job. Caller
  // does not need to run another mix job until the reference clock reads `next_mix_job_start_time`.
  struct StoppedStatus {
    // If specified, this is the time when the consumer is scheduled to start again. The consumer
    // is guaranteed to return StoppedStatus for all calls `RunMixJob(ctx, start, period)` where
    // `start + period < next_mix_job_start_time`. If not specified, the next start time is unknown.
    //
    // TODO(fxbug.dev/87651): The above guarantee may not hold preciely if the delay is dynamically
    // changing
    std::optional<zx::time> next_mix_job_start_time;
  };

  using Status = std::variant<StartedStatus, StoppedStatus>;

  // Executes a single mix job that consumes `period` worth of audio data. This method should be
  // called at some time during range `R = [mix_job_start_time, mix_job_start_time + period)`,
  // relative to our reference clock, with the expectation that RunMixJob should complete before
  // `R.end`. This expectation is not checked -- it's the caller's responsibility to check for
  // overruns and underflows as desired.
  //
  // Put differently, within RunMixJob, all calls to `ref_clock->now()` should return a value in
  // range `R`. The range `R` bounds the "actual current time" according to this consumer's
  // reference clock, not the presentation times of audio frames to consume. As described below, the
  // mix job actually consumes frames presented outside of range `R` -- either after `R` (in output
  // pipelines) or before `R` (in input pipelines).
  //
  // TODO(fxbug.dev/87651): Add a discussion of "safe write range" and "safe read range" to
  // ../docs/delay.md and reference that here.
  //
  // ## Output pipelines
  //
  // Since this job may run until `mix_job_start_time + period`, we cannot consume any frames that
  // will be presented before `T = mix_job_start_time + period + downstream_delay`. Hence, we
  // consume frames in the range `[T, T + period)`.
  //
  // ## Input pipelines
  //
  // Since this job starts running as early as `mix_job_start_time`, we cannot consume any frames
  // that are presented after `T = mix_job_start_time - upstream_delay`. Hence, we consume frames in
  // the range `[T - period, T)`.
  Status RunMixJob(MixJobContext& ctx, zx::time mix_job_start_time, zx::duration period);

  // Sets the delay introduced by our downstream external output device, such as a physical speaker.
  // This is Node::max_downstream_output_pipeline_delay().
  //
  // REQUIRED: must have `pipeline_direction == kOutput`
  void set_downstream_delay(zx::duration delay);

  // Sets the delay introduced by our source input pipeline, not including any delays introduced by
  // this ConsumerStage. This is Node::max_upstream_input_pipeline_delay() in our *source* node.
  //
  // REQUIRED: must have `pipeline_direction == kInput`
  void set_upstream_delay_for_source(zx::duration delay);

 private:
  void UpdateStatus(const MixJobContext& ctx, zx::time mix_job_current_presentation_time);

  // For output pipelines, this reports the presentation delay downstream of this consumer.
  zx::duration downstream_delay() const {
    FX_CHECK(pipeline_direction_ == PipelineDirection::kOutput);
    return presentation_delay_;
  }

  // For input pipelines, this reports the presentation delay upstream of this consumer's source.
  zx::duration upstream_delay_for_source() const {
    FX_CHECK(pipeline_direction_ == PipelineDirection::kInput);
    return presentation_delay_;
  }

  const PipelineDirection pipeline_direction_;
  const std::shared_ptr<Writer> writer_;  // how to write consumed packets
  const std::shared_ptr<PendingStartStopCommand> pending_start_stop_command_;
  StartStopControl start_stop_control_;

  // Downstream or upstream delay, depending on `pipeline_direction_`.
  zx::duration presentation_delay_;

  // The last `mix_job_start_time + period` passed to RunMixJob.
  std::optional<zx::time> last_mix_job_end_time_;

  // Current status: either started or stopped, with the (reference clock) presentation time of the
  // next transition to a different state.
  struct InternalStartedStatus {
    std::optional<zx::time> next_stop_presentation_time;
  };
  struct InternalStoppedStatus {
    std::optional<zx::time> next_start_presentation_time;
  };
  using InternalStatus = std::variant<InternalStartedStatus, InternalStoppedStatus>;
  static Status ToStatus(const InternalStatus& internal_status, zx::duration consume_offset);
  InternalStatus internal_status_ = InternalStoppedStatus{};
};

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_SERVICES_MIXER_MIX_CONSUMER_STAGE_H_
