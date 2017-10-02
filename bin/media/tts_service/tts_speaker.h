// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/types.h>
#include <zx/vmo.h>
#include <thread>

#include "lib/app/cpp/application_context.h"
#include "lib/fidl/cpp/bindings/string.h"
#include "lib/fxl/synchronization/mutex.h"
#include "lib/fxl/tasks/task_runner.h"
#include "lib/media/fidl/audio_server.fidl.h"
#include "third_party/flite/include/flite_fuchsia.h"

namespace media {
namespace tts {

class TtsSpeaker : public std::enable_shared_from_this<TtsSpeaker> {
 public:
  TtsSpeaker(fxl::RefPtr<fxl::TaskRunner> master_task_runner);
  ~TtsSpeaker();

  zx_status_t Init(
      const std::unique_ptr<app::ApplicationContext>& application_context);

  zx_status_t Speak(const fidl::String& words,
                    const fxl::Closure& speak_complete_cbk);
  void Shutdown();

 private:
  // Methods which interact with the audio mixer service and run on the master
  // thread.
  void SendPendingAudio();
  void UpdateRdPtr(uint64_t new_pos);

  // Methods which run on the dedicated engine thread.
  int ProduceAudioCbk(const cst_wave* wave, int start, int size, int last);
  void DoSpeak();

  // Methods which may run on either thread.
  uint64_t ComputeRingDistance(uint64_t back, uint64_t front) {
    uint64_t ret;

    FXL_DCHECK(front < shared_buf_size_);
    FXL_DCHECK(back < shared_buf_size_);
    ret = (front >= back) ? (front - back) : (shared_buf_size_ + front - back);

    FXL_DCHECK(ret < shared_buf_size_);
    return ret;
  }

  uint64_t ComputeWriteSpace() FXL_EXCLUSIVE_LOCKS_REQUIRED(ring_buffer_lock_) {
    return shared_buf_size_ - ComputeRingDistance(rd_ptr_, wr_ptr_) - 1;
  }

  uint64_t ComputeTxPending() FXL_EXCLUSIVE_LOCKS_REQUIRED(ring_buffer_lock_) {
    return ComputeRingDistance(tx_ptr_, wr_ptr_);
  }

  bool clock_started_ = false;

  std::thread engine_thread_;
  fxl::RefPtr<fxl::TaskRunner> engine_task_runner_;
  fxl::RefPtr<fxl::TaskRunner> master_task_runner_;

  media::AudioRendererPtr audio_renderer_;
  media::MediaRendererPtr media_renderer_;
  media::MediaPacketConsumerPtr packet_consumer_;
  media::MediaTimelineControlPointPtr timeline_cp_;
  media::TimelineConsumerPtr timeline_consumer_;

  zx::vmo shared_buf_vmo_;
  void* shared_buf_virt_ = nullptr;

  // Note: shared_buf_size_ is a value established at Init time, before the work
  // thread has been created.  Once its value has been determined, it never
  // changes, therefor it should not need to be guarded by the ring buffer lock.
  uint64_t shared_buf_size_ = 0;

  fxl::Mutex ring_buffer_lock_;
  uint64_t wr_ptr_ FXL_GUARDED_BY(ring_buffer_lock_) = 0;
  uint64_t rd_ptr_ FXL_GUARDED_BY(ring_buffer_lock_) = 0;
  uint64_t tx_ptr_ = 0;
  zx::event wakeup_event_;

  fidl::String words_;
  fxl::Closure speak_complete_cbk_;
  std::atomic<bool> abort_playback_;
  std::atomic<bool> synthesis_complete_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TtsSpeaker);
};

}  // namespace tts
}  // namespace media
