// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/tts_service/tts_speaker.h"
#include "garnet/public/lib/fsl/threading/create_thread.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/functional/make_copyable.h"

namespace media {
namespace tts {

static constexpr uint64_t kSharedBufSize = 64 << 10;
static constexpr uint32_t kOutputBufferId = 0;
static constexpr uint32_t kLowWaterMsec = 100;

static constexpr uint32_t kFliteChannelCount = 1;
static constexpr uint32_t kFliteFrameRate = 16000;
static constexpr auto kFliteSampleFormat = media::AudioSampleFormat::SIGNED_16;
static constexpr uint32_t kFliteBytesPerFrame = 2;
static constexpr uint32_t kLowWaterBytes =
    (kFliteFrameRate * kLowWaterMsec * kFliteBytesPerFrame) / 1000;

TtsSpeaker::TtsSpeaker(fxl::RefPtr<fxl::TaskRunner> master_task_runner)
    : master_task_runner_(std::move(master_task_runner)),
      abort_playback_(false),
      synthesis_complete_(false) {}

zx_status_t TtsSpeaker::Speak(const fidl::String& words,
                              const fxl::Closure& speak_complete_cbk) {
  words_ = std::move(words);
  speak_complete_cbk_ = std::move(speak_complete_cbk);

  FXL_DCHECK(!engine_thread_.joinable());
  engine_thread_ = fsl::CreateThread(&engine_task_runner_);

  engine_task_runner_->PostTask(
      [thiz = shared_from_this()]() { thiz->DoSpeak(); });

  return ZX_OK;
}

TtsSpeaker::~TtsSpeaker() {
  if (shared_buf_virt_ != nullptr) {
    zx::vmar::root_self().unmap(reinterpret_cast<uintptr_t>(shared_buf_virt_),
                                shared_buf_size_);
  }
}

zx_status_t TtsSpeaker::Init(
    const std::unique_ptr<app::ApplicationContext>& application_context) {
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

  res = zx::vmo::create(kSharedBufSize, 0, &shared_buf_vmo_);
  if (res != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to create " << kSharedBufSize
                   << " byte VMO!  (res " << res << ")";
    return res;
  }

  res = shared_buf_vmo_.get_size(&shared_buf_size_);
  if (res != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to fetch VMO size!  (res " << res << ")";
    return res;
  }

  // We currently hardcode 16 bps and single channel, so the size of our VMO
  // (even if the kernel rounds up to page size) should always be divisble by
  // the size of an audio frame (2 bytes)
  FXL_DCHECK((shared_buf_size_ % (sizeof(int16_t) * kFliteChannelCount)) == 0);

  uintptr_t tmp;
  res = zx::vmar::root_self().map(0, shared_buf_vmo_, 0, shared_buf_size_,
                                  ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE,
                                  &tmp);

  if (res != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to map VMO!  (res " << res << ")";
    return res;
  }
  shared_buf_virt_ = reinterpret_cast<void*>(tmp);

  zx::vmo rend_vmo;
  res = shared_buf_vmo_.duplicate(
      ZX_RIGHT_READ | ZX_RIGHT_TRANSFER | ZX_RIGHT_MAP, &rend_vmo);
  if (res != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to duplicate shared buffer VMO!  (res " << res
                   << ")";
    return res;
  }

  FXL_DCHECK(application_context != nullptr);
  auto audio_server =
      application_context->ConnectToEnvironmentService<media::AudioServer>();

  audio_server->CreateRenderer(audio_renderer_.NewRequest(),
                               media_renderer_.NewRequest());

  auto audio_details = media::AudioMediaTypeDetails::New();
  audio_details->sample_format = kFliteSampleFormat;
  audio_details->channels = kFliteChannelCount;
  audio_details->frames_per_second = kFliteFrameRate;

  auto media_details = media::MediaTypeDetails::New();
  media_details->set_audio(std::move(audio_details));

  auto media_type = media::MediaType::New();
  media_type->medium = media::MediaTypeMedium::AUDIO;
  media_type->details = std::move(media_details);
  media_type->encoding = media::MediaType::kAudioEncodingLpcm;

  media_renderer_->SetMediaType(std::move(media_type));
  media_renderer_->GetPacketConsumer(packet_consumer_.NewRequest());
  media_renderer_->GetTimelineControlPoint(timeline_cp_.NewRequest());
  packet_consumer_->AddPayloadBuffer(kOutputBufferId, std::move(rend_vmo));
  timeline_cp_->GetTimelineConsumer(timeline_consumer_.NewRequest());

  return ZX_OK;
}

void TtsSpeaker::Shutdown() {
  if (engine_task_runner_) {
    FXL_DCHECK(engine_thread_.joinable());
    abort_playback_.store(true);
    {
      fxl::MutexLocker lock(&ring_buffer_lock_);
      wakeup_event_.signal(0, ZX_USER_SIGNAL_0);
    }
    engine_thread_.join();
  }
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
    fxl::MutexLocker lock(&ring_buffer_lock_);
    bytes_to_send = ComputeTxPending();
  }

  bool first_payload = !clock_started_;
  bool eos = synthesis_complete_.load();
  uint64_t bytes_till_low_water = eos ? 0 : bytes_to_send - kLowWaterBytes;
  uint64_t bytes_till_ring_wrap = shared_buf_size_ - tx_ptr_;

  FXL_DCHECK(eos || bytes_to_send > kLowWaterBytes);

  while (bytes_to_send) {
    uint64_t todo = bytes_to_send;

    if (bytes_till_ring_wrap && (todo > bytes_till_ring_wrap)) {
      todo = bytes_till_ring_wrap;
    }

    if (bytes_till_low_water && (todo > bytes_till_low_water)) {
      todo = bytes_till_low_water;
    }

    auto pkt = media::MediaPacket::New();

    pkt->pts_rate_ticks = kFliteFrameRate;
    pkt->pts_rate_seconds = 1u;
    pkt->pts = first_payload ? 0 : media::kUnspecifiedTime;
    pkt->flags =
        (eos && (todo == bytes_to_send)) ? media::MediaPacket::kFlagEos : 0u;

    pkt->payload_buffer_id = kOutputBufferId;
    pkt->payload_offset = tx_ptr_;
    pkt->payload_size = todo;

    first_payload = false;
    tx_ptr_ += todo;
    if (tx_ptr_ >= shared_buf_size_) {
      FXL_DCHECK(tx_ptr_ == shared_buf_size_);
      tx_ptr_ = 0;
    }

    media::MediaPacketConsumer::SupplyPacketCallback after_payload_rendered;

    if (pkt->flags & media::MediaPacket::kFlagEos) {
      after_payload_rendered =
          [speak_complete_cbk = std::move(speak_complete_cbk_)](
              media::MediaPacketDemandPtr) { speak_complete_cbk(); };
    } else if (todo == bytes_till_low_water) {
      after_payload_rendered =
          [thiz = shared_from_this(), new_rd_pos = tx_ptr_](
              media::MediaPacketDemandPtr) { thiz->UpdateRdPtr(new_rd_pos); };
    } else {
      after_payload_rendered = [](media::MediaPacketDemandPtr) {};
    }

    packet_consumer_->SupplyPacket(std::move(pkt),
                                   std::move(after_payload_rendered));

    FXL_DCHECK(todo <= bytes_to_send);
    bytes_to_send -= todo;
    if (bytes_till_ring_wrap)
      bytes_till_ring_wrap -= todo;
    if (bytes_till_low_water)
      bytes_till_low_water -= todo;
  }

  if (!clock_started_) {
    auto start = media::TimelineTransform::New();
    start->reference_time = zx_clock_get(ZX_CLOCK_MONOTONIC) + ZX_MSEC(50);
    start->subject_time = 0;
    start->reference_delta = 1u;
    start->subject_delta = 1u;
    clock_started_ = true;
    timeline_consumer_->SetTimelineTransform(std::move(start), [](bool) {});
  }
}

void TtsSpeaker::UpdateRdPtr(uint64_t new_pos) {
  if (!abort_playback_.load()) {
    fxl::MutexLocker lock(&ring_buffer_lock_);
    rd_ptr_ = new_pos;
    wakeup_event_.signal(0, ZX_USER_SIGNAL_0);
  }
}

int TtsSpeaker::ProduceAudioCbk(const cst_wave* wave,
                                int start,
                                int sz,
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
      fxl::MutexLocker lock(&ring_buffer_lock_);
      uint64_t space = ComputeWriteSpace();

      if (size < space) {
        while (size > 0) {
          uint64_t todo = std::min<uint64_t>(shared_buf_size_ - wr_ptr_, size);

          ::memcpy(reinterpret_cast<uint8_t*>(shared_buf_virt_) + wr_ptr_,
                   payload, todo);

          size -= todo;
          wr_ptr_ += todo;
          payload = reinterpret_cast<const void*>(
              reinterpret_cast<uintptr_t>(payload) + todo);

          if (wr_ptr_ >= shared_buf_size_) {
            FXL_DCHECK(wr_ptr_ == shared_buf_size_);
            wr_ptr_ = 0;
          }
        }

        break;
      }

      wakeup_event_.signal(ZX_USER_SIGNAL_0, 0);
    }

    // Looks like we need to wait for there to be some space.  Before we do so,
    // let the master thread know it needs to send the data we just produced.
    master_task_runner_->PostTask(
        [thiz = shared_from_this()]() { thiz->SendPendingAudio(); });

    zx_signals_t pending;
    zx_status_t res;

    res = wakeup_event_.wait_one(ZX_USER_SIGNAL_0, zx::time::infinite(), &pending);
    if ((res != ZX_OK) || abort_playback_.load()) {
      return CST_AUDIO_STREAM_STOP;
    }
  }

  // If this is the last chunk of audio, tell the master thread to send the rest
  // of our synthesized audio right now.
  if (last) {
    synthesis_complete_.store(true);
    master_task_runner_->PostTask(
        [thiz = shared_from_this()]() { thiz->SendPendingAudio(); });
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

  flite_text_to_speech(words_.data(), vox, "play");
  delete_voice(vox);

  if (abort_playback_.load()) {
    master_task_runner_->PostTask(
        [speak_complete_cbk = std::move(speak_complete_cbk_)]() {
          speak_complete_cbk();
        });
  }

  fsl::MessageLoop::GetCurrent()->PostQuitTask();
}

}  // namespace tts
}  // namespace media
