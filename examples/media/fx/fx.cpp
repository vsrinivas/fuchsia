// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <audio-utils/audio-input.h>
#include <fbl/algorithm.h>
#include <fbl/auto_call.h>
#include <fbl/limits.h>
#include <fbl/unique_ptr.h>
#include <fuchsia/media/cpp/fidl.h>
#include <inttypes.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/fit/function.h>
#include <lib/fzl/vmo-mapper.h>
#include <lib/zx/time.h>
#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>
#include <stdio.h>
#include <zircon/compiler.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include "lib/component/cpp/connect.h"
#include "lib/component/cpp/startup_context.h"
#include "lib/fsl/tasks/fd_waiter.h"
#include "lib/media/timeline/timeline_function.h"

#include "garnet/lib/media/wav_writer/wav_writer.h"

constexpr bool kWavWriterEnabled = false;

using media::TimelineFunction;

constexpr uint32_t NUM_CHANNELS = 1u;
constexpr uint32_t INPUT_FRAMES_PER_SEC = 48000u;
constexpr uint32_t INPUT_BUFFER_LENGTH_MSEC = 10u;
constexpr uint32_t INPUT_BUFFER_MIN_FRAMES =
    (INPUT_FRAMES_PER_SEC * INPUT_BUFFER_LENGTH_MSEC) / 1000u;
constexpr zx_time_t PROCESS_CHUNK_TIME = ZX_MSEC(1);
constexpr uint32_t OUTPUT_BUF_MSEC = 1000;
constexpr zx_time_t OUTPUT_BUF_TIME = ZX_MSEC(OUTPUT_BUF_MSEC);
constexpr zx_time_t OUTPUT_SEND_PACKET_OVERHEAD_NSEC = ZX_MSEC(1);

constexpr int32_t MIN_REVERB_DEPTH_MSEC = 1;
constexpr int32_t MAX_REVERB_DEPTH_MSEC = OUTPUT_BUF_MSEC - 10;
constexpr int32_t SMALL_REVERB_DEPTH_STEP = 1;
constexpr int32_t LARGE_REVERB_DEPTH_STEP = 10;
constexpr float MIN_REVERB_FEEDBACK_GAIN = -60.0f;
constexpr float MAX_REVERB_FEEDBACK_GAIN = -3.0f;
constexpr float SMALL_REVERB_GAIN_STEP = 0.5;
constexpr float LARGE_REVERB_GAIN_STEP = 2.5;

constexpr float MIN_FUZZ_GAIN = 1.0;
constexpr float MAX_FUZZ_GAIN = 50.0;
constexpr float SMALL_FUZZ_GAIN_STEP = 0.1;
constexpr float LARGE_FUZZ_GAIN_STEP = 1.0;
constexpr float MIN_FUZZ_MIX = 0.0;
constexpr float MAX_FUZZ_MIX = 1.0;
constexpr float SMALL_FUZZ_MIX_STEP = 0.01;
constexpr float LARGE_FUZZ_MIX_STEP = 0.1;

constexpr float MIN_PREAMP_GAIN = -30.0f;
constexpr float MAX_PREAMP_GAIN = 20.0f;
constexpr float SMALL_PREAMP_GAIN_STEP = 0.1f;
constexpr float LARGE_PREAMP_GAIN_STEP = 1.0f;
constexpr uint32_t PREAMP_GAIN_FRAC_BITS = 12;

constexpr int32_t DEFAULT_REVERB_DEPTH_MSEC = 200;
constexpr float DEFAULT_REVERB_FEEDBACK_GAIN = -4.0f;
constexpr float DEFAULT_FUZZ_GAIN = 0.0;
constexpr float DEFAULT_FUZZ_MIX = 1.0;
constexpr float DEFAULT_PREAMP_GAIN = -5.0f;

using audio::utils::AudioInput;

class FxProcessor {
 public:
  FxProcessor(fbl::unique_ptr<AudioInput> input, fit::closure quit_callback)
      : input_(fbl::move(input)), quit_callback_(std::move(quit_callback)) {
    FXL_DCHECK(quit_callback_);
  }

  void Startup(fuchsia::media::AudioPtr audio);

 private:
  using EffectFn = void (FxProcessor::*)(int16_t* src, int16_t* dst,
                                         uint32_t frames);

  static inline float Norm(int16_t value) {
    return (value < 0)
               ? static_cast<float>(value) / fbl::numeric_limits<int16_t>::min()
               : static_cast<float>(value) /
                     fbl::numeric_limits<int16_t>::max();
  }

  static inline float FuzzNorm(float norm_value, float gain) {
    return 1.0f - expf(-norm_value * gain);
  }

  void OnMinLeadTimeChanged(int64_t new_min_lead_time_nsec);
  void RequestKeystrokeMessage();
  void HandleKeystroke(zx_status_t status, uint32_t events);
  void Shutdown(const char* reason = "unknown");
  void ProcessInput();
  void ProduceOutputPackets(fuchsia::media::StreamPacket* out_pkt1,
                            fuchsia::media::StreamPacket* out_pkt2);
  void ApplyEffect(int16_t* src, uint32_t src_offset, uint32_t src_rb_size,
                   int16_t* dst, uint32_t dst_offset, uint32_t dst_rb_size,
                   uint32_t frames, EffectFn effect);

  void CopyInputEffect(int16_t* src, int16_t* dst, uint32_t frames);
  void PreampInputEffect(int16_t* src, int16_t* dst, uint32_t frames);
  void ReverbMixEffect(int16_t* src, int16_t* dst, uint32_t frames);
  void FuzzEffect(int16_t* src, int16_t* dst, uint32_t frames);
  void MixedFuzzEffect(int16_t* src, int16_t* dst, uint32_t frames);

  fsl::FDWaiter::Callback handle_keystroke_thunk_ = [this](zx_status_t status,
                                                           uint32_t event) {
    HandleKeystroke(status, event);
  };

  void UpdateReverb(bool enabled, int32_t depth_delta = 0,
                    float gain_delta = 0.0f);
  void UpdateFuzz(bool enabled, float gain_delta = 0.0f,
                  float mix_delta = 0.0f);
  void UpdatePreampGain(float delta);

  fzl::VmoMapper output_buf_;
  size_t output_buf_sz_ = 0;
  uint32_t output_buf_frames_ = 0;
  uint64_t output_buf_wp_ = 0;
  int64_t input_rp_ = 0;
  bool shutting_down_ = false;

  bool reverb_enabled_ = false;
  int32_t reverb_depth_msec_ = DEFAULT_REVERB_DEPTH_MSEC;
  float reverb_feedback_gain_ = DEFAULT_REVERB_FEEDBACK_GAIN;
  uint32_t reverb_depth_frames_;
  uint16_t reverb_feedback_gain_fixed_;

  bool fuzz_enabled_ = false;
  float fuzz_gain_ = DEFAULT_FUZZ_GAIN;
  float fuzz_mix_ = DEFAULT_FUZZ_MIX;
  float fuzz_mix_inv_;

  float preamp_gain_ = DEFAULT_PREAMP_GAIN;
  uint16_t preamp_gain_fixed_;

  fbl::unique_ptr<AudioInput> input_;
  fit::closure quit_callback_;
  uint32_t input_buffer_frames_ = 0;
  fuchsia::media::AudioOutPtr audio_renderer_;
  media::TimelineFunction clock_mono_to_input_wr_ptr_;
  fsl::FDWaiter keystroke_waiter_;
  media::audio::WavWriter<kWavWriterEnabled> wav_writer_;

  int64_t lead_time_frames_ = 0;
  bool lead_time_frames_known_ = false;
};

void FxProcessor::Startup(fuchsia::media::AudioPtr audio) {
  auto cleanup = fbl::MakeAutoCall([this] { Shutdown("Startup failure"); });

  zx_thread_set_priority(24 /* HIGH_PRIORITY in LK */);

  if (input_->sample_size() != 2) {
    printf("Invalid input sample size %u\n", input_->sample_size());
    return;
  }

  FXL_DCHECK((input_->ring_buffer_bytes() % input_->frame_sz()) == 0);
  input_buffer_frames_ = input_->ring_buffer_bytes() / input_->frame_sz();

  if (!wav_writer_.Initialize(
          "/tmp/fx.wav", fuchsia::media::AudioSampleFormat::SIGNED_16,
          input_->channel_cnt(), input_->frame_rate(), 16)) {
    printf("Unable to initialize WAV file for recording.\n");
    return;
  }

  // Create a renderer.  Setup connection error handlers.
  audio->CreateAudioOut(audio_renderer_.NewRequest());

  audio_renderer_.set_error_handler([this]() {
    Shutdown("fuchsia::media::AudioRenderer connection closed");
  });

  // Set the stream_type.
  fuchsia::media::AudioStreamType stream_type;
  stream_type.sample_format = fuchsia::media::AudioSampleFormat::SIGNED_16;
  stream_type.channels = input_->channel_cnt();
  stream_type.frames_per_second = input_->frame_rate();
  audio_renderer_->SetPcmStreamType(std::move(stream_type));

  // Create and map a VMO our mixing buffer and that we will use to send data to
  // the audio renderer.  Fill the memory with silence, then send a handle to
  // the VMO with read only rights to the audio renderer.
  output_buf_frames_ = static_cast<uint32_t>(
      (OUTPUT_BUF_TIME * input_->frame_rate()) / 1000000000u);
  output_buf_sz_ = static_cast<size_t>(input_->frame_sz()) * output_buf_frames_;

  zx::vmo rend_vmo;
  zx_status_t res = output_buf_.CreateAndMap(
      output_buf_sz_, ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE, nullptr,
      &rend_vmo, ZX_RIGHT_READ | ZX_RIGHT_MAP | ZX_RIGHT_TRANSFER);

  audio_renderer_->AddPayloadBuffer(0, std::move(rend_vmo));

  // We want to work in units of audio frames for our PTS units.  Configure this
  // now.
  audio_renderer_->SetPtsUnits(input_->frame_rate(), 1);

  // Start the input ring buffer.
  res = input_->StartRingBuffer();
  if (res != ZX_OK) {
    printf("Failed to start input ring buffer (res %d)\n", res);
    return;
  }

  // Setup the function which will convert from system ticks to the ring
  // buffer write pointer (in audio frames).  Note, we offset by the fifo
  // depth so that the write pointer we get back will be the safe write
  // pointer position; IOW - not where the capture currently is, but where the
  // most recent frame which is guaranteed to be written to system memory is.
  int64_t fifo_frames =
      ((input_->fifo_depth() + input_->frame_sz() - 1) / input_->frame_sz());

  media::TimelineRate frames_per_nsec;
  {
    media::TimelineRate frames_per_sec(input_->frame_rate(), 1);
    media::TimelineRate sec_per_nsec(1, ZX_SEC(1));
    frames_per_nsec =
        media::TimelineRate::Product(frames_per_sec, sec_per_nsec);
  }

  clock_mono_to_input_wr_ptr_ = media::TimelineFunction(
      -fifo_frames, input_->start_time(), frames_per_nsec);

  // Request notifications about the minimum clock lead time requirements.  We
  // will be able to start to process the input stream once we know what this
  // number is.
  // TODO(johngro): Set the handler here!
  audio_renderer_.events().OnMinLeadTimeChanged = [this](int64_t nsec) {
    OnMinLeadTimeChanged(nsec);
  };
  audio_renderer_->EnableMinLeadTimeEvents(true);

  // Success.  Print out the usage message, and force an update of effect
  // parameters (which will also print their status).
  printf(
      "Welcome to FX.  Keybindings are as follows.\n"
      "q : Quit the application.\n"
      "\n== Pre-amp Gain\n"
      "] : Increase the pre-amp gain\n"
      "[ : Decrease the pre-amp gain\n"
      "\n== Reverb/Echo Effect ==\n"
      "r : Toggle Reverb\n"
      "i : Increase reverb feedback gain\n"
      "k : Decrease reverb feedback gain\n"
      "l : Increase reverb delay\n"
      "j : Decrease reverb delay\n"
      "\n== Fuzz Effect ==\n"
      "f : Toggle Fuzz\n"
      "w : Increase the fuzz gain\n"
      "s : Decrease the fuzz gain\n"
      "d : Increase the fuzz mix percentage\n"
      "a : Decrease the fuzz mix percentage\n"
      "\nUse <shift> when adjusting parameters in order to use the large "
      "step size for the parameter.\n"
      "\nCurrent settings are...\n");
  UpdatePreampGain(0.0f);
  UpdateFuzz(fuzz_enabled_);
  UpdateReverb(reverb_enabled_);

  // Start to process keystrokes, then cancel the auto-cleanup and get out..
  RequestKeystrokeMessage();
  cleanup.cancel();
}

void FxProcessor::OnMinLeadTimeChanged(int64_t new_min_lead_time_nsec) {
  const auto& cm_to_frames = clock_mono_to_input_wr_ptr_.rate();
  int64_t new_lead_time_frames = cm_to_frames.Scale(new_min_lead_time_nsec);

  if (new_lead_time_frames > lead_time_frames_) {
    // Note: If the system is currently running, this discontinuity is going to
    // put a pop into our presentation.  If this is a huge issue, what we would
    // really want to do is...
    //
    // 1) Take manual control of the routing policy.
    // 2) When outputs get added, decide whether or not we want to make any
    //    routing changes ourselves.
    // 3) If we do, and these changes would effect our lead time requirements,
    //    we should smoothly ramp down our current presentation, let that play
    //    out, then stop the output, make the routing changes, then start
    //    everything back up again.
    //
    // Right now, there are no policy APIs which would allow us to acomplish any
    // of this, so this is the best we can do for the time being.
    lead_time_frames_ = new_lead_time_frames;
  }

  // If this is the first time we are learning about our lead time requirements,
  // it is time to process some input data and start the clock.
  if (!lead_time_frames_known_) {
    lead_time_frames_known_ = true;

    // Offset our initial write pointer by a small number of frames (in addition
    // to our lead time) to allow time for our packet messages to read the mixer
    // and get noticed by the mixing output loops.
    output_buf_wp_ = cm_to_frames.Scale(OUTPUT_SEND_PACKET_OVERHEAD_NSEC);

    // Set up our concept of the input read pointer so that it one
    // PROCESS_CHUNK_TIME behind the current write pointer.
    zx_time_t now = zx_clock_get(ZX_CLOCK_MONOTONIC);
    input_rp_ = clock_mono_to_input_wr_ptr_.Apply(now - PROCESS_CHUNK_TIME);

    // Process the input to produce some output, then start the clock.  Note: we
    // start the clock by explicitly mapping  'now' to PTS 0 on our presentation
    // timeline.  We will control our clock lead time by writing explicit
    // timestamps on our packets using the sum of the current output_buf_wp_ and
    // lead_time_frames_.
    ProcessInput();
    audio_renderer_->PlayNoReply(now, 0);
  }
}

void FxProcessor::RequestKeystrokeMessage() {
  keystroke_waiter_.Wait(handle_keystroke_thunk_, STDIN_FILENO, POLLIN);
}

void FxProcessor::HandleKeystroke(zx_status_t status, uint32_t events) {
  if (shutting_down_) {
    return;
  }

  if (status != ZX_OK) {
    printf("Bad status in HandleKeystroke (status %d)\n", status);
    Shutdown("Keystroke read error");
  }

  char c;
  ssize_t res = ::read(STDIN_FILENO, &c, sizeof(c));
  if (res != 1) {
    printf("Error reading keystroke (res %zd, errno %d)\n", res, errno);
    Shutdown("Keystroke read error");
  }

  switch (c) {
    case 'q':
    case 'Q':
      Shutdown("User requested");
      break;

    case 'r':
    case 'R':
      UpdateReverb(!reverb_enabled_);
      break;
    case 'i':
      UpdateReverb(true, 0, SMALL_REVERB_GAIN_STEP);
      break;
    case 'I':
      UpdateReverb(true, 0, LARGE_REVERB_GAIN_STEP);
      break;
    case 'k':
      UpdateReverb(true, 0, -SMALL_REVERB_GAIN_STEP);
      break;
    case 'K':
      UpdateReverb(true, 0, -LARGE_REVERB_GAIN_STEP);
      break;
    case 'l':
      UpdateReverb(true, SMALL_REVERB_DEPTH_STEP, 0.0f);
      break;
    case 'L':
      UpdateReverb(true, LARGE_REVERB_DEPTH_STEP, 0.0f);
      break;
    case 'j':
      UpdateReverb(true, -SMALL_REVERB_DEPTH_STEP, 0.0f);
      break;
    case 'J':
      UpdateReverb(true, -LARGE_REVERB_DEPTH_STEP, 0.0f);
      break;

    case '[':
      UpdatePreampGain(-SMALL_PREAMP_GAIN_STEP);
      break;
    case '{':
      UpdatePreampGain(-LARGE_PREAMP_GAIN_STEP);
      break;
    case ']':
      UpdatePreampGain(SMALL_PREAMP_GAIN_STEP);
      break;
    case '}':
      UpdatePreampGain(LARGE_PREAMP_GAIN_STEP);
      break;

    case 'f':
    case 'F':
      UpdateFuzz(!fuzz_enabled_);
      break;
    case 'd':
      UpdateFuzz(true, 0.0, SMALL_FUZZ_MIX_STEP);
      break;
    case 'D':
      UpdateFuzz(true, 0.0, LARGE_FUZZ_MIX_STEP);
      break;
    case 'a':
      UpdateFuzz(true, 0.0, -SMALL_FUZZ_MIX_STEP);
      break;
    case 'A':
      UpdateFuzz(true, 0.0, -LARGE_FUZZ_MIX_STEP);
      break;
    case 'w':
      UpdateFuzz(true, SMALL_FUZZ_GAIN_STEP);
      break;
    case 'W':
      UpdateFuzz(true, LARGE_FUZZ_GAIN_STEP);
      break;
    case 's':
      UpdateFuzz(true, -SMALL_FUZZ_GAIN_STEP);
      break;
    case 'S':
      UpdateFuzz(true, -LARGE_FUZZ_GAIN_STEP);
      break;

    default:
      break;
  }

  RequestKeystrokeMessage();
}

void FxProcessor::Shutdown(const char* reason) {
  // We're done (for good or bad): flush (save) the headers; close the WAV file.
  wav_writer_.Close();

  printf("Shutting down, reason = \"%s\"\n", reason);
  shutting_down_ = true;
  audio_renderer_.Unbind();
  input_.reset();
  quit_callback_();
}

void FxProcessor::ProcessInput() {
  fuchsia::media::StreamPacket pkt1, pkt2;

  pkt1.payload_size = 0;
  pkt2.payload_size = 0;

  // Produce output packet(s)  If we do not produce any packets, something is
  // very wrong and we are in the process of shutting down, so just get out now.
  ProduceOutputPackets(&pkt1, &pkt2);
  if (!pkt1.payload_size) {
    return;
  }

  // Send the packet(s)
  audio_renderer_->SendPacketNoReply(std::move(pkt1));
  if (pkt2.payload_size) {
    audio_renderer_->SendPacketNoReply(std::move(pkt2));
  }

  // If the input has been closed by the driver, shutdown.
  if (input_->IsRingBufChannelConnected()) {
    Shutdown("Input unplugged");
    return;
  }

  // Save output audio to WAV file (if configured to do so).
  auto output_base = reinterpret_cast<uint8_t*>(output_buf_.start());
  if (pkt1.payload_size) {
    wav_writer_.Write(output_base + pkt1.payload_offset, pkt1.payload_size);
  }

  if (pkt2.payload_size) {
    wav_writer_.Write(output_base + pkt2.payload_offset, pkt2.payload_size);
  }

  // Schedule our next processing callback.
  async::PostDelayedTask(async_get_default_dispatcher(),
                         [this]() { ProcessInput(); },
                         zx::nsec(PROCESS_CHUNK_TIME));
}

void FxProcessor::ProduceOutputPackets(fuchsia::media::StreamPacket* out_pkt1,
                                       fuchsia::media::StreamPacket* out_pkt2) {
  // Figure out how much input data we have to process.
  zx_time_t now = zx_clock_get(ZX_CLOCK_MONOTONIC);
  int64_t input_wp = clock_mono_to_input_wr_ptr_.Apply(now);
  if (input_wp <= input_rp_) {
    printf("input wp <= rp (wp %" PRId64 " rp %" PRId64 " now %" PRIu64 ")\n",
           input_wp, input_rp_, now);
    Shutdown("Failed to produce output packet");
    return;
  }

  int64_t todo64 = input_wp - input_rp_;
  if (todo64 > input_buffer_frames_) {
    printf(
        "Fell behind by more than the input buffer size "
        "(todo %" PRId64 " buflen %u\n",
        todo64, input_buffer_frames_);
    Shutdown("Failed to produce output packet");
    return;
  }

  uint32_t todo = static_cast<uint32_t>(todo64);
  uint32_t input_start =
      static_cast<uint32_t>(input_rp_) % input_buffer_frames_;
  uint32_t output_start = output_buf_wp_ % output_buf_frames_;
  uint32_t output_space = output_buf_frames_ - output_start;

  // Create the actual output packet(s) based on the amt of data we need to
  // send and the current position of the write pointer in the output ring
  // buffer.
  uint32_t pkt1_frames = fbl::min<uint32_t>(output_space, todo);
  out_pkt1->pts = output_buf_wp_ + lead_time_frames_;
  out_pkt1->payload_offset = output_start * input_->frame_sz();
  out_pkt1->payload_size = pkt1_frames * input_->frame_sz();

  // Does this job wrap the ring?  If so, we need to create 2 packets instead
  // of 1.
  if (pkt1_frames < todo) {
    out_pkt2->pts = out_pkt1->pts + pkt1_frames;
    out_pkt2->payload_offset = 0;
    out_pkt2->payload_size = (todo - pkt1_frames) * input_->frame_sz();
  } else {
    out_pkt2->pts = fuchsia::media::NO_TIMESTAMP;
    out_pkt2->payload_offset = 0;
    out_pkt2->payload_size = 0;
  }

  // Now actually apply the effects.  Start by just copying the input to the
  // output.
  auto input_base = reinterpret_cast<int16_t*>(input_->ring_buffer());
  auto output_base = reinterpret_cast<int16_t*>(output_buf_.start());
  ApplyEffect(input_base, input_start, input_buffer_frames_, output_base,
              output_start, output_buf_frames_, todo,
              (preamp_gain_ == 0.0) ? &FxProcessor::CopyInputEffect
                                    : &FxProcessor::PreampInputEffect);

  // If enabled, add some fuzz
  if (fuzz_enabled_ && (fuzz_mix_ >= 0.01f)) {
    ApplyEffect(output_base, output_start, output_buf_frames_, output_base,
                output_start, output_buf_frames_, todo,
                (fuzz_mix_ <= 0.99f) ? &FxProcessor::MixedFuzzEffect
                                     : &FxProcessor::FuzzEffect);
  }

  // If enabled, add some reverb.
  if (reverb_enabled_ && (reverb_feedback_gain_fixed_ > 0)) {
    uint32_t reverb_start =
        output_start + (output_buf_frames_ - reverb_depth_frames_);
    if (reverb_start >= output_buf_frames_)
      reverb_start -= output_buf_frames_;

    ApplyEffect(output_base, reverb_start, output_buf_frames_, output_base,
                output_start, output_buf_frames_, todo,
                &FxProcessor::ReverbMixEffect);
  }

  // Finally, update our input read pointer and our output write pointer.
  input_rp_ += todo;
  output_buf_wp_ += todo;
}

void FxProcessor::ApplyEffect(int16_t* src, uint32_t src_offset,
                              uint32_t src_rb_size, int16_t* dst,
                              uint32_t dst_offset, uint32_t dst_rb_size,
                              uint32_t frames, EffectFn effect) {
  while (frames) {
    ZX_DEBUG_ASSERT(src_offset < src_rb_size);
    ZX_DEBUG_ASSERT(dst_offset < dst_rb_size);

    uint32_t src_space = src_rb_size - src_offset;
    uint32_t dst_space = dst_rb_size - dst_offset;
    uint32_t todo = fbl::min(fbl::min(frames, src_space), dst_space);

    // TODO(johngro): Either add fbl::invoke to fbl, or use std::invoke
    // here when we switch to C++17.  The syntax for invoking a pointer to
    // non-static method on an object is ugly and hard to understand, and
    // people should not be forced to look at it.
    ((*this).*(effect))(src + (src_offset * NUM_CHANNELS),
                        dst + (dst_offset * NUM_CHANNELS), todo);

    src_offset = (src_space > todo) ? (src_offset + todo) : 0;
    dst_offset = (dst_space > todo) ? (dst_offset + todo) : 0;
    frames -= todo;
  }
}

void FxProcessor::CopyInputEffect(int16_t* src, int16_t* dst, uint32_t frames) {
  ::memcpy(dst, src, frames * sizeof(*dst) * NUM_CHANNELS);
}

void FxProcessor::PreampInputEffect(int16_t* src, int16_t* dst,
                                    uint32_t frames) {
  for (uint32_t i = 0; i < frames * NUM_CHANNELS; ++i) {
    int32_t tmp = src[i];
    tmp *= preamp_gain_fixed_;
    tmp >>= PREAMP_GAIN_FRAC_BITS;
    tmp = fbl::clamp<int32_t>(tmp, fbl::numeric_limits<int16_t>::min(),
                              fbl::numeric_limits<int16_t>::max());
    dst[i] = static_cast<int16_t>(tmp);
  }
}

void FxProcessor::ReverbMixEffect(int16_t* src, int16_t* dst, uint32_t frames) {
  // TODO(johngro): We should probably process everything into an intermediate
  // 32 bit (or even 64 bit or float) buffer, and clamp after the fact.
  for (uint32_t i = frames * NUM_CHANNELS; i > 0;) {
    --i;

    int32_t tmp = src[i];
    tmp *= reverb_feedback_gain_fixed_;
    tmp >>= 16;
    tmp += dst[i];
    tmp = fbl::clamp<int32_t>(tmp, fbl::numeric_limits<int16_t>::min(),
                              fbl::numeric_limits<int16_t>::max());
    dst[i] = static_cast<int16_t>(tmp);
  }
}

void FxProcessor::FuzzEffect(int16_t* src, int16_t* dst, uint32_t frames) {
  for (uint32_t i = 0; i < frames * NUM_CHANNELS; ++i) {
    float norm = FuzzNorm(Norm(src[i]), fuzz_gain_);
    dst[i] =
        (src[i] < 0)
            ? static_cast<int16_t>(fbl::numeric_limits<int16_t>::min() * norm)
            : static_cast<int16_t>(fbl::numeric_limits<int16_t>::max() * norm);
  }
}

void FxProcessor::MixedFuzzEffect(int16_t* src, int16_t* dst, uint32_t frames) {
  for (uint32_t i = 0; i < frames * NUM_CHANNELS; ++i) {
    float norm = Norm(src[i]);
    float fnorm = FuzzNorm(norm, fuzz_gain_);
    float mixed = ((fnorm * fuzz_mix_) + (norm * fuzz_mix_inv_));
    dst[i] =
        (src[i] < 0)
            ? static_cast<int16_t>(fbl::numeric_limits<int16_t>::min() * mixed)
            : static_cast<int16_t>(fbl::numeric_limits<int16_t>::max() * mixed);
  }
}

void FxProcessor::UpdateReverb(bool enabled, int32_t depth_delta,
                               float gain_delta) {
  reverb_enabled_ = enabled;

  reverb_depth_msec_ =
      fbl::clamp<uint32_t>(reverb_depth_msec_ + depth_delta,
                           MIN_REVERB_DEPTH_MSEC, MAX_REVERB_DEPTH_MSEC);

  reverb_feedback_gain_ =
      fbl::clamp(reverb_feedback_gain_ + gain_delta, MIN_REVERB_FEEDBACK_GAIN,
                 MAX_REVERB_FEEDBACK_GAIN);

  if (enabled) {
    reverb_depth_frames_ = (input_->frame_rate() * reverb_depth_msec_) / 1000u;

    double gain_scale = pow(10.0, reverb_feedback_gain_ / 20.0);
    reverb_feedback_gain_fixed_ = static_cast<uint16_t>(gain_scale * 0x10000);

    printf("%7s: %u mSec %.1f dB\n", "Reverb", reverb_depth_msec_,
           reverb_feedback_gain_);
  } else {
    printf("%7s: Disabled\n", "Reverb");
  }
}

void FxProcessor::UpdateFuzz(bool enabled, float gain_delta, float mix_delta) {
  fuzz_enabled_ = enabled;
  fuzz_gain_ =
      fbl::clamp(fuzz_gain_ + gain_delta, MIN_FUZZ_GAIN, MAX_FUZZ_GAIN);
  fuzz_mix_ = fbl::clamp(fuzz_mix_ + mix_delta, MIN_FUZZ_MIX, MAX_FUZZ_MIX);
  fuzz_mix_inv_ = 1.0f - fuzz_mix_;

  if (enabled) {
    printf("%7s: Gain %.1f Mix %.1f%%\n", "Fuzz", fuzz_gain_,
           fuzz_mix_ * 100.0f);
  } else {
    printf("%7s: Disabled\n", "Fuzz");
  }
}

void FxProcessor::UpdatePreampGain(float delta) {
  preamp_gain_ =
      fbl::clamp(preamp_gain_ + delta, MIN_PREAMP_GAIN, MAX_PREAMP_GAIN);

  double gain_scale = pow(10.0, preamp_gain_ / 20.0);
  preamp_gain_fixed_ =
      static_cast<uint16_t>(gain_scale * (0x1 << PREAMP_GAIN_FRAC_BITS));

  printf("%7s: %.1f dB\n", "PreGain", preamp_gain_);
}

void usage(const char* prog_name) {
  printf("usage: %s [input_dev_num]\n", prog_name);
}

int main(int argc, char** argv) {
  uint32_t input_num = 0;

  if (argc >= 2) {
    if (1 != sscanf(argv[1], "%u", &input_num)) {
      usage(argv[0]);
      return -1;
    }
  }

  zx_status_t res;
  auto input = AudioInput::Create(input_num);

  res = input->Open();
  if (res != ZX_OK) {
    return res;
  }

  // TODO(johngro) : Fetch the supported stream_types from the audio
  // input itself and select from them, do not hardcode this.
  res = input->SetFormat(48000u, NUM_CHANNELS, AUDIO_SAMPLE_FORMAT_16BIT);
  if (res != ZX_OK) {
    return res;
  }

  res = input->GetBuffer(INPUT_BUFFER_MIN_FRAMES, 0u);
  if (res != ZX_OK) {
    return res;
  }

  async::Loop loop(&kAsyncLoopConfigAttachToThread);

  std::unique_ptr<component::StartupContext> startup_context =
      component::StartupContext::CreateFromStartupInfo();

  fuchsia::media::AudioPtr audio =
      startup_context->ConnectToEnvironmentService<fuchsia::media::Audio>();

  FxProcessor fx(fbl::move(input), [&loop]() {
    async::PostTask(loop.dispatcher(), [&loop]() { loop.Quit(); });
  });
  fx.Startup(std::move(audio));

  loop.Run();
  return 0;
}
