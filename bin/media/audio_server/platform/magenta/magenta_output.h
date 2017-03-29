// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <mx/channel.h>
#include <mx/vmo.h>
#include <string>

#include "apps/media/src/audio_server/platform/generic/standard_output_base.h"

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
  MagentaOutput(mx::channel channel, AudioOutputManager* manager);

  template <typename ReqType, typename RespType>
  mx_status_t SyncDriverCall(const mx::channel& channel,
                             const ReqType& req,
                             RespType* resp,
                             mx_handle_t* resp_handle_out = nullptr);

  mx::channel stream_channel_;
  mx::channel rb_channel_;
  mx::vmo rb_vmo_;
  uint64_t rb_size_ = 0;
  uint32_t rb_frames_ = 0;
  uint64_t rb_fifo_depth_ = 0;
  void* rb_virt_ = nullptr;
  bool started_ = false;
  int64_t frames_sent_ = 0;
  uint32_t frames_to_mix_ = 0;
  int64_t fifo_frames_ = 0;
  int64_t low_water_frames_ = 0;
  TimelineRate local_to_frames_;
  TimelineFunction local_to_output_;
};

}  // namespace audio
}  // namespace media
