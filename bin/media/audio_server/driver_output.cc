// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/audio_server/driver_output.h"

#include <iomanip>

#include <audio-proto-utils/format-utils.h>
#include <dispatcher-pool/dispatcher-channel.h>
#include <fbl/atomic.h>
#include <fbl/auto_call.h>
#include <fbl/limits.h>
#include <zircon/process.h>

#include "garnet/bin/media/audio_server/audio_device_manager.h"
#include "garnet/lib/media/wav_writer/wav_writer.h"
#include "lib/fxl/logging.h"

constexpr bool VERBOSE_TIMING_DEBUG = false;

namespace media {
namespace audio {

static constexpr uint32_t kDefaultFramesPerSec = 48000;
static constexpr uint32_t kDefaultChannelCount = 2;
static constexpr fuchsia::media::AudioSampleFormat kDefaultAudioFmt =
    fuchsia::media::AudioSampleFormat::SIGNED_16;
static constexpr int64_t kDefaultLowWaterNsec = ZX_MSEC(20);
static constexpr int64_t kDefaultHighWaterNsec = ZX_MSEC(30);
static constexpr int64_t kDefaultMaxRetentionNsec = ZX_MSEC(60);
static constexpr int64_t kDefaultRetentionGapNsec = ZX_MSEC(10);
static constexpr zx_duration_t kUnderflowCooldown = ZX_SEC(1);

static fbl::atomic<zx_txid_t> TXID_GEN(1);
static thread_local zx_txid_t TXID = TXID_GEN.fetch_add(1);

fbl::RefPtr<AudioOutput> DriverOutput::Create(zx::channel stream_channel,
                                              AudioDeviceManager* manager) {
  return fbl::AdoptRef(new DriverOutput(manager, fbl::move(stream_channel)));
}

DriverOutput::DriverOutput(AudioDeviceManager* manager,
                           zx::channel initial_stream_channel)
    : StandardOutputBase(manager),
      initial_stream_channel_(fbl::move(initial_stream_channel)) {}

DriverOutput::~DriverOutput() { wav_writer_.Close(); }

zx_status_t DriverOutput::Init() {
  FXL_DCHECK(state_ == State::Uninitialized);

  zx_status_t res = StandardOutputBase::Init();
  if (res != ZX_OK) {
    return res;
  }

  res = driver_->Init(fbl::move(initial_stream_channel_));
  if (res != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to initialize driver object (res " << res << ")";
    return res;
  }

  state_ = State::FormatsUnknown;
  return res;
}

void DriverOutput::OnWakeup() {
  // If we are not in the FormatsUnknown state, then we have already started the
  // state machine.  There is (currently) nothing else to do here.
  FXL_DCHECK(state_ != State::Uninitialized);
  if (state_ != State::FormatsUnknown) {
    return;
  }

  // Kick off the process of driver configuration by requesting the basic driver
  // info, which will include the modes which the driver supports.
  driver_->GetDriverInfo();
  state_ = State::FetchingFormats;
}

void DriverOutput::Cleanup() {
  driver_->Cleanup();
  StandardOutputBase::Cleanup();
}

bool DriverOutput::StartMixJob(MixJob* job, fxl::TimePoint process_start) {
  if (state_ != State::Started) {
    FXL_LOG(ERROR) << "Bad state during StartMixJob "
                   << static_cast<uint32_t>(state_);
    state_ = State::Shutdown;
    ShutdownSelf();
    return false;
  }

  // TODO(johngro): Depending on policy, use send appropriate commands to the
  // driver to control gain as well.  Some policy settings which might be useful
  // include...
  //
  // ++ Never use HW gain, even if it supports it.
  // ++ Always use HW gain when present, regarless of its limitations.
  // ++ Use HW gain when present, but only if it reaches a minimum bar of
  //    functionality.
  // ++ Implement a hybrid of HW/SW gain.  IOW - Get as close as possible to our
  //    target using HW, and then get the rest of the way there using SW
  //    scaling.  This approach may end up being unreasonably tricky as we may
  //    not be able to synchronize the HW and SW changes in gain well enough to
  //    avoid strange situations where the jumps in one direction (because of
  //    the SW component), and then in the other (as the HW gain command takes
  //    affect).
  //
  if (device_settings_ != nullptr) {
    AudioDeviceSettings::GainState cur_gain_state;
    device_settings_->SnapshotGainState(&cur_gain_state);
    job->sw_output_db_gain = cur_gain_state.db_gain;
    job->sw_output_muted = cur_gain_state.muted;
  } else {
    job->sw_output_db_gain = 0.0f;
    job->sw_output_muted = true;
  }

  FXL_DCHECK(driver_ring_buffer() != nullptr);
  int64_t now = process_start.ToEpochDelta().ToNanoseconds();
  const auto& cm2rd_pos = clock_mono_to_ring_buf_pos_frames_;
  const auto& cm2frames = cm2rd_pos.rate();
  const auto& rb = *driver_ring_buffer();
  uint32_t fifo_frames = driver_->fifo_depth_frames();

  // If frames_to_mix_ is 0, then this is the start of a new cycle.  Check to
  // make sure we have not underflowed while we were sleeping, then compute how
  // many frames we need to mix during this wakeup cycle, and return a job
  // containing the largest contiguous buffer we can mix during this phase of
  // this cycle.
  if (!frames_to_mix_) {
    int64_t rd_ptr_frames = cm2rd_pos.Apply(now);
    int64_t fifo_threshold = rd_ptr_frames + fifo_frames;

    if (fifo_threshold >= frames_sent_) {
      if (!underflow_start_time_) {
        // If this was the first time we missed our limit, log a message, mark
        // the start time of the underflow event, and fill our entire ring
        // buffer with silence.
        int64_t rd_limit_miss = rd_ptr_frames - frames_sent_;
        int64_t fifo_limit_miss = rd_limit_miss + fifo_frames;
        int64_t low_water_limit_miss = rd_limit_miss + low_water_frames_;

        FXL_LOG(ERROR)
            << "UNDERFLOW: Missed mix target by (Rd, Fifo, LowWater) = ("
            << std::fixed << std::setprecision(3)
            << cm2frames.Inverse().Scale(rd_limit_miss) / 1000000.0 << ", "
            << cm2frames.Inverse().Scale(fifo_limit_miss) / 1000000.0 << ", "
            << cm2frames.Inverse().Scale(low_water_limit_miss) / 1000000.0
            << ") mSec.  Cooling down for at least "
            << kUnderflowCooldown / 1000000.0 << " mSec.";

        underflow_start_time_ = now;
        output_formatter_->FillWithSilence(rb.virt(), rb.frames());
        zx_cache_flush(rb.virt(), rb.size(), ZX_CACHE_FLUSH_DATA);

        wav_writer_.Close();
      }

      // Regardless of whether this was the first or a subsequent underflow,
      // update the cooldown deadline (the time at which we will start producing
      // frames again, provided we don't underflow again)
      underflow_cooldown_deadline_ = zx_deadline_after(kUnderflowCooldown);
    }

    int64_t fill_target =
        fifo_frames + cm2rd_pos.Apply(now + kDefaultHighWaterNsec);

    // Are we in the middle of an underflow cooldown?  If so, check to see if we
    // have recovered yet.
    if (underflow_start_time_) {
      if (static_cast<zx_time_t>(now) < underflow_cooldown_deadline_) {
        // Looks like we have not recovered yet.  Pretend to have produced the
        // frames we were going to produce and schedule the next wakeup time.
        frames_sent_ = fill_target;
        ScheduleNextLowWaterWakeup();
        return false;
      } else {
        // Looks like we recovered.  Log and go back to mixing.
        FXL_LOG(INFO) << "UNDERFLOW: Recovered after " << std::fixed
                      << std::setprecision(3)
                      << (now - underflow_start_time_) / 1000000.0 << " mSec.";
        underflow_start_time_ = 0;
        underflow_cooldown_deadline_ = 0;
      }
    }

    int64_t frames_in_flight = frames_sent_ - rd_ptr_frames;
    FXL_DCHECK((frames_in_flight >= 0) && (frames_in_flight <= rb.frames()));
    FXL_DCHECK(frames_sent_ <= fill_target);
    int64_t desired_frames = fill_target - frames_sent_;

    // If we woke up too early to have any work to do, just get out now.
    if (desired_frames == 0) {
      return false;
    }

    uint32_t rb_space = rb.frames() - static_cast<uint32_t>(frames_in_flight);
    if (desired_frames > rb.frames()) {
      FXL_LOG(ERROR) << "Fatal underflow: want to produce " << desired_frames
                     << " but the ring buffer is only " << rb.frames()
                     << " frames long.";
      return false;
    }

    frames_to_mix_ =
        static_cast<uint32_t>(fbl::min<int64_t>(rb_space, desired_frames));
  }

  uint32_t to_mix = frames_to_mix_;
  uint32_t wr_ptr = frames_sent_ % rb.frames();
  uint32_t contig_space = rb.frames() - wr_ptr;

  if (to_mix > contig_space) {
    to_mix = contig_space;
  }

  job->buf = rb.virt() + (rb.frame_size() * wr_ptr);
  job->buf_frames = to_mix;
  job->start_pts_of = frames_sent_;
  job->local_to_output = &cm2rd_pos;
  job->local_to_output_gen = clock_mono_to_ring_buf_pos_id_.get();

  return true;
}

bool DriverOutput::FinishMixJob(const MixJob& job) {
  const auto& rb = driver_ring_buffer();
  FXL_DCHECK(rb != nullptr);
  size_t buf_len = job.buf_frames * rb->frame_size();

  wav_writer_.Write(job.buf, buf_len);
  wav_writer_.UpdateHeader();
  zx_cache_flush(job.buf, buf_len, ZX_CACHE_FLUSH_DATA);

  if (VERBOSE_TIMING_DEBUG) {
    const auto& cm2rd_pos = clock_mono_to_ring_buf_pos_frames_;
    uint32_t fifo_frames = driver_->fifo_depth_frames();
    int64_t now = fxl::TimePoint::Now().ToEpochDelta().ToNanoseconds();
    int64_t rd_ptr_frames = cm2rd_pos.Apply(now);
    int64_t playback_lead_start = frames_sent_ - rd_ptr_frames;
    int64_t playback_lead_end = playback_lead_start + job.buf_frames;
    int64_t dma_lead_start = playback_lead_start - fifo_frames;
    int64_t dma_lead_end = playback_lead_end - fifo_frames;

    FXL_LOG(INFO) << "PLead [" << std::setw(4) << playback_lead_start << ", "
                  << std::setw(4) << playback_lead_end << "] DLead ["
                  << std::setw(4) << dma_lead_start << ", " << std::setw(4)
                  << dma_lead_end << "]";
  }

  FXL_DCHECK(frames_to_mix_ >= job.buf_frames);
  frames_sent_ += job.buf_frames;
  frames_to_mix_ -= job.buf_frames;

  if (!frames_to_mix_) {
    ScheduleNextLowWaterWakeup();
    return false;
  }

  return true;
}

void DriverOutput::ApplyGainLimits(::fuchsia::media::AudioGainInfo* in_out_info,
                                   uint32_t set_flags) {
  // See the comment at the start of StartMixJob.  The actual limits we set here
  // are going to eventually depend on what our HW gain control capabilities
  // are, and how we choose to apply them (based on policy)
  FXL_DCHECK(in_out_info != nullptr);

  // We do not currently allow more than unity gain for audio outputs.
  if (in_out_info->db_gain > 0.0) {
    in_out_info->db_gain = 0;
  }

  // Audio outputs should never support AGC
  in_out_info->flags &= ~(::fuchsia::media::AudioGainInfoFlag_AgcEnabled);
}

void DriverOutput::ScheduleNextLowWaterWakeup() {
  // Schedule the next callback for when we are at the low water mark behind
  // the write pointer.
  const auto& cm2rd_pos = clock_mono_to_ring_buf_pos_frames_;
  int64_t low_water_frames = frames_sent_ - low_water_frames_;
  int64_t low_water_time = cm2rd_pos.ApplyInverse(low_water_frames);
  SetNextSchedTime(fxl::TimePoint::FromEpochDelta(
      fxl::TimeDelta::FromNanoseconds(low_water_time)));
}

void DriverOutput::OnDriverInfoFetched() {
  auto cleanup = fbl::MakeAutoCall([this]() FXL_NO_THREAD_SAFETY_ANALYSIS {
    state_ = State::Shutdown;
    ShutdownSelf();
  });

  if (state_ != State::FetchingFormats) {
    FXL_LOG(INFO) << "Unexpected GetFormatsComplete while in state "
                  << static_cast<uint32_t>(state_);
    return;
  }

  zx_status_t res;

  // TODO(johngro): Don't use hardcoded defaults here.  Try to pick the best
  // match among the formats supported by the driver.
  uint32_t pref_fps = kDefaultFramesPerSec;
  uint32_t pref_chan = kDefaultChannelCount;
  fuchsia::media::AudioSampleFormat pref_fmt = kDefaultAudioFmt;
  zx_duration_t min_rb_duration = kDefaultHighWaterNsec +
                                  kDefaultMaxRetentionNsec +
                                  kDefaultRetentionGapNsec;

  res = SelectBestFormat(driver_->format_ranges(), &pref_fps, &pref_chan,
                         &pref_fmt);

  if (res != ZX_OK) {
    FXL_LOG(ERROR) << "Output: cannot match a driver format to this request: "
                   << pref_fps << " Hz, " << pref_chan
                   << "-channel, sample format 0x" << std::hex
                   << static_cast<uint32_t>(pref_fmt);
    return;
  }

  FXL_LOG(INFO) << "Output: configuring the following best-fit format: "
                << pref_fps << " Hz, " << pref_chan
                << "-channel, sample format 0x" << std::hex
                << static_cast<uint32_t>(pref_fmt);

  TimelineRate ns_to_frames(pref_fps, ZX_SEC(1));
  int64_t retention_frames = ns_to_frames.Scale(kDefaultMaxRetentionNsec);
  FXL_DCHECK(retention_frames != TimelineRate::kOverflow);
  FXL_DCHECK(retention_frames <= std::numeric_limits<uint32_t>::max());
  driver_->SetEndFenceToStartFenceFrames(
      static_cast<uint32_t>(retention_frames));

  // Select our output formatter
  fuchsia::media::AudioMediaTypeDetailsPtr config(
      fuchsia::media::AudioMediaTypeDetails::New());
  config->frames_per_second = pref_fps;
  config->channels = pref_chan;
  config->sample_format = pref_fmt;

  output_formatter_ = OutputFormatter::Select(config);
  if (!output_formatter_) {
    FXL_LOG(ERROR) << "Output: OutputFormatter cannot support this request: "
                   << pref_fps << " Hz, " << pref_chan
                   << "-channel, sample format 0x" << std::hex
                   << static_cast<uint32_t>(pref_fmt);
    return;
  }

  // Start the process of configuring our driver
  res = driver_->Configure(pref_fps, pref_chan, pref_fmt, min_rb_duration);
  if (res != ZX_OK) {
    FXL_LOG(ERROR) << "Output: failed to configure driver for: " << pref_fps
                   << " Hz, " << pref_chan << "-channel, sample format 0x"
                   << std::hex << static_cast<uint32_t>(pref_fmt) << " (res "
                   << std::dec << res << ")";
    return;
  }

  wav_writer_.Initialize(nullptr, pref_fmt, pref_chan, pref_fps,
                         driver_->bytes_per_frame() * 8 / pref_chan);

  // Let the AudioDeviceManager that we are ready to be added to the set of
  // active audio devices.
  ActivateSelf();

  // Success, wait until configuration completes.
  state_ = State::Configuring;
  cleanup.cancel();
}

void DriverOutput::OnDriverConfigComplete() {
  auto cleanup = fbl::MakeAutoCall([this]() FXL_NO_THREAD_SAFETY_ANALYSIS {
    state_ = State::Shutdown;
    ShutdownSelf();
  });

  if (state_ != State::Configuring) {
    FXL_LOG(ERROR) << "Unexpected ConfigComplete while in state "
                   << static_cast<uint32_t>(state_);
    return;
  }

  // Now that our driver is completely configured, we should have all the info
  // we need in order to compute the minimum clock lead time requrirement for
  // this output.
  int64_t fifo_depth_nsec = TimelineRate::Scale(
      driver_->fifo_depth_frames(), ZX_SEC(1), driver_->frames_per_sec());
  min_clock_lead_time_nsec_ =
      driver_->external_delay_nsec() + fifo_depth_nsec + kDefaultHighWaterNsec;

  // Fill our brand new ring buffer with silence
  FXL_CHECK(driver_ring_buffer() != nullptr);
  const auto& rb = *driver_ring_buffer();
  FXL_DCHECK(output_formatter_ != nullptr);
  FXL_DCHECK(rb.virt() != nullptr);
  output_formatter_->FillWithSilence(rb.virt(), rb.frames());

  // Set up the intermediate buffer at the StandardOutputBase level
  //
  // TODO(johngro): The intermediate buffer probably does not need to be as
  // large as the entire ring buffer.  Consider limiting this to be something
  // only slightly larger than a nominal mix job.
  SetupMixBuffer(rb.frames());

  // Start the ring buffer running
  //
  // TODO(johngro) : Don't actually start things up here.  We should start only
  // when we have clients with work to do, and we should stop when we have no
  // work to do.  See MTWN-5
  zx_status_t res = driver_->Start();
  if (res != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to start ring buffer (res = " << res << ")";
    return;
  }

  // Start monitoring plug state.
  res = driver_->SetPlugDetectEnabled(true);
  if (res != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to enable plug detection (res = " << res << ")";
    return;
  }

  // Success
  state_ = State::Starting;
  cleanup.cancel();
}

void DriverOutput::OnDriverStartComplete() {
  if (state_ != State::Starting) {
    FXL_LOG(ERROR) << "Unexpected StartComplete while in state "
                   << static_cast<uint32_t>(state_);
    return;
  }

  // Compute the transformation from clock mono to the ring buffer read position
  // in frames, rounded up.  Then compute our low water mark (in frames) and
  // where we want to start mixing.  Finally kick off the mixing engine by
  // manually calling Process.
  uint32_t bytes_per_frame = driver_->bytes_per_frame();
  int64_t offset = static_cast<int64_t>(1) - bytes_per_frame;
  const TimelineFunction bytes_to_frames(0, offset, 1, bytes_per_frame);
  const TimelineFunction& t_bytes = driver_clock_mono_to_ring_pos_bytes();

  clock_mono_to_ring_buf_pos_frames_ =
      TimelineFunction::Compose(bytes_to_frames, t_bytes);
  clock_mono_to_ring_buf_pos_id_.Next();

  const TimelineFunction& trans = clock_mono_to_ring_buf_pos_frames_;
  uint32_t fd_frames = driver_->fifo_depth_frames();
  low_water_frames_ = fd_frames + trans.rate().Scale(kDefaultLowWaterNsec);
  frames_sent_ = low_water_frames_;
  frames_to_mix_ = 0;

  if (VERBOSE_TIMING_DEBUG) {
    FXL_LOG(INFO) << "Audio output: FIFO depth (" << fd_frames << " frames "
                  << std::fixed << std::setprecision(3)
                  << trans.rate().Inverse().Scale(fd_frames) / 1000000.0
                  << " mSec) Low Water (" << low_water_frames_ << " frames "
                  << std::fixed << std::setprecision(3)
                  << trans.rate().Inverse().Scale(low_water_frames_) / 1000000.0
                  << " mSec)";
  }

  state_ = State::Started;
  Process();
}

void DriverOutput::OnDriverPlugStateChange(bool plugged, zx_time_t plug_time) {
  // Reflect this message to the AudioDeviceManager so it can deal with the plug
  // state change.
  manager_->ScheduleMainThreadTask([manager = manager_,
                                    output = fbl::WrapRefPtr(this), plugged,
                                    plug_time]() {
    manager->HandlePlugStateChange(output, plugged, plug_time);
  });
}

}  // namespace audio
}  // namespace media
