// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async/cpp/task.h>

#include "garnet/bin/tts/tts_speaker.h"
#include "lib/fxl/functional/make_copyable.h"

namespace tts {

static constexpr uint64_t kSharedBufSize = 64 << 10;
static constexpr uint32_t kLowWaterMsec = 100;

static constexpr uint32_t kFliteChannelCount = 1;
static constexpr uint32_t kFliteFrameRate = 16000;
static constexpr auto kFliteSampleFormat =
    fuchsia::media::AudioSampleFormat::SIGNED_16;
static constexpr uint32_t kFliteBytesPerFrame = 2;
static constexpr uint32_t kLowWaterBytes =
    (kFliteFrameRate * kLowWaterMsec * kFliteBytesPerFrame) / 1000;

TtsSpeaker::TtsSpeaker(async_dispatcher_t* dispatcher)
    : master_dispatcher_(dispatcher), abort_playback_(false), synthesis_complete_(false) {
  engine_loop_.StartThread();
}

zx_status_t TtsSpeaker::Speak(fidl::StringPtr words,
                              fit::closure speak_complete_cbk) {
  words_ = std::move(words);
  speak_complete_cbk_ = std::move(speak_complete_cbk);

  async::PostTask(engine_loop_.dispatcher(),
                  [thiz = shared_from_this()]() { thiz->DoSpeak(); });

  return ZX_OK;
}

zx_status_t TtsSpeaker::Init(
    const std::unique_ptr<component::StartupContext>& startup_context) {
  zx_status_t res;

  if (wakeup_event_.is_valid()) {
    FXL_LOG(ERROR) << "Attempted to initialize TtsSpeaker twice!";
    return ZX_ERR_BAD_STATE;
  }

  res = zx::event::create(0, &wakeup_event_);
  if (res != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to create wakeup event!  (res " << res << ")";
    return res;
  }

  zx::vmo shared_vmo;
  res = shared_buf_.CreateAndMap(
      kSharedBufSize, ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE, nullptr,
      &shared_vmo, ZX_RIGHT_READ | ZX_RIGHT_MAP | ZX_RIGHT_TRANSFER);

  if (res != ZX_OK) {
    FXL_LOG(ERROR) << "VmoMapper:::CreateAndMap failed - " << res;
    return res;
  }

  FXL_DCHECK(startup_context != nullptr);
  auto audio =
      startup_context->ConnectToEnvironmentService<fuchsia::media::Audio>();

  audio->CreateRendererV2(audio_renderer_.NewRequest());

  fuchsia::media::AudioPcmFormat format;
  format.sample_format = kFliteSampleFormat;
  format.channels = kFliteChannelCount;
  format.frames_per_second = kFliteFrameRate;

  audio_renderer_->SetPcmFormat(std::move(format));
  audio_renderer_->SetPayloadBuffer(std::move(shared_vmo));

  return ZX_OK;
}

void TtsSpeaker::Shutdown() {
  abort_playback_.store(true);
  {
    std::lock_guard<std::mutex> lock(ring_buffer_lock_);
    wakeup_event_.signal(0, ZX_USER_SIGNAL_0);
  }
  engine_loop_.Shutdown();
}

void TtsSpeaker::SendPendingAudio() {
  if (abort_playback_.load())
    return;

  // Figure out how much audio we have synthesized, but not given to the audio
  // render yet, and hand it off to the renderer.  There are three (slightly)
  // special cases we need to consider.
  //
  // 1) We may not permit our payloads to span the ring wrap point.  All
  //    payloads must be contiugous in our VMO.
  // 2) We need to make sure that we break our payloads such that when we hit
  //    our low water mark, we receive a callback which wakes up the engine
  //    thread to produce some more audio.
  // 3) We need to make sure that we send our final payload that its callback
  //    calls our completion handler.
  //
  uint64_t bytes_to_send;
  {
    std::lock_guard<std::mutex> lock(ring_buffer_lock_);
    bytes_to_send = ComputeTxPending();
  }

  bool first_payload = !clock_started_;
  bool eos = synthesis_complete_.load();
  uint64_t bytes_till_low_water = eos ? 0 : bytes_to_send - kLowWaterBytes;
  uint64_t bytes_till_ring_wrap = shared_buf_.size() - tx_ptr_;

  FXL_DCHECK(eos || bytes_to_send > kLowWaterBytes);

  while (bytes_to_send) {
    uint64_t todo = bytes_to_send;

    if (bytes_till_ring_wrap && (todo > bytes_till_ring_wrap)) {
      todo = bytes_till_ring_wrap;
    }

    if (bytes_till_low_water && (todo > bytes_till_low_water)) {
      todo = bytes_till_low_water;
    }

    fuchsia::media::AudioPacket pkt;
    pkt.payload_offset = tx_ptr_;
    pkt.payload_size = todo;

    first_payload = false;
    tx_ptr_ += todo;
    if (tx_ptr_ >= shared_buf_.size()) {
      FXL_DCHECK(tx_ptr_ == shared_buf_.size());
      tx_ptr_ = 0;
    }

    if (eos && (todo == bytes_to_send)) {
      audio_renderer_->SendPacket(
          std::move(pkt), fxl::MakeCopyable([speak_complete_cbk = std::move(
                                                 speak_complete_cbk_)]() {
            speak_complete_cbk();
          }));
    } else if (todo == bytes_till_low_water) {
      audio_renderer_->SendPacket(
          std::move(pkt), [thiz = shared_from_this(), new_rd_pos = tx_ptr_]() {
            thiz->UpdateRdPtr(new_rd_pos);
          });
    } else {
      audio_renderer_->SendPacketNoReply(std::move(pkt));
    }

    FXL_DCHECK(todo <= bytes_to_send);
    bytes_to_send -= todo;
    if (bytes_till_ring_wrap)
      bytes_till_ring_wrap -= todo;
    if (bytes_till_low_water)
      bytes_till_low_water -= todo;
  }

  if (!clock_started_) {
    audio_renderer_->PlayNoReply(fuchsia::media::kNoTimestamp,
                                 fuchsia::media::kNoTimestamp);
    clock_started_ = true;
  }
}

void TtsSpeaker::UpdateRdPtr(uint64_t new_pos) {
  if (!abort_playback_.load()) {
    std::lock_guard<std::mutex> lock(ring_buffer_lock_);
    rd_ptr_ = new_pos;
    wakeup_event_.signal(0, ZX_USER_SIGNAL_0);
  }
}

int TtsSpeaker::ProduceAudioCbk(const cst_wave* wave, int start, int sz,
                                int last) {
  if (abort_playback_.load()) {
    return CST_AUDIO_STREAM_STOP;
  }

  FXL_DCHECK(sz >= 0);

  const void* payload;
  int16_t junk = 0;

  if (sz == 0) {
    FXL_DCHECK(last);
    payload = &junk;
    sz = 1;
  } else {
    payload = wave->samples + start;
  }

  uint64_t size = static_cast<uint64_t>(sz) * kFliteBytesPerFrame;

  while (true) {
    {  // explicit scope for ring buffer lock.
      std::lock_guard<std::mutex> lock(ring_buffer_lock_);
      uint64_t space = ComputeWriteSpace();

      if (size < space) {
        while (size > 0) {
          uint64_t todo;
          todo = std::min<uint64_t>(shared_buf_.size() - wr_ptr_, size);

          ::memcpy(reinterpret_cast<uint8_t*>(shared_buf_.start()) + wr_ptr_,
                   payload, todo);

          size -= todo;
          wr_ptr_ += todo;
          payload = reinterpret_cast<const void*>(
              reinterpret_cast<uintptr_t>(payload) + todo);

          if (wr_ptr_ >= shared_buf_.size()) {
            FXL_DCHECK(wr_ptr_ == shared_buf_.size());
            wr_ptr_ = 0;
          }
        }

        break;
      }

      wakeup_event_.signal(ZX_USER_SIGNAL_0, 0);
    }

    // Looks like we need to wait for there to be some space.  Before we do so,
    // let the master thread know it needs to send the data we just produced.
    async::PostTask(master_dispatcher_, [thiz = shared_from_this()]() {
      thiz->SendPendingAudio();
    });

    zx_signals_t pending;
    zx_status_t res;

    res = wakeup_event_.wait_one(ZX_USER_SIGNAL_0, zx::time::infinite(),
                                 &pending);
    if ((res != ZX_OK) || abort_playback_.load()) {
      return CST_AUDIO_STREAM_STOP;
    }
  }

  // If this is the last chunk of audio, tell the master thread to send the rest
  // of our synthesized audio right now.
  if (last) {
    synthesis_complete_.store(true);
    async::PostTask(master_dispatcher_, [thiz = shared_from_this()]() {
      thiz->SendPendingAudio();
    });
  }

  return CST_AUDIO_STREAM_CONT;
}

void TtsSpeaker::DoSpeak() {
  cst_voice* vox = flite_fuchsia_create_voice(
      [](const cst_wave* w, int start, int size, int last,
         struct cst_audio_streaming_info_struct* asi) -> int {
        auto thiz = reinterpret_cast<TtsSpeaker*>(asi->userdata);
        return thiz->ProduceAudioCbk(w, start, size, last);
      },
      this);

  flite_text_to_speech(words_->data(), vox, "play");
  delete_voice(vox);

  if (abort_playback_.load()) {
    async::PostTask(master_dispatcher_,
                    [speak_complete_cbk = std::move(speak_complete_cbk_)]() {
                      speak_complete_cbk();
                    });
  }
}

}  // namespace tts
