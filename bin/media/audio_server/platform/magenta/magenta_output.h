// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <mx/channel.h>
#include <mx/vmo.h>
#include <string>

#include "apps/media/src/audio_server/platform/generic/standard_output_base.h"
#include "drivers/audio/dispatcher-pool/dispatcher-channel.h"

namespace media {
namespace audio {

class MagentaOutput : public StandardOutputBase {
 public:
  static AudioOutputPtr Create(mx::channel channel,
                               AudioOutputManager* manager);
  ~MagentaOutput();

  // AudioOutput implementation
  MediaResult Init() override;

  void Cleanup() override;

  // StandardOutputBase implementation
  bool StartMixJob(MixJob* job, ftl::TimePoint process_start) override;
  bool FinishMixJob(const MixJob& job) override;

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
  // In an ideal world, all of these tasks would be handled by the MagentaOutput
  // directly, but there are some architectural issues which prevent this at the
  // moment.  In specific...
  //
  // The lifecycle of a DispatcherChannel::Owner is controlled using mxtl
  // intrusive ref counting and mxtl::RefPtrs.  Currently, audio server outputs
  // have their lifecycles managed using std:: weak and shared pointers.  These
  // two mechanisms are not compatible and should not be mixed.  Eventually, we
  // will convert the outputs to use mxtl intrusive primitives (for lists, sets,
  // ref counts, etc...), but until then, we need a separate object owned by the
  // MagentaOutput to serve as bridge between the two worlds.
  //
  // Additionally; using the audio dispatcher framework basically commits a user
  // to all async processing all of the time.  Attempting to use the
  // mx_channel_call synchronous call helper while there are threads waiting for
  // events in the dispatcher pool is going to cause problems.  Again, the plan
  // is currently to transition away from any synchronous interactions with the
  // driver and move to a purely async state machine model, but until that
  // happens we need to keep the event paths separate.
  //
  // Finally; the MagentaOutput is driven almost entirely by timing in steady
  // state operation.  Unfortunately, we do not currently have a kernel
  // primitive we can use to signal a magenta port at a scheduled time.  Once
  // this functionality arrives, we can...
  //
  // 1) Add support to the dispatcher for timers in addition to channels.
  // 2) Transition mixer outputs to use mxtl intrusive ref counting.
  // 3) Move event processing for the stream and ring-buffer channels into the
  //    MagentaOutput itself.
  // 4) Convert all communications between the mixer output and the driver to be
  //    asynchronous, and move timing over to the new timing object.
  //
  using DispatcherChannel = ::audio::DispatcherChannel;
  class EventReflector : public DispatcherChannel::Owner {
   public:
     static mxtl::RefPtr<EventReflector> Create(AudioOutputManager* manager,
                                                AudioOutputWeakPtr output) {
       return mxtl::AdoptRef(new EventReflector(manager, output));
     }
     mx_status_t Activate(mx::channel channel);

   protected:
    mx_status_t ProcessChannel(DispatcherChannel* channel) final;
    void NotifyChannelDeactivated(const DispatcherChannel& channel) final;

   private:
    EventReflector(AudioOutputManager* manager, AudioOutputWeakPtr output)
      : manager_(manager),
        output_(output) { }
    void HandlePlugStateChange(bool plugged, mx_time_t plug_time);

    AudioOutputManager* manager_;
    AudioOutputWeakPtr  output_;
  };

  MagentaOutput(mx::channel channel, AudioOutputManager* manager);

  template <typename ReqType, typename RespType>
  mx_status_t SyncDriverCall(const mx::channel& channel,
                             const ReqType& req,
                             RespType* resp,
                             mx_handle_t* resp_handle_out = nullptr);

  void ScheduleNextLowWaterWakeup();

  mx::channel stream_channel_;
  mx::channel rb_channel_;
  mx::vmo rb_vmo_;
  mxtl::RefPtr<EventReflector> reflector_;
  uint64_t rb_size_ = 0;
  uint32_t rb_frames_ = 0;
  uint64_t rb_fifo_depth_ = 0;
  void* rb_virt_ = nullptr;
  bool started_ = false;
  int64_t frames_sent_ = 0;
  uint32_t frames_to_mix_ = 0;
  int64_t fifo_frames_ = 0;
  int64_t low_water_frames_ = 0;
  mx_time_t underflow_start_time_ = 0;
  mx_time_t underflow_cooldown_deadline_ = 0;
  TimelineRate local_to_frames_;
  TimelineFunction local_to_output_;
};

}  // namespace audio
}  // namespace media
