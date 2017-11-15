// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <audio-utils/audio-input.h>
#include <inttypes.h>
#include <zircon/compiler.h>
#include <zircon/errors.h>
#include <zircon/types.h>
#include <zx/vmar.h>
#include <zx/vmo.h>
#include <fbl/auto_call.h>
#include <fbl/algorithm.h>
#include <fbl/limits.h>
#include <fbl/unique_ptr.h>
#include <stdio.h>

#include "lib/app/cpp/application_context.h"
#include "lib/app/cpp/connect.h"
#include "lib/media/timeline/timeline_function.h"
#include "lib/fsl/tasks/fd_waiter.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/media/fidl/audio_server.fidl.h"

using media::TimelineFunction;

static constexpr uint32_t INPUT_FRAMES_PER_SEC = 48000u;
static constexpr uint32_t INPUT_BUFFER_LENGTH_MSEC = 10u;
static constexpr uint32_t INPUT_BUFFER_LENGTH_FRAMES = (INPUT_FRAMES_PER_SEC *
                                                        INPUT_BUFFER_LENGTH_MSEC) / 1000u;
static constexpr zx_time_t PROCESS_CHUNK_TIME = ZX_MSEC(1);
static constexpr uint32_t  OUTPUT_BUF_MSEC = 1000;
static constexpr zx_time_t OUTPUT_BUF_TIME = ZX_MSEC(1000);
static constexpr uint32_t OUTPUT_BUFFER_ID = 0;

static constexpr int32_t  MIN_REVERB_DEPTH_MSEC    = 1;
static constexpr int32_t  MAX_REVERB_DEPTH_MSEC    = OUTPUT_BUF_MSEC - 100;
static constexpr int32_t  SMALL_REVERB_DEPTH_STEP  = 1;
static constexpr int32_t  LARGE_REVERB_DEPTH_STEP  = 10;
static constexpr float    MIN_REVERB_FEEDBACK_GAIN = -60.0f;
static constexpr float    MAX_REVERB_FEEDBACK_GAIN = -3.0f;
static constexpr float    SMALL_REVERB_GAIN_STEP   = 0.5;
static constexpr float    LARGE_REVERB_GAIN_STEP   = 2.5;

static constexpr float    MIN_FUZZ_GAIN = 1.0;
static constexpr float    MAX_FUZZ_GAIN = 50.0;
static constexpr float    SMALL_FUZZ_GAIN_STEP = 0.1;
static constexpr float    LARGE_FUZZ_GAIN_STEP = 1.0;
static constexpr float    MIN_FUZZ_MIX = 0.0;
static constexpr float    MAX_FUZZ_MIX = 1.0;
static constexpr float    SMALL_FUZZ_MIX_STEP = 0.01;
static constexpr float    LARGE_FUZZ_MIX_STEP = 0.1;

static constexpr float    MIN_PREAMP_GAIN = -30.0f;
static constexpr float    MAX_PREAMP_GAIN = 20.0f;
static constexpr float    SMALL_PREAMP_GAIN_STEP = 0.1f;
static constexpr float    LARGE_PREAMP_GAIN_STEP = 1.0f;
static constexpr uint32_t PREAMP_GAIN_FRAC_BITS = 12;

using audio::utils::AudioInput;

class FxProcessor {
public:
    FxProcessor(fbl::unique_ptr<AudioInput> input, media::AudioServerPtr audio_server)
        : input_(fbl::move(input)),
          audio_server_(std::move(audio_server)) { }

    void Startup();

private:
    using EffectFn = void (FxProcessor::*)(int16_t* src, int16_t* dst, uint32_t frames);

    static inline float Norm(int16_t value) {
        return (value < 0)
            ? static_cast<float>(value) / fbl::numeric_limits<int16_t>::min()
            : static_cast<float>(value) / fbl::numeric_limits<int16_t>::max();
    }

    static inline float FuzzNorm(float norm_value, float gain) {
        return 1.0f - expf(-norm_value * gain);
    }

    media::MediaPacketPtr CreateOutputPacket();
    void RequestKeystrokeMessage();
    void HandleKeystroke(zx_status_t status, uint32_t events);
    void Shutdown(const char* reason = "unknown");
    void ProcessInput(bool first_time);
    void ProduceOutputPackets(media::MediaPacketPtr* out_pkt1, media::MediaPacketPtr* out_pkt2);
    void ApplyEffect(int16_t* src, uint32_t src_offset, uint32_t src_rb_size,
                     int16_t* dst, uint32_t dst_offset, uint32_t dst_rb_size,
                     uint32_t frames, EffectFn effect);

    void CopyInputEffect(int16_t* src, int16_t* dst, uint32_t frames);
    void PreampInputEffect(int16_t* src, int16_t* dst, uint32_t frames);
    void ReverbMixEffect(int16_t* src, int16_t* dst, uint32_t frames);
    void FuzzEffect(int16_t* src, int16_t* dst, uint32_t frames);
    void MixedFuzzEffect(int16_t* src, int16_t* dst, uint32_t frames);

    fsl::FDWaiter::Callback handle_keystroke_thunk_ =
        [this](zx_status_t status, uint32_t event) {
            HandleKeystroke(status, event);
        };

    void UpdateReverb(bool enabled, int32_t depth_delta = 0, float gain_delta = 0.0f);
    void UpdateFuzz(bool enabled, float gain_delta = 0.0f, float mix_delta = 0.0f);
    void UpdatePreampGain(float delta);

    zx::vmo                             output_buf_vmo_;
    void*                               output_buf_virt_ = nullptr;
    size_t                              output_buf_sz_ = 0;
    uint32_t                            output_buf_frames_ = 0;
    uint64_t                            output_buf_wp_ = 0;
    int64_t                             input_rp_ = 0;
    bool                                shutting_down_ = false;

    bool                                reverb_enabled_ = false;
    int32_t                             reverb_depth_msec_ = 100;
    float                               reverb_feedback_gain_ = -8.0;
    uint32_t                            reverb_depth_frames_;
    uint16_t                            reverb_feedback_gain_fixed_;

    bool                                fuzz_enabled_ = false;
    float                               fuzz_gain_    = 15.0;
    float                               fuzz_mix_     = 1.0;
    float                               fuzz_mix_inv_;

    float                               preamp_gain_ = 5.0;
    uint16_t                            preamp_gain_fixed_;

    fbl::unique_ptr<AudioInput>        input_;
    media::AudioServerPtr               audio_server_;
    media::AudioRendererPtr             output_audio_;
    media::MediaRendererPtr             output_media_;
    media::MediaPacketConsumerPtr       output_consumer_;
    media::MediaTimelineControlPointPtr output_timeline_cp_;
    media::TimelineConsumerPtr          output_timeline_consumer_;
    media::TimelineFunction             clock_mono_to_input_wr_ptr_;
    fsl::FDWaiter                       keystroke_waiter_;
};

void FxProcessor::Startup() {
    auto cleanup = fbl::MakeAutoCall([this] { Shutdown("Startup failure"); });

    if (input_->sample_size() != 2) {
        printf("Invalid input sample size %u\n", input_->sample_size());
        return;
    }

    // Construct the media type we will use to configure the renderer.
    media::AudioMediaTypeDetailsPtr audio_details = media::AudioMediaTypeDetails::New();
    audio_details->sample_format = media::AudioSampleFormat::SIGNED_16;
    audio_details->channels = input_->channel_cnt();
    audio_details->frames_per_second = input_->frame_rate();

    media::MediaTypeDetailsPtr media_details = media::MediaTypeDetails::New();
    media_details->set_audio(std::move(audio_details));

    media::MediaTypePtr media_type = media::MediaType::New();
    media_type->medium = media::MediaTypeMedium::AUDIO;
    media_type->details = std::move(media_details);
    media_type->encoding = media::MediaType::kAudioEncodingLpcm;

    // Create a renderer.  Setup connection error handlers.
    audio_server_->CreateRenderer(output_audio_.NewRequest(), output_media_.NewRequest());

    output_audio_.set_connection_error_handler([this]() {
        Shutdown("AudioRenderer connection closed");
    });

    output_media_.set_connection_error_handler([this]() {
        Shutdown("MediaRenderer connection closed");
    });

    // Set the media type
    output_media_->SetMediaType(std::move(media_type));

    // Fetch the packet consumer and timeline interfaces, and set connection
    // error handlers for them as well.
    output_media_->GetPacketConsumer(output_consumer_.NewRequest());
    output_media_->GetTimelineControlPoint(output_timeline_cp_.NewRequest());

    output_consumer_.set_connection_error_handler([this]() {
        Shutdown("MediaConsumer connection closed");
    });

    output_timeline_cp_.set_connection_error_handler([this]() {
        Shutdown("TimelineControlPoint connection closed");
    });

    output_timeline_cp_->GetTimelineConsumer(output_timeline_consumer_.NewRequest());
    output_timeline_consumer_.set_connection_error_handler([this]() {
        Shutdown("TimelineConsumer connection closed");
    });

    // Construct the VMO we will use as our mixing buffer and that we will use
    // to send data to the audio renderer.  Map it into our address space, fill
    // it with silence, then duplicate it and assign it to our media consumer
    // channel.
    zx_status_t res;
    output_buf_frames_ = static_cast<uint32_t>((OUTPUT_BUF_TIME * input_->frame_rate())
                                               / 1000000000u);
    output_buf_sz_ = static_cast<size_t>(input_->frame_sz()) * output_buf_frames_;

    res = zx::vmo::create(output_buf_sz_, 0, &output_buf_vmo_);
    if (res != ZX_OK) {
        printf("Failed to create %zu byte output buffer vmo (res %d)\n", output_buf_sz_, res);
        return;
    }

    uintptr_t tmp;
    res = zx::vmar::root_self().map(0, output_buf_vmo_,
                                    0, output_buf_sz_,
                                    ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE, &tmp);

    if (res != ZX_OK) {
        printf("Failed to map %zu byte output buffer vmo (res %d)\n", output_buf_sz_, res);
        return;
    }
    output_buf_virt_ = reinterpret_cast<void*>(tmp);

    zx::vmo rend_vmo;
    res = output_buf_vmo_.duplicate(ZX_RIGHTS_BASIC | ZX_RIGHT_READ | ZX_RIGHT_MAP, &rend_vmo);
    if (res != ZX_OK) {
        printf("Failed to duplicate output buffer vmo handle (res %d)\n", res);
        return;
    }

    output_consumer_->AddPayloadBuffer(OUTPUT_BUFFER_ID, std::move(rend_vmo));

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
    int64_t fifo_frames = ((input_->fifo_depth() + input_->frame_sz() - 1) / input_->frame_sz());

    // TODO(johngro): Switch audio start times to always be expressed in clock monotonic units,
    // instead of ticks.  Right now, we are making assumptions about the relationship between ticks
    // and clock monotonic which we should not be making.
    TimelineFunction clock_mono_to_ticks(0, 0, 1000000000u, zx_ticks_per_second());
    TimelineFunction ticks_to_input_wr_ptr(input_->start_ticks(), -fifo_frames,
                                           zx_ticks_per_second(), input_->frame_rate());

    clock_mono_to_input_wr_ptr_ =
        TimelineFunction::Compose(ticks_to_input_wr_ptr, clock_mono_to_ticks);

    // Compute the time at which the input will have a chunk of data to process,
    // and schedule a DPC for then.
    int64_t first_process_frames = (PROCESS_CHUNK_TIME * input_->frame_rate()) / 1000000000;
    auto first_process_time = fxl::TimePoint::FromEpochDelta(fxl::TimeDelta::FromNanoseconds(
                clock_mono_to_input_wr_ptr_.ApplyInverse(first_process_frames)));

    fsl::MessageLoop::GetCurrent()->task_runner()->PostTaskForTime(
        [this]() { ProcessInput(true); },
        first_process_time);

    // Success.  Print out the usage message, and force an update of effect
    // parameters (which will also print their status).
    printf("Welcome to FX.  Keybindings are as follows.\n"
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

void FxProcessor::RequestKeystrokeMessage() {
    keystroke_waiter_.Wait(handle_keystroke_thunk_, STDIN_FILENO, POLLIN);
}

void FxProcessor::HandleKeystroke(zx_status_t status, uint32_t events) {
    if (shutting_down_) return;

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
    case 'Q': Shutdown("User requested"); break;

    case 'r':
    case 'R': UpdateReverb(!reverb_enabled_); break;
    case 'i': UpdateReverb(true,  0,  SMALL_REVERB_GAIN_STEP); break;
    case 'I': UpdateReverb(true,  0,  LARGE_REVERB_GAIN_STEP); break;
    case 'k': UpdateReverb(true,  0, -SMALL_REVERB_GAIN_STEP); break;
    case 'K': UpdateReverb(true,  0, -LARGE_REVERB_GAIN_STEP); break;
    case 'l': UpdateReverb(true,  SMALL_REVERB_DEPTH_STEP, 0.0f); break;
    case 'L': UpdateReverb(true,  LARGE_REVERB_DEPTH_STEP, 0.0f); break;
    case 'j': UpdateReverb(true, -SMALL_REVERB_DEPTH_STEP, 0.0f); break;
    case 'J': UpdateReverb(true, -LARGE_REVERB_DEPTH_STEP, 0.0f); break;

    case '[': UpdatePreampGain(-SMALL_PREAMP_GAIN_STEP); break;
    case '{': UpdatePreampGain(-LARGE_PREAMP_GAIN_STEP); break;
    case ']': UpdatePreampGain( SMALL_PREAMP_GAIN_STEP); break;
    case '}': UpdatePreampGain( LARGE_PREAMP_GAIN_STEP); break;

    case 'f':
    case 'F': UpdateFuzz(!fuzz_enabled_); break;
    case 'd': UpdateFuzz(true, 0.0,  SMALL_FUZZ_MIX_STEP); break;
    case 'D': UpdateFuzz(true, 0.0,  LARGE_FUZZ_MIX_STEP); break;
    case 'a': UpdateFuzz(true, 0.0, -SMALL_FUZZ_MIX_STEP); break;
    case 'A': UpdateFuzz(true, 0.0, -LARGE_FUZZ_MIX_STEP); break;
    case 'w': UpdateFuzz(true,  SMALL_FUZZ_GAIN_STEP); break;
    case 'W': UpdateFuzz(true,  LARGE_FUZZ_GAIN_STEP); break;
    case 's': UpdateFuzz(true, -SMALL_FUZZ_GAIN_STEP); break;
    case 'S': UpdateFuzz(true, -LARGE_FUZZ_GAIN_STEP); break;

    default:
        break;
    }

    RequestKeystrokeMessage();
}

media::MediaPacketPtr FxProcessor::CreateOutputPacket() {
    // Create a media packet for output and fill out the default fields.  The
    // user still needs to fill out the position of the media in the ring
    // buffer, and the PTS of the packet.
    media::MediaPacketPtr pkt = media::MediaPacket::New();

    pkt->pts_rate_ticks = input_->frame_rate();
    pkt->pts_rate_seconds = 1u;
    pkt->flags = 0u;
    pkt->payload_buffer_id = OUTPUT_BUFFER_ID;

    return pkt;
}

void FxProcessor::Shutdown(const char* reason) {
    printf("Shutting down, reason = \"%s\"\n", reason);
    shutting_down_ = true;
    output_timeline_cp_.reset();
    output_timeline_consumer_.reset();
    output_consumer_.reset();
    output_audio_.reset();
    output_media_.reset();
    audio_server_.reset();
    input_.reset();
    fsl::MessageLoop::GetCurrent()->PostQuitTask();
}

void FxProcessor::ProcessInput(bool first_time) {
    media::MediaPacketPtr pkt1, pkt2;

    // Produce the output packet(s)  If we do not produce any packets, something
    // is very wrong and we are in the process of shutting down, so just get out
    // now.
    ProduceOutputPackets(&pkt1, &pkt2);
    if (pkt1.is_null()) return;

    // Send the packet(s)
    output_consumer_->SupplyPacket(std::move(pkt1), [](media::MediaPacketDemandPtr) {} );
    if (!pkt2.is_null()) {
        output_consumer_->SupplyPacket(std::move(pkt2), [](media::MediaPacketDemandPtr) {} );
    }

    // if this is the first time we are processing input, start the clock.
    if (first_time) {
        // TODO(johngro) : this lead time amount should not be arbitrary... it
        // needs to be based on the requirements of the renderer at the moment.
        media::TimelineTransformPtr start = media::TimelineTransform::New();
        start->reference_time = zx_time_get(ZX_CLOCK_MONOTONIC) + ZX_MSEC(8);
        start->subject_time = 0;
        start->reference_delta = 1u;
        start->subject_delta = 1u;
        output_timeline_consumer_->SetTimelineTransformNoReply(std::move(start));
    }

    // If the input has been closed by the driver, shutdown.
    if (input_->IsRingBufChannelConnected()) {
        Shutdown("Input unplugged");
        return;
    }

    // Schedule our next processing callback.
    fsl::MessageLoop::GetCurrent()->task_runner()->PostDelayedTask(
        [this]() { ProcessInput(false); },
        fxl::TimeDelta::FromNanoseconds(PROCESS_CHUNK_TIME));
}

void FxProcessor::ProduceOutputPackets(media::MediaPacketPtr* out_pkt1,
                                       media::MediaPacketPtr* out_pkt2) {
    // Figure out how much input data we have to process.
    zx_time_t now = zx_time_get(ZX_CLOCK_MONOTONIC);
    int64_t input_wp = clock_mono_to_input_wr_ptr_.Apply(now);
    if (input_wp <= input_rp_) {
        printf("input wp <= rp (wp %" PRId64 " rp %" PRId64 " now %" PRIu64 ")\n",
                input_wp, input_rp_, now);
        Shutdown("Failed to produce output packet");
        return;
    }

    int64_t todo64 = input_wp - input_rp_;
    if (todo64 > INPUT_BUFFER_LENGTH_FRAMES) {
        printf("Fell behind by more than the input buffer size "
               "(todo %" PRId64 " buflen %u\n",
                todo64, INPUT_BUFFER_LENGTH_FRAMES);
        Shutdown("Failed to produce output packet");
        return;
    }

    uint32_t todo         = static_cast<uint32_t>(todo64);
    uint32_t input_start  = static_cast<uint32_t>(input_rp_) % INPUT_BUFFER_LENGTH_FRAMES;
    uint32_t output_start = output_buf_wp_ % output_buf_frames_;
    uint32_t output_space = output_buf_frames_ - output_start;

    // Create the actual output packet(s) based on the amt of data we need to
    // send and the current position of the write pointer in the output ring
    // buffer.
    uint32_t pkt1_frames = fbl::min<uint32_t>(output_space, todo);
    *out_pkt1 = CreateOutputPacket();
    (*out_pkt1)->pts = output_buf_wp_;
    (*out_pkt1)->payload_offset = output_start * input_->frame_sz();
    (*out_pkt1)->payload_size = pkt1_frames * input_->frame_sz();

    // Does this job wrap the ring?  If so, we need to create 2 packets instead
    // of 1.
    if (pkt1_frames < todo) {
        *out_pkt2 = CreateOutputPacket();
        (*out_pkt2)->pts = output_buf_wp_ + pkt1_frames;
        (*out_pkt2)->payload_offset = 0;
        (*out_pkt2)->payload_size = (todo - pkt1_frames) * input_->frame_sz();
    }

    // Now actually apply the effects.  Start by just copying the input to the
    // output.
    auto input_base  = reinterpret_cast<int16_t*>(input_->ring_buffer());
    auto output_base = reinterpret_cast<int16_t*>(output_buf_virt_);
    ApplyEffect(input_base,  input_start,  INPUT_BUFFER_LENGTH_FRAMES,
                output_base, output_start, output_buf_frames_,
                todo,
                (preamp_gain_ == 0.0) ? &FxProcessor::CopyInputEffect
                                      : &FxProcessor::PreampInputEffect);

    // If enabled, add some fuzz
    if (fuzz_enabled_ && (fuzz_mix_ >= 0.01f)) {
        ApplyEffect(output_base, output_start, output_buf_frames_,
                    output_base, output_start, output_buf_frames_,
                    todo,
                    (fuzz_mix_ <= 0.99f) ? &FxProcessor::MixedFuzzEffect
                                         : &FxProcessor::FuzzEffect);
    }

    // If enabled, add some reverb.
    if (reverb_enabled_ &&  (reverb_feedback_gain_fixed_ > 0)) {
        uint32_t reverb_start = output_start + (output_buf_frames_ - reverb_depth_frames_);
        if (reverb_start >= output_buf_frames_)
            reverb_start -= output_buf_frames_;

        ApplyEffect(output_base, reverb_start, output_buf_frames_,
                    output_base, output_start, output_buf_frames_,
                    todo, &FxProcessor::ReverbMixEffect);
    }

    // Finally, update our input read pointer and our output write pointer.
    input_rp_ += todo;
    output_buf_wp_ += todo;
}

void FxProcessor::ApplyEffect(int16_t* src, uint32_t src_offset, uint32_t src_rb_size,
                              int16_t* dst, uint32_t dst_offset, uint32_t dst_rb_size,
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
        ((*this).*(effect))(src + src_offset, dst + dst_offset, todo);

        src_offset = (src_space > todo) ? (src_offset + todo) : 0;
        dst_offset = (dst_space > todo) ? (dst_offset + todo) : 0;
        frames -= todo;

    }
}

void FxProcessor::CopyInputEffect(int16_t* src, int16_t* dst, uint32_t frames) {
    ::memcpy(dst, src, frames * sizeof(*dst));
}

void FxProcessor::PreampInputEffect(int16_t* src, int16_t* dst, uint32_t frames) {
    for (uint32_t i = 0; i < frames; ++i) {
        int32_t tmp = src[i];
        tmp *= preamp_gain_fixed_;
        tmp >>= PREAMP_GAIN_FRAC_BITS;
        tmp = fbl::clamp<int32_t>(tmp,
                                   fbl::numeric_limits<int16_t>::min(),
                                   fbl::numeric_limits<int16_t>::max());
        dst[i] = static_cast<int16_t>(tmp);
    }
}

void FxProcessor::ReverbMixEffect(int16_t* src, int16_t* dst, uint32_t frames) {
    // TODO(johngro): We should probably process everything into an intermediate
    // 32 bit (or even 64 bit or float) buffer, and clamp after the fact.
    for (uint32_t i = frames; i > 0; ) {
        --i;

        int32_t tmp = src[i];
        tmp *= reverb_feedback_gain_fixed_;
        tmp >>= 16;
        tmp += dst[i];
        tmp = fbl::clamp<int32_t>(tmp,
                                   fbl::numeric_limits<int16_t>::min(),
                                   fbl::numeric_limits<int16_t>::max());
        dst[i] = static_cast<int16_t>(tmp);
    }
}

void FxProcessor::FuzzEffect(int16_t* src, int16_t* dst, uint32_t frames) {
    for (uint32_t i = 0; i < frames; ++i) {
        float norm = FuzzNorm(Norm(src[i]), fuzz_gain_);
        dst[i] = (src[i] < 0)
               ? static_cast<int16_t>(fbl::numeric_limits<int16_t>::min() * norm)
               : static_cast<int16_t>(fbl::numeric_limits<int16_t>::max() * norm);
    }
}

void FxProcessor::MixedFuzzEffect(int16_t* src, int16_t* dst, uint32_t frames) {
    for (uint32_t i = 0; i < frames; ++i) {
        float norm  = Norm(src[i]);
        float fnorm = FuzzNorm(norm, fuzz_gain_);
        float mixed = ((fnorm * fuzz_mix_) + (norm * fuzz_mix_inv_));
        dst[i] = (src[i] < 0)
               ? static_cast<int16_t>(fbl::numeric_limits<int16_t>::min() * mixed)
               : static_cast<int16_t>(fbl::numeric_limits<int16_t>::max() * mixed);
    }
}

void FxProcessor::UpdateReverb(bool enabled, int32_t depth_delta, float gain_delta) {
    reverb_enabled_ = enabled;

    reverb_depth_msec_ = fbl::clamp<uint32_t>(reverb_depth_msec_ + depth_delta,
                                               MIN_REVERB_DEPTH_MSEC,
                                               MAX_REVERB_DEPTH_MSEC);

    reverb_feedback_gain_ = fbl::clamp(reverb_feedback_gain_ + gain_delta,
                                        MIN_REVERB_FEEDBACK_GAIN,
                                        MAX_REVERB_FEEDBACK_GAIN);

    if (enabled) {
        reverb_depth_frames_ = (input_->frame_rate() * reverb_depth_msec_) / 1000u;

        double gain_scale = pow(10.0, reverb_feedback_gain_ / 20.0);
        reverb_feedback_gain_fixed_ = static_cast<uint16_t>(gain_scale * 0x10000);

        printf("%7s: %u mSec %.1f dB\n", "Reverb", reverb_depth_msec_, reverb_feedback_gain_);
    } else {
        printf("%7s: Disabled\n", "Reverb");
    }
}

void FxProcessor::UpdateFuzz(bool enabled, float gain_delta, float mix_delta) {
    fuzz_enabled_ = enabled;
    fuzz_gain_    = fbl::clamp(fuzz_gain_ + gain_delta, MIN_FUZZ_GAIN, MAX_FUZZ_GAIN);
    fuzz_mix_     = fbl::clamp(fuzz_mix_  + mix_delta,  MIN_FUZZ_MIX,  MAX_FUZZ_MIX);
    fuzz_mix_inv_ = 1.0f - fuzz_mix_;

    if (enabled) {
        printf("%7s: Gain %.1f Mix %.1f%%\n", "Fuzz", fuzz_gain_, fuzz_mix_ * 100.0f);
    } else {
        printf("%7s: Disabled\n", "Fuzz");
    }
}

void FxProcessor::UpdatePreampGain(float delta) {
    preamp_gain_ = fbl::clamp(preamp_gain_ + delta, MIN_PREAMP_GAIN, MAX_PREAMP_GAIN);

    double gain_scale = pow(10.0, preamp_gain_ / 20.0);
    preamp_gain_fixed_ = static_cast<uint16_t>(gain_scale * (0x1 << PREAMP_GAIN_FRAC_BITS));

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
    if (res != ZX_OK)
        return res;

    // TODO(johngro) : Fetch the supported formats from the audio
    // input itself and select from them, do not hardcode this.
    res = input->SetFormat(48000u, 1u, AUDIO_SAMPLE_FORMAT_16BIT);
    if (res != ZX_OK)
        return res;

    res = input->GetBuffer(INPUT_BUFFER_LENGTH_FRAMES, 0u);
    if (res != ZX_OK)
        return res;

    fsl::MessageLoop loop;

    std::unique_ptr<app::ApplicationContext> application_context =
        app::ApplicationContext::CreateFromStartupInfo();

    media::AudioServerPtr audio_server =
        application_context->ConnectToEnvironmentService<media::AudioServer>();

    FxProcessor fx(fbl::move(input), std::move(audio_server));
    fx.Startup();

    loop.Run();
    return 0;
}
