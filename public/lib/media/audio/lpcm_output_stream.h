// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "lib/fxl/functional/closure.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/memory/ref_counted.h"
#include "lib/fxl/memory/ref_ptr.h"
#include "lib/fxl/synchronization/thread_checker.h"
#include "lib/fxl/tasks/task_runner.h"
#include "lib/fxl/time/time_point.h"
#include "lib/media/audio/lpcm_payload.h"
#include "lib/media/audio/types.h"
#include "lib/media/fidl/audio_renderer.fidl.h"
#include "lib/media/fidl/media_renderer.fidl.h"
#include "lib/media/fidl/media_result.fidl.h"
#include "lib/media/fidl/timeline_controller.fidl.h"
#include "lib/media/timeline/timeline.h"
#include "lib/media/timeline/timeline_function.h"
#include "lib/media/transport/media_packet_producer_base.h"

namespace media {

// Streams LPCM audio to a renderer. This class must be created and called on
// a single thread that has a message loop.
// TODO(dalesat): There's currently no routing control.
class LpcmOutputStream : public LpcmPayload::Owner {
 public:
  // Creates a new |LpcmOutputStream| and returns a |RefPtr| to it.
  // |payload_pool_frames| specifies the number of frames in the pool from
  // which payloads are allocated (via |CreatePayload|).
  // TODO(dalesat): Replace |channel_count| with |channel_configuration|.
  static fxl::RefPtr<LpcmOutputStream> Create(
      app::ApplicationContext* application_context,
      AudioSampleFormat sample_format,
      uint32_t channel_count,
      uint32_t frames_per_second,
      size_t payload_pool_frames);

  // Destroys this |LpcmOutputStream|.
  ~LpcmOutputStream();

  // Shuts down this |LpcmOutputStream| immediately.
  void Reset() override;

  // Returns |MediaResult::OK| unless this |LpcmOutputStream| has been reset or
  // an error has occurred. If an error has occurred, this |LpcmAudioStream|
  // can no longer be used.
  // |BAD_STATE|: This |LpcmOutputStream| has been reset.
  // |CONNECTION_LOST|: The channel to the audio service closed unexpectedly.
  MediaResult error() const {
    FXL_DCHECK_CREATION_THREAD_IS_CURRENT(thread_checker_);
    return error_;
  }

  // Registers a handler to be called when an error occurs. Consult the |error|
  // method to determine the nature of the error. The error handler is cleared
  // when |Reset| is called.
  void OnError(fxl::Closure handler);

  // Returns the sample format value passed in the |Open| call.
  // Inherited from LpcmPayload::Owner.
  // AudioSampleFormat sample_format() const;

  // Returns the channel count value passed in the |Open| call.
  // Inherited from LpcmPayload::Owner.
  // uint32_t channel_count() const;

  // Returns the number of bytes occupied by a single sample.
  // Inherited from LpcmPayload::Owner.
  // uint32_t bytes_per_sample() const;

  // Returns the number of bytes occupied by a single frame.
  // Inherited from LpcmPayload::Owner.
  // uint32_t bytes_per_frame() const;

  // Returns the frames per second value passed in the |Open| call.
  uint32_t frames_per_second() const {
    FXL_DCHECK_CREATION_THREAD_IS_CURRENT(thread_checker_);
    return frames_per_second_;
  }

  // Allocates a payload that may be passed to |Send|. |frame_count| specifies
  // the size of the payload buffer in frames. Returns a null |LpcmPayload| if a
  // payload cannot be allocated, an error has occurred (see |error|) or this
  // |LpcmOutputStream| has been reset.
  //
  // If a payload cannot be allocated, this method will return a null payload.
  // In that case, |PostTaskWhenPayloadFramesAvailable| or
  // |PostTaskBeforeDeadline| can be used to wait until payload memory is
  // available.
  LpcmPayload CreatePayload(size_t frame_count);

  // Sends |payload|, which must have been allocated using |CreatePayload|.
  void Send(LpcmPayload payload);

  // Copies and sends |data|. |size_in_bytes| must be a multiple of
  // |bytes_per_frame()|. Returns true unless payload memory cannot be
  // allocated, an error has occurred (see |error|) or this |LpcmOutputStream|
  // has been reset.
  //
  // If a payload cannot be allocated, this method will return false. In that
  // case, |PostTaskWhenPayloadFramesAvailable| or |PostTaskBeforeDeadline| can
  // be used to wait until payload memory is available.
  bool CopyAndSend(const void* source, size_t size_in_bytes);

  // Returns the time at which the next payload sent via |Send| or |CopyAndSend|
  // will be presented. Returns |fxl::TimePoint()| when the timeline is not
  // progressing, in which case the presentation time isn't known. If
  // |DropFramesWhenStarving| is set to false (the default), and the renderer is
  // starving, this method assumes the next payload will be sent immediately.
  // TODO(dalesat):Should be able to set this. In frames? As TimePoint? Both?
  fxl::TimePoint PresentationTimeOfNextSend();

  // Posts a task to be run when the specified number of frames are available
  // in the payload pool. Only one task can be posted at a time. This applies to
  // tasks posted with this method and with |PostTaskBeforeDeadline|.
  void PostTaskWhenPayloadFramesAvailable(fxl::Closure task,
                                          size_t payload_frames_available);

  // Posts a task to be run before the time at which audio renderer will starve
  // for lack of sent payloads. |lead_time| specifies how much in advance of the
  // deadline the task will run. |lead_time| cannot be zero. Only one task can
  // be posted at a time. This applies to tasks posted with this method and
  // with |PostTaskWhenPayloadFramesAvailable|.
  void PostTaskBeforeDeadline(fxl::Closure task, fxl::TimeDelta lead_time);

  // Cancels any pending task previously posted using
  // |PostTaskWhenPayloadFramesAvailable| or |PostTaskBeforeDeadline|.
  void CancelPostedTask();

  // Determines whether more data is currently needed in order to cover
  // minimum latency and the specified lead time. |lead_time| cannot be zero.
  bool ShouldSendNow(fxl::TimeDelta lead_time) const;

  // Signals that the stream has ended. |complete| is called when the entire
  // stream has been rendered. Calling |Send| or |CopyAndSend| after calling
  // |End| restarts the stream. |complete| may be null.
  void End(fxl::Closure complete);

  // Sets the gain for this stream in decibels. |gain_db| must be at most
  // |AudioRenderer::kMaxGain|.
  void SetGain(float gain_db);

  // Controls whether this stream is muted.
  void SetMute(bool mute);

  // Sets the starvation behavior. If |drop_frames_when_starving| is false (the
  // default), all frames are played, and starvation causes a gap to be
  // inserted. For example, if 1 second of content is played,  but the renderer
  // starves for 0.1 seconds, the content will take 1.1 seconds to play, and all
  // of the content plus 0.1 seconds of silence will be heard. If
  // |drop_frames_when_starving| is true, starvation causes some frames to be
  // dropped. For example, if 1 second of content is played, but the renderer
  // starves for 0.1 seconds, the content will still take 1 second to play, and
  // 0.1 seconds of the content will be replaced by silence.
  void SetDropFramesWhenStarving(bool drop_frames_when_starving) {
    FXL_DCHECK_CREATION_THREAD_IS_CURRENT(thread_checker_);
    drop_frames_when_starving_ = drop_frames_when_starving;
  }

  // Returns the number of frames currently allocated to payloads.
  size_t frames_currently_allocated() {
    FXL_DCHECK_CREATION_THREAD_IS_CURRENT(thread_checker_);
    FXL_DCHECK(is_valid());
    return frames_currently_allocated_;
  }

  // Returns the number of frames current available for creating payloads.
  size_t frames_currently_available() {
    FXL_DCHECK_CREATION_THREAD_IS_CURRENT(thread_checker_);
    FXL_DCHECK(is_valid());
    return payload_pool_frames_ - frames_currently_allocated_;
  }

  // Returns the number of frames of silence that have been inserted due to
  // starvation. If |SetDropFramesWhenStarving| was set to true, this value
  // will always be zero.
  size_t silent_frames_inserted_when_starving() { return slippage_frames_; }

  // Sets the timeline transform. This method provides precise control over
  // the renderer timeline and is not needed for most applications. To apply
  // a transform when the stream is first starting, call this method before
  // calling |Send| or |CopyAndSend| for the first time. To apply a transform
  // when the stream is restarting after |End| is complete, call this method
  // after |End| completes and before calling |Send| or |CopyAndSend|.
  //
  // This method can be called while content is playing, but care must be taken
  // to do this properly. In general, the |subject_time| of the transform
  // should be |kUnspecifiedTime| to prevent discontinuous playback. Also keep
  // in mind that a previous call to |PostTaskBeforeDeadline| may need to be
  // performed again if the rate is increased.
  // TODO(dalesat): Pass TimelineFunction instead, once it includes the
  // kUnspecifiedTime concept and better Create methods.
  void SetTimelineTransform(const TimelineTransformPtr& timeline_transform);

 private:
  class Producer : public MediaPacketProducerBase {
   public:
    Producer(LpcmOutputStream* owner);

    ~Producer();

    void DetachFromOwner() { owner_ = nullptr; }

   protected:
    // MediaPacketProducerBase overrides.
    void OnDemandUpdated(uint32_t min_packets_outstanding,
                         int64_t min_pts) override;

    void OnFailure() override;

   private:
    LpcmOutputStream* owner_;
  };

  struct PendingSend {
    PendingSend(LpcmPayload payload, int64_t pts, bool end_of_stream)
        : payload_(std::move(payload)),
          pts_(pts),
          end_of_stream_(end_of_stream) {}

    LpcmPayload payload_;
    int64_t pts_;
    bool end_of_stream_;
  };

  // Returns a |RefPtr| to |this|.
  fxl::RefPtr<LpcmOutputStream> ref_ptr() {
    return fxl::RefPtr<LpcmOutputStream>(this);
  }

  LpcmOutputStream(app::ApplicationContext* application_context,
                   AudioSampleFormat sample_format,
                   uint32_t channel_count,
                   uint32_t frames_per_second,
                   size_t payload_pool_frames);

  // Called after |AdoptRef|.
  void Init();

  // Determines if this |LpcmOutputStream| is valid (has not been reset).
  bool is_valid() { return error_ != MediaResult::BAD_STATE; }

  // LpcmPayload::Owner implementation.
  void ReleasePayloadBuffer(void* buffer, size_t size) override;

  // Starts the audio renderer timeline.
  void Start(const TimelineFunction& timeline =
                 TimelineFunction(kUnspecifiedTime, 0, 1, 1));

  // Restarts a stream that has ended.
  void Restart();

  // Sets the current error and calls the error handler if there is one.
  void SetError(MediaResult error);

  // Internal version of |Send|.
  void SendInternal(LpcmPayload payload, bool end_of_stream);

  // Increases |next_pts_| as needed if |drop_frames_on_starvation_| is false
  // and the renderer is starving. Slippage is accumulated in
  // |slippage_frames_|.
  void MaybeSlipPts();

  // Sends |size| bytes of audio content from |buffer|, which must have been
  // allocated using |CreatePayload|. Assumes |started_| is true.
  void SendInternalStarted(void* buffer,
                           size_t size,
                           int64_t pts,
                           bool end_of_stream = false);

  // Calls |send_task_| synchronously if it's not null and it's time to call it
  // based on |send_task_lead_time_| or |send_task_payload_bytes_available_|.
  void MaybeCallSendTask();

  // Determines if the amount of content currently outstanding at the renderer
  // is adequate to cover the minimum latency and the specified lead time.
  bool LeadTimeSatisfied(int64_t lead_time) const;

  // Posts |send_task_| to run |send_task_lead_time_ns_| before the deadline.
  // Assumes that |local_to_presentation_frames_| is set and progressing
  // (invertable).
  void PostTaskBeforeDeadlineInternal();

  // Handles a status update from the timeline control point. When called
  // with the default argument values, initiates status updates.
  void HandleStatusUpdates(
      uint64_t version = MediaTimelineControlPoint::kInitialStatus,
      MediaTimelineControlPointStatusPtr status = nullptr);

  fxl::RefPtr<fxl::TaskRunner> task_runner_;

  uint32_t frames_per_second_;
  size_t payload_pool_frames_;
  Producer producer_;

  AudioRendererPtr audio_renderer_;
  MediaRendererPtr media_renderer_;
  MediaTimelineControlPointPtr timeline_control_point_;
  TimelineConsumerPtr timeline_consumer_;

  MediaResult error_ = MediaResult::OK;
  fxl::Closure error_handler_;
  bool ready_ = false;
  bool started_ = false;
  bool end_method_called_ = false;
  fxl::Closure end_callback_;
  size_t frames_currently_allocated_ = 0;
  std::vector<PendingSend> sends_pending_ready_;

  // |send_task_| is set when a send task is pending. |send_task_lead_time_ns_|
  // is set to a non-zero value to indicate that |PostTaskBeforeDeadline| was
  // called, but we didn't know the presentation timeline at the time.
  // |send_task_payload_frames_available_| is set to a non-zero value to
  // indicate that a |PostTaskWhenPayloadFramesAvailable| is pending.
  fxl::Closure send_task_;
  int64_t send_task_lead_time_ns_;
  size_t send_task_payload_frames_available_ = 0;

  int64_t next_send_pts_ = 0;
  int64_t next_return_pts_ = 0;
  // TODO(dalesat): Renderer should provide |min_latency_ns_|.
  int64_t min_latency_ns_ = Timeline::ns_from_ms(30);
  bool drop_frames_when_starving_ = false;
  size_t slippage_frames_ = 0;

  // Converts from system time ns to presentation time frames. This is set in
  // |HandleStatusUpdates| and is used for |PresentationTimeOfNextSend|.
  TimelineFunction local_to_presentation_frames_;
  // Converts ns to frames.
  TimelineFunction ns_to_frames_;

  // TODO(dalesat): Remove when service supports mute.
  float gain_db_ = 0.0f;

  FXL_DECLARE_THREAD_CHECKER(thread_checker_);

  FXL_DISALLOW_COPY_AND_ASSIGN(LpcmOutputStream);
};

}  // namespace media
