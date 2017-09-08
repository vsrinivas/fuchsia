// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zx/channel.h>
#include <zx/vmo.h>
#include <string>

#include "drivers/audio/dispatcher-pool/dispatcher-channel.h"
#include "drivers/audio/dispatcher-pool/dispatcher-execution-domain.h"
#include "garnet/bin/media/audio_server/platform/generic/standard_output_base.h"

namespace media {
namespace audio {

class DriverOutput : public StandardOutputBase {
 public:
  static fbl::RefPtr<AudioOutput> Create(zx::channel channel,
                                         AudioOutputManager* manager);
  ~DriverOutput();

  // AudioOutput implementation
  MediaResult Init() override;

  void Cleanup() override;

  // StandardOutputBase implementation
  bool StartMixJob(MixJob* job, fxl::TimePoint process_start)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mutex_) override;
  bool FinishMixJob(const MixJob& job)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mutex_) override;

 private:
  // TODO(johngro) : Remove the EventReflector class.
  //
  // The EventReflector is a temporary hack which will eventually go away.  It
  // takes ownership of the stream channel after the format has been configured
  // and binds it to the audio dispatcher thread pool as the stream channels
  // owner.  This allows it to process asynchronous events using the
  // dispatcher's thread pool, such as plug detection notifications or the
  // stream being unpublished by the stream driver.
  //
  // In an ideal world, all of these tasks would be handled by the DriverOutput
  // directly, but there are some architectural issues which prevent this at the
  // moment.  In specific...
  //
  // The lifecycle of a DispatcherEventSource::Owner is controlled using fbl
  // intrusive ref counting and fbl::RefPtrs.  Currently, audio server outputs
  // have their lifecycles managed using std:: weak and shared pointers.  These
  // two mechanisms are not compatible and should not be mixed.  Eventually, we
  // will convert the outputs to use fbl intrusive primitives (for lists, sets,
  // ref counts, etc...), but until then, we need a separate object owned by the
  // DriverOutput to serve as bridge between the two worlds.
  //
  // Additionally; using the audio dispatcher framework basically commits a user
  // to all async processing all of the time.  Attempting to use the
  // zx_channel_call synchronous call helper while there are threads waiting for
  // events in the dispatcher pool is going to cause problems.  Again, the plan
  // is currently to transition away from any synchronous interactions with the
  // driver and move to a purely async state machine model, but until that
  // happens we need to keep the event paths separate.
  //
  // Finally; the DriverOutput is driven almost entirely by timing in steady
  // state operation.  Unfortunately, we do not currently have a kernel
  // primitive we can use to signal a zircon port at a scheduled time.  Once
  // this functionality arrives, we can...
  //
  // 1) Add support to the dispatcher for timers in addition to channels.
  // 2) Transition mixer outputs to use fbl intrusive ref counting.
  // 3) Move event processing for the stream and ring-buffer channels into the
  //    DriverOutput itself.
  // 4) Convert all communications between the mixer output and the driver to be
  //    asynchronous, and move timing over to the new timing object.
  //
  class EventReflector : public fbl::RefCounted<EventReflector> {
   public:
    static fbl::RefPtr<EventReflector> Create(AudioOutputManager* manager,
                                              AudioOutput* output);
    zx_status_t Activate(zx::channel channel);

   private:
    using Channel = ::audio::dispatcher::Channel;
    using ExecutionDomain = ::audio::dispatcher::ExecutionDomain;

    EventReflector(AudioOutputManager* manager, AudioOutput* output,
                   fbl::RefPtr<ExecutionDomain>&& domain)
        : manager_(manager),
          output_(output),
          default_domain_(fbl::move(domain)) {}

    zx_status_t ProcessChannelMessage(Channel* channel);
    void ProcessChannelDeactivate(const Channel* channel);
    void HandlePlugStateChange(bool plugged, zx_time_t plug_time);

    AudioOutputManager* manager_;
    AudioOutput* output_;
    fbl::RefPtr<ExecutionDomain> default_domain_;
  };

  DriverOutput(zx::channel channel, AudioOutputManager* manager);

  template <typename ReqType, typename RespType>
  zx_status_t SyncDriverCall(const zx::channel& channel, const ReqType& req,
                             RespType* resp,
                             zx_handle_t* resp_handle_out = nullptr);

  void ScheduleNextLowWaterWakeup();

  zx::channel stream_channel_;
  zx::channel rb_channel_;
  zx::vmo rb_vmo_;
  fbl::RefPtr<EventReflector> reflector_;
  uint64_t rb_size_ = 0;
  uint32_t rb_frames_ = 0;
  uint64_t rb_fifo_depth_ = 0;
  void* rb_virt_ = nullptr;
  bool started_ = false;
  int64_t frames_sent_ = 0;
  uint32_t frames_to_mix_ = 0;
  int64_t fifo_frames_ = 0;
  int64_t low_water_frames_ = 0;
  zx_time_t underflow_start_time_ = 0;
  zx_time_t underflow_cooldown_deadline_ = 0;
  TimelineRate local_to_frames_;
  TimelineFunction local_to_output_;

  // TODO(johngro) : See MG-940.  eliminate this ASAP
  bool mix_job_prio_bumped_ = false;
};

}  // namespace audio
}  // namespace media
