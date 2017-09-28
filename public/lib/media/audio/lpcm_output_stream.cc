// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/media/audio/lpcm_output_stream.h"

#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/logging.h"
#include "lib/media/fidl/media_service.fidl.h"
#include "lib/media/timeline/fidl_type_conversions.h"
#include "lib/media/timeline/timeline.h"
#include "lib/media/timeline/timeline_rate.h"

namespace media {

// static
fxl::RefPtr<LpcmOutputStream> LpcmOutputStream::Create(
    app::ApplicationContext* application_context,
    AudioSampleFormat sample_format,
    uint32_t channel_count,
    uint32_t frames_per_second,
    size_t payload_pool_frames) {
  auto result = fxl::AdoptRef(
      new LpcmOutputStream(application_context, sample_format, channel_count,
                           frames_per_second, payload_pool_frames));
  result->Init();
  return result;
}

LpcmOutputStream::LpcmOutputStream(app::ApplicationContext* application_context,
                                   AudioSampleFormat sample_format,
                                   uint32_t channel_count,
                                   uint32_t frames_per_second,
                                   size_t payload_pool_frames)
    : LpcmPayload::Owner(sample_format, channel_count),
      task_runner_(fsl::MessageLoop::GetCurrent()->task_runner()),
      frames_per_second_(frames_per_second),
      payload_pool_frames_(payload_pool_frames),
      producer_(this) {
  FXL_DCHECK(application_context);
  FXL_DCHECK(sample_format != AudioSampleFormat::NONE);
  FXL_DCHECK(sample_format != AudioSampleFormat::ANY);
  FXL_DCHECK(channel_count >= kMinLpcmChannelCount &&
             channel_count <= kMaxLpcmChannelCount)
      << "channel_count out of range";
  FXL_DCHECK(frames_per_second >= kMinLpcmFramesPerSecond &&
             frames_per_second <= kMaxLpcmFramesPerSecond)
      << "frames_per_second out of range";
  FXL_DCHECK(payload_pool_frames > 0);

  FXL_DCHECK(task_runner_);

  // TODO(dalesat): Remove this when float is supported.
  FXL_DCHECK(sample_format != AudioSampleFormat::FLOAT)
      << "Float sample format is not currently supported by the audio "
         "service";

  producer_.SetFixedBufferSize(payload_pool_frames_ * bytes_per_frame());

  ns_to_frames_ = TimelineFunction(TimelineRate(frames_per_second_) /
                                   TimelineRate::NsPerSecond);

  auto media_service =
      application_context->ConnectToEnvironmentService<MediaService>();

  media_service->CreateAudioRenderer(audio_renderer_.NewRequest(),
                                     media_renderer_.NewRequest());
  audio_renderer_.set_connection_error_handler([this]() {
    audio_renderer_.reset();
    SetError(MediaResult::CONNECTION_LOST);
  });
  media_renderer_.set_connection_error_handler([this]() {
    media_renderer_.reset();
    SetError(MediaResult::CONNECTION_LOST);
  });

  media_renderer_->GetTimelineControlPoint(
      timeline_control_point_.NewRequest());
  timeline_control_point_.set_connection_error_handler([this]() {
    timeline_control_point_.reset();
    SetError(MediaResult::CONNECTION_LOST);
  });

  timeline_control_point_->GetTimelineConsumer(timeline_consumer_.NewRequest());
  timeline_consumer_.set_connection_error_handler([this]() {
    timeline_consumer_.reset();
    SetError(MediaResult::CONNECTION_LOST);
  });

  media_renderer_->SetMediaType(
      CreateLpcmMediaType(sample_format, channel_count, frames_per_second));
}

LpcmOutputStream::~LpcmOutputStream() {
  FXL_DCHECK_CREATION_THREAD_IS_CURRENT(thread_checker_);
  if (is_valid()) {
    Reset();
  }
}

void LpcmOutputStream::Init() {
  MediaPacketConsumerPtr packet_consumer;
  media_renderer_->GetPacketConsumer(packet_consumer.NewRequest());

  HandleStatusUpdates();

  producer_.Connect(MediaPacketConsumerPtr::Create(std::move(packet_consumer)),
                    [this_ptr = ref_ptr()]() {
                      if (!this_ptr->is_valid()) {
                        return;
                      }

                      this_ptr->ready_ = true;
                      if (!this_ptr->sends_pending_ready_.empty()) {
                        this_ptr->Start();
                      }
                    });
}

void LpcmOutputStream::Reset() {
  FXL_DCHECK_CREATION_THREAD_IS_CURRENT(thread_checker_);
  FXL_DCHECK(is_valid());

  audio_renderer_.reset();
  media_renderer_.reset();
  timeline_control_point_.reset();
  timeline_consumer_.reset();

  LpcmPayload::Owner::Reset();
  frames_per_second_ = 0;
  local_to_presentation_frames_ = TimelineFunction();
  next_send_pts_ = 0;
  next_return_pts_ = 0;
  slippage_frames_ = 0;

  error_ = MediaResult::BAD_STATE;
  error_handler_ = nullptr;
  started_ = false;
  end_method_called_ = false;
  sends_pending_ready_.clear();
  send_task_ = nullptr;
  send_task_lead_time_ns_ = 0;
  send_task_payload_frames_available_ = 0;

  gain_db_ = 0.0f;
}

void LpcmOutputStream::OnError(fxl::Closure handler) {
  FXL_DCHECK_CREATION_THREAD_IS_CURRENT(thread_checker_);
  FXL_DCHECK(is_valid());

  error_handler_ = handler;

  if (error_handler_ && error_ != MediaResult::OK) {
    task_runner_->PostTask([this_ptr = ref_ptr()]() {
      if (this_ptr->error_handler_) {
        this_ptr->error_handler_();
      }
    });
  }
}

LpcmPayload LpcmOutputStream::CreatePayload(size_t frame_count) {
  FXL_DCHECK_CREATION_THREAD_IS_CURRENT(thread_checker_);
  FXL_DCHECK(frame_count > 0);
  FXL_DCHECK(is_valid());

  if (error_ != MediaResult::OK) {
    return LpcmPayload();
  }

  size_t size = frame_count * bytes_per_frame();

  void* buffer = producer_.AllocatePayloadBuffer(size);
  if (buffer == nullptr) {
    return LpcmPayload();
  }

  frames_currently_allocated_ += frame_count;

  return LpcmPayload(buffer, size, fxl::RefPtr<LpcmPayload::Owner>(this));
}

void LpcmOutputStream::Send(LpcmPayload payload) {
  FXL_DCHECK_CREATION_THREAD_IS_CURRENT(thread_checker_);
  FXL_DCHECK(payload);
  FXL_DCHECK(payload.owner_.get() == this);
  FXL_DCHECK(is_valid());

  if (error_ != MediaResult::OK) {
    return;
  }

  if (end_method_called_) {
    Restart();
  }

  SendInternal(std::move(payload), false);
}

bool LpcmOutputStream::CopyAndSend(const void* source, size_t size_in_bytes) {
  FXL_DCHECK_CREATION_THREAD_IS_CURRENT(thread_checker_);
  FXL_DCHECK(source);
  FXL_DCHECK(size_in_bytes > 0 && size_in_bytes % bytes_per_frame() == 0);
  FXL_DCHECK(is_valid());

  if (error_ != MediaResult::OK) {
    return false;
  }

  LpcmPayload payload = CreatePayload(size_in_bytes / bytes_per_frame());
  if (!payload) {
    return false;
  }

  std::memcpy(payload.data(), source, size_in_bytes);

  SendInternal(std::move(payload), false);

  return true;
}

fxl::TimePoint LpcmOutputStream::PresentationTimeOfNextSend() {
  FXL_DCHECK_CREATION_THREAD_IS_CURRENT(thread_checker_);

  if (end_method_called_ || !local_to_presentation_frames_.invertable()) {
    return fxl::TimePoint();
  } else {
    MaybeSlipPts();
    return Timeline::to_time_point(
        local_to_presentation_frames_.ApplyInverse(next_send_pts_));
  }
}

bool LpcmOutputStream::ShouldSendNow(fxl::TimeDelta lead_time) const {
  FXL_DCHECK_CREATION_THREAD_IS_CURRENT(thread_checker_);
  FXL_DCHECK(lead_time != fxl::TimeDelta());

  return error_ == MediaResult::OK &&
         !LeadTimeSatisfied(Timeline::delta_from(lead_time));
}

void LpcmOutputStream::PostTaskWhenPayloadFramesAvailable(
    fxl::Closure task,
    size_t payload_frames_available) {
  FXL_DCHECK_CREATION_THREAD_IS_CURRENT(thread_checker_);
  FXL_DCHECK(task);
  FXL_DCHECK(payload_frames_available > 0);
  FXL_DCHECK(is_valid());

  if (error_ != MediaResult::OK) {
    return;
  }

  send_task_ = task;
  send_task_lead_time_ns_ = 0;
  send_task_payload_frames_available_ = payload_frames_available;

  if (frames_currently_available() >= send_task_payload_frames_available_) {
    // The requested |payload_frames_available| is already available, so we run
    // the task immediately.
    task_runner_->PostTask([this_ptr = ref_ptr()]() {
      this_ptr->MaybeCallSendTask();
    });
  }
}

void LpcmOutputStream::PostTaskBeforeDeadline(fxl::Closure task,
                                              fxl::TimeDelta lead_time) {
  FXL_DCHECK_CREATION_THREAD_IS_CURRENT(thread_checker_);
  FXL_DCHECK(task);
  FXL_DCHECK(lead_time != fxl::TimeDelta());
  FXL_DCHECK(is_valid());

  if (error_ != MediaResult::OK) {
    return;
  }

  send_task_ = task;
  send_task_lead_time_ns_ = Timeline::delta_from(lead_time);
  send_task_payload_frames_available_ = 0;

  if (local_to_presentation_frames_.invertable()) {
    // We know how presentation time relates to system time, so we know how
    // to schedule the task.
    PostTaskBeforeDeadlineInternal();
  } else if (!LeadTimeSatisfied(send_task_lead_time_ns_)) {
    // There isn't enough content outstanding at the renderer to cover the
    // minimum latency and the lead time, so we run the task immediately.
    task_runner_->PostTask([this_ptr = ref_ptr()]() {
      this_ptr->MaybeCallSendTask();
    });
  } else {
    // There's enough content outstanding at the renderer to cover the
    // minimum latency and the lead time. We don't currently know when to
    // run the task, so we'll revisit this calculation when a packet is
    // returned or we find out how presentation time relates to system time.
  }
}

void LpcmOutputStream::CancelPostedTask() {
  FXL_DCHECK_CREATION_THREAD_IS_CURRENT(thread_checker_);
  send_task_ = nullptr;
  send_task_lead_time_ns_ = 0;
  send_task_payload_frames_available_ = 0;
}

void LpcmOutputStream::End(fxl::Closure complete) {
  FXL_DCHECK_CREATION_THREAD_IS_CURRENT(thread_checker_);
  FXL_DCHECK(is_valid());
  FXL_DCHECK(!end_method_called_);

  if (error_ != MediaResult::OK) {
    return;
  }

  end_method_called_ = true;
  end_callback_ = complete;

  SendInternal(nullptr, true);
}

void LpcmOutputStream::SetGain(float gain_db) {
  FXL_DCHECK_CREATION_THREAD_IS_CURRENT(thread_checker_);
  FXL_DCHECK(gain_db <= AudioRenderer::kMaxGain);
  FXL_DCHECK(is_valid());

  if (error_ != MediaResult::OK) {
    return;
  }

  gain_db_ = gain_db;
  audio_renderer_->SetGain(gain_db);
}

void LpcmOutputStream::SetMute(bool mute) {
  FXL_DCHECK_CREATION_THREAD_IS_CURRENT(thread_checker_);
  FXL_DCHECK(is_valid());

  if (error_ != MediaResult::OK) {
    return;
  }

  audio_renderer_->SetGain(mute ? AudioRenderer::kMutedGain : gain_db_);
}

void LpcmOutputStream::SetTimelineTransform(
    const TimelineTransformPtr& timeline_transform) {
  FXL_DCHECK(timeline_transform);

  if (started_) {
    local_to_presentation_frames_ = TimelineFunction();
    timeline_consumer_->SetTimelineTransform(timeline_transform.Clone(),
                                             [](bool completed) {});
  } else {
    Start(timeline_transform.To<TimelineFunction>());
  }
}

void LpcmOutputStream::Start(const TimelineFunction& timeline) {
  FXL_DCHECK_CREATION_THREAD_IS_CURRENT(thread_checker_);
  FXL_DCHECK(is_valid());
  FXL_DCHECK(!started_);
  FXL_DCHECK(local_to_presentation_frames_ == TimelineFunction());

  started_ = true;

  for (auto& pending_send : sends_pending_ready_) {
    size_t size = pending_send.payload_.size();
    SendInternalStarted(pending_send.payload_.release(), size,
                        pending_send.pts_, pending_send.end_of_stream_);
  }

  sends_pending_ready_.clear();

  timeline_consumer_->SetTimelineTransform(TimelineTransform::From(timeline),
                                           [](bool completed) {});
}

void LpcmOutputStream::Restart() {
  FXL_DCHECK_CREATION_THREAD_IS_CURRENT(thread_checker_);
  FXL_DCHECK(is_valid());

  started_ = false;
  end_method_called_ = false;

  TimelineTransformPtr timeline_transform = media::TimelineTransform::New();

  timeline_transform->reference_time = kUnspecifiedTime;
  timeline_transform->subject_time = kUnspecifiedTime;
  timeline_transform->reference_delta = 0;
  timeline_transform->subject_delta = 1;

  timeline_consumer_->SetTimelineTransform(std::move(timeline_transform),
                                           [](bool completed) {});

  local_to_presentation_frames_ = TimelineFunction();

  producer_.FlushConsumer(false, [this]() {
    next_send_pts_ = 0;
    next_return_pts_ = 0;
    slippage_frames_ = 0;
  });
}

void LpcmOutputStream::SetError(MediaResult error) {
  FXL_DCHECK_CREATION_THREAD_IS_CURRENT(thread_checker_);

  if (error_ == error) {
    return;
  }

  // Make sure we don't call |send_task_|.
  send_task_ = nullptr;
  send_task_lead_time_ns_ = 0;
  send_task_payload_frames_available_ = 0;

  error_ = error;

  if (error_handler_) {
    error_handler_();
  }
}

void LpcmOutputStream::SendInternal(LpcmPayload payload, bool end_of_stream) {
  FXL_DCHECK_CREATION_THREAD_IS_CURRENT(thread_checker_);
  FXL_DCHECK(ready_);

  size_t frame_count = payload.frame_count();

  if (!ready_) {
    sends_pending_ready_.emplace_back(std::move(payload), next_send_pts_,
                                      end_of_stream);
    next_send_pts_ += frame_count;
    return;
  }

  if (!started_) {
    Start();
  }

  MaybeSlipPts();
  size_t size = payload.size();
  SendInternalStarted(payload.release(), size, next_send_pts_, end_of_stream);
  next_send_pts_ += frame_count;
}

void LpcmOutputStream::MaybeSlipPts() {
  if (drop_frames_when_starving_ ||
      local_to_presentation_frames_.subject_time() == kUnspecifiedTime ||
      local_to_presentation_frames_.reference_time() == kUnspecifiedTime) {
    return;
  }

  int64_t pts_nowish =
      local_to_presentation_frames_(Timeline::local_now() + min_latency_ns_);
  if (next_send_pts_ >= pts_nowish) {
    return;
  }

  slippage_frames_ += pts_nowish - next_send_pts_;
  next_send_pts_ = pts_nowish;
}

void LpcmOutputStream::SendInternalStarted(void* buffer,
                                           size_t size,
                                           int64_t pts,
                                           bool end_of_stream) {
  FXL_DCHECK_CREATION_THREAD_IS_CURRENT(thread_checker_);
  FXL_DCHECK((buffer == nullptr) == (size == 0));
  FXL_DCHECK(size % bytes_per_frame() == 0);

  if (!is_valid() || error_ != MediaResult::OK) {
    if (buffer) {
      ReleasePayloadBuffer(buffer, size);
    }

    return;
  }

  FXL_DCHECK(started_);

  producer_.ProducePacket(buffer, size, pts,
                          TimelineRate(frames_per_second_, 1), false,
                          end_of_stream, nullptr, [this, buffer, size]() {
                            ReleasePayloadBuffer(buffer, size);
                            next_return_pts_ += size / bytes_per_frame();
                            MaybeCallSendTask();
                          });
}

void LpcmOutputStream::MaybeCallSendTask() {
  FXL_DCHECK_CREATION_THREAD_IS_CURRENT(thread_checker_);

  if (!send_task_) {
    return;
  }

  // Only one of these two should be non-zero if |send_task_|.
  FXL_DCHECK((send_task_payload_frames_available_ == 0) !=
             (send_task_lead_time_ns_ == 0));

  // |send_task_| should be null if there's an error.
  FXL_DCHECK(error_ == MediaResult::OK);

  if ((send_task_payload_frames_available_ != 0 &&
       frames_currently_available() >= send_task_payload_frames_available_) ||
      !LeadTimeSatisfied(send_task_lead_time_ns_)) {
    send_task_payload_frames_available_ = 0;
    send_task_lead_time_ns_ = 0;

    fxl::Closure task = std::move(send_task_);
    // std::move above doesn't actually do anything, so we have to clear
    // |send_task_| explicitly.
    send_task_ = nullptr;
    task();
  }
}

bool LpcmOutputStream::LeadTimeSatisfied(int64_t lead_time_ns) const {
  // TODO(dalesat): This calculation doesn't work for non-1/1 rate.
  return ns_to_frames_.ApplyInverse(next_send_pts_ - next_return_pts_) >
         lead_time_ns + min_latency_ns_;
}

void LpcmOutputStream::PostTaskBeforeDeadlineInternal() {
  FXL_DCHECK(send_task_);
  FXL_DCHECK(local_to_presentation_frames_.invertable());

  fxl::TimePoint when = Timeline::to_time_point(
      local_to_presentation_frames_.ApplyInverse(next_send_pts_) -
      min_latency_ns_ - send_task_lead_time_ns_);

  // TODO(dalesat): This could keep 'this' alive for a long time. Maybe limit
  // send_task_lead_time_ns_?
  task_runner_
      ->PostTaskForTime([this_ptr =
                             ref_ptr()]() { this_ptr->MaybeCallSendTask(); },
                        when);
}

void LpcmOutputStream::HandleStatusUpdates(
    uint64_t version,
    MediaTimelineControlPointStatusPtr status) {
  FXL_DCHECK_CREATION_THREAD_IS_CURRENT(thread_checker_);

  if (status) {
    // Process status received from the control point.
    bool need_to_post_task_before_deadline =
        send_task_lead_time_ns_ != 0 &&
        !local_to_presentation_frames_.invertable();

    local_to_presentation_frames_ =
        ns_to_frames_ * status->timeline_transform.To<TimelineFunction>();

    if (need_to_post_task_before_deadline &&
        local_to_presentation_frames_.invertable()) {
      // A previous call to |PostTaskBeforeDeadline| needed a valid
      // |local_to_presentation_frames_| and didn't have it. Schedule the task
      // now.
      FXL_DCHECK(send_task_);
      PostTaskBeforeDeadlineInternal();
    }

    if (status->end_of_stream && end_callback_) {
      end_callback_();
      end_callback_ = nullptr;
    }
  }

  if (!timeline_control_point_) {
    return;
  }

  // Request a status update.
  timeline_control_point_->GetStatus(
      version,
      [this](uint64_t version, MediaTimelineControlPointStatusPtr status) {
        HandleStatusUpdates(version, std::move(status));
      });
}

LpcmOutputStream::Producer::Producer(LpcmOutputStream* owner) : owner_(owner) {
  FXL_DCHECK(owner_);
}

LpcmOutputStream::Producer::~Producer() {}

void LpcmOutputStream::Producer::OnDemandUpdated(
    uint32_t min_packets_outstanding,
    int64_t min_pts) {
  if (owner_) {
    owner_->MaybeCallSendTask();
  }
}

void LpcmOutputStream::Producer::OnFailure() {
  if (owner_) {
    owner_->SetError(MediaResult::CONNECTION_LOST);
  }
}

void LpcmOutputStream::ReleasePayloadBuffer(void* buffer, size_t size) {
  FXL_DCHECK(buffer);
  producer_.ReleasePayloadBuffer(buffer);

  if (is_valid()) {
    FXL_DCHECK(frames_currently_allocated_ >= size / bytes_per_frame());
    frames_currently_allocated_ -= size / bytes_per_frame();
  }
}

}  // namespace media
