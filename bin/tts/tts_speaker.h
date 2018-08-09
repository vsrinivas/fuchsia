// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_TTS_TTS_SPEAKER_H_
#define GARNET_BIN_TTS_TTS_SPEAKER_H_

#include <mutex>
#include <thread>

#include <fuchsia/media/cpp/fidl.h>
#include <fuchsia/tts/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fit/function.h>
#include <lib/fzl/vmo-mapper.h>
#include <zircon/types.h>

#include "lib/component/cpp/startup_context.h"
#include "lib/fidl/cpp/string.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/synchronization/thread_annotations.h"
#include "third_party/flite/include/flite_fuchsia.h"

namespace tts {

class TtsSpeaker : public std::enable_shared_from_this<TtsSpeaker> {
 public:
  TtsSpeaker(async_dispatcher_t* master_dispatcher);
  ~TtsSpeaker() = default;

  zx_status_t Init(
      const std::unique_ptr<component::StartupContext>& startup_context);

  zx_status_t Speak(fidl::StringPtr words, fit::closure speak_complete_cbk);
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

    auto sb_size = shared_buf_.size();
    FXL_DCHECK(front < sb_size);
    FXL_DCHECK(back < sb_size);
    ret = (front >= back) ? (front - back) : (sb_size + front - back);

    FXL_DCHECK(ret < sb_size);
    return ret;
  }

  uint64_t ComputeWriteSpace() FXL_EXCLUSIVE_LOCKS_REQUIRED(ring_buffer_lock_) {
    return shared_buf_.size() - ComputeRingDistance(rd_ptr_, wr_ptr_) - 1;
  }

  uint64_t ComputeTxPending() FXL_EXCLUSIVE_LOCKS_REQUIRED(ring_buffer_lock_) {
    return ComputeRingDistance(tx_ptr_, wr_ptr_);
  }

  bool clock_started_ = false;

  async::Loop engine_loop_;
  async_dispatcher_t* master_dispatcher_;

  fuchsia::media::AudioOutPtr audio_renderer_;
  fzl::VmoMapper shared_buf_;

  std::mutex ring_buffer_lock_;
  uint64_t wr_ptr_ FXL_GUARDED_BY(ring_buffer_lock_) = 0;
  uint64_t rd_ptr_ FXL_GUARDED_BY(ring_buffer_lock_) = 0;
  uint64_t tx_ptr_ = 0;
  zx::event wakeup_event_;

  fidl::StringPtr words_;
  fit::closure speak_complete_cbk_;
  std::atomic<bool> abort_playback_;
  std::atomic<bool> synthesis_complete_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TtsSpeaker);
};

}  // namespace tts

#endif  // GARNET_BIN_TTS_TTS_SPEAKER_H_
