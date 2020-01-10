// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/driver_output.h"

#include <lib/async/cpp/time.h>
#include <lib/fit/defer.h>
#include <lib/zx/clock.h>

#include <algorithm>
#include <iomanip>

#include <trace/event.h>

#include "src/media/audio/audio_core/reporter.h"

constexpr bool VERBOSE_TIMING_DEBUG = false;

namespace media::audio {

static constexpr uint32_t kDefaultFramesPerSec = 48000;
static constexpr uint32_t kDefaultChannelCount = 2;
static constexpr fuchsia::media::AudioSampleFormat kDefaultAudioFmt =
    fuchsia::media::AudioSampleFormat::SIGNED_24_IN_32;
static constexpr zx::duration kDefaultMaxRetentionNsec = zx::msec(60);
static constexpr zx::duration kDefaultRetentionGapNsec = zx::msec(10);
static constexpr zx::duration kUnderflowCooldown = zx::msec(1000);

static std::atomic<zx_txid_t> TXID_GEN(1);
static thread_local zx_txid_t TXID = TXID_GEN.fetch_add(1);

// Consts used if kEnableFinalMixWavWriter is set:
//
// This atomic is only used when the final-mix wave-writer is enabled --
// specifically to generate unique ids for each final-mix WAV file.
std::atomic<uint32_t> DriverOutput::final_mix_instance_num_(0u);
// WAV file location: FilePathName+final_mix_instance_num_+FileExtension
constexpr const char* kDefaultWavFilePathName = "/tmp/final_mix_";
constexpr const char* kWavFileExtension = ".wav";

fbl::RefPtr<AudioOutput> DriverOutput::Create(zx::channel stream_channel,
                                              ThreadingModel* threading_model,
                                              DeviceRegistry* registry) {
  return fbl::AdoptRef(new DriverOutput(threading_model, registry, std::move(stream_channel)));
}

DriverOutput::DriverOutput(ThreadingModel* threading_model, DeviceRegistry* registry,
                           zx::channel initial_stream_channel)
    : AudioOutput(threading_model, registry),
      initial_stream_channel_(std::move(initial_stream_channel)) {}

DriverOutput::~DriverOutput() { wav_writer_.Close(); }

zx_status_t DriverOutput::Init() {
  TRACE_DURATION("audio", "DriverOutput::Init");
  FX_DCHECK(state_ == State::Uninitialized);

  zx_status_t res = AudioOutput::Init();
  if (res != ZX_OK) {
    return res;
  }

  res = driver()->Init(std::move(initial_stream_channel_));
  if (res != ZX_OK) {
    FX_PLOGS(ERROR, res) << "Failed to initialize driver object";
    return res;
  }

  state_ = State::FormatsUnknown;
  return res;
}

void DriverOutput::OnWakeup() {
  TRACE_DURATION("audio", "DriverOutput::OnWakeup");
  // If we are not in the FormatsUnknown state, then we have already started the
  // state machine.  There is (currently) nothing else to do here.
  FX_DCHECK(state_ != State::Uninitialized);
  if (state_ != State::FormatsUnknown) {
    return;
  }

  // Kick off the process of driver configuration by requesting the basic driver
  // info, which will include the modes which the driver supports.
  driver()->GetDriverInfo();
  state_ = State::FetchingFormats;
}

std::optional<MixStage::FrameSpan> DriverOutput::StartMixJob(zx::time uptime) {
  TRACE_DURATION("audio", "DriverOutput::StartMixJob");
  if (state_ != State::Started) {
    FX_LOGS(ERROR) << "Bad state during StartMixJob " << static_cast<uint32_t>(state_);
    state_ = State::Shutdown;
    ShutdownSelf();
    return std::nullopt;
  }

  // TODO(mpuryear): Depending on policy, use send appropriate commands to the
  // driver to control gain as well.  Some policy settings which might be useful
  // include...
  //
  // ++ Never use HW gain, even if it supports it.
  // ++ Always use HW gain when present, regardless of its limitations.
  // ++ Use HW gain when present, but only if it reaches a minimum bar of
  //    functionality.
  // ++ Implement a hybrid of HW/SW gain.  IOW - Get as close as possible to our
  //    target using HW, and then get the rest of the way there using SW
  //    scaling.  This approach may end up being unreasonably tricky as we may
  //    not be able to synchronize the HW and SW changes in gain well enough to
  //    avoid strange situations where the jumps in one direction (because of
  //    the SW component), and then in the other (as the HW gain command takes
  //    effect).
  //
  bool output_muted = true;
  const auto& settings = device_settings();
  if (settings != nullptr) {
    AudioDeviceSettings::GainState cur_gain_state;
    settings->SnapshotGainState(&cur_gain_state);
    output_muted = cur_gain_state.muted;
  }

  FX_DCHECK(driver_ring_buffer() != nullptr);
  const auto& clock_monotonic_to_output_frame = clock_monotonic_to_output_frame_;
  const auto& output_frames_per_monotonic_tick = clock_monotonic_to_output_frame.rate();
  const auto& rb = *driver_ring_buffer();
  uint32_t fifo_frames = driver()->fifo_depth_frames();

  // output_frames_consumed is the number of frames that the audio output device has read so far.
  // output_frames_emitted is the slightly-smaller number of frames that have physically exited
  // the device itself (the number of frames that have "made sound" so far);
  int64_t output_frames_consumed = clock_monotonic_to_output_frame.Apply(uptime.get());
  int64_t output_frames_emitted = output_frames_consumed - fifo_frames;

  if (output_frames_consumed >= frames_sent_) {
    if (!underflow_start_time_.get()) {
      // If this was the first time we missed our limit, log a message, mark the start time of the
      // underflow event, and fill our entire ring buffer with silence.
      int64_t output_underflow_frames = output_frames_consumed - frames_sent_;
      int64_t low_water_frames_underflow = output_underflow_frames + low_water_frames_;

      zx::duration output_underflow_duration =
          zx::nsec(output_frames_per_monotonic_tick.Inverse().Scale(output_underflow_frames));
      FX_CHECK(output_underflow_duration.get() >= 0);

      zx::duration output_variance_from_expected_wakeup =
          zx::nsec(output_frames_per_monotonic_tick.Inverse().Scale(low_water_frames_underflow));

      FX_LOGS(ERROR) << "OUTPUT UNDERFLOW: Missed mix target by (worst-case, expected) = ("
                     << std::setprecision(4)
                     << static_cast<double>(output_underflow_duration.to_nsecs()) / ZX_MSEC(1)
                     << ", " << output_variance_from_expected_wakeup.to_msecs()
                     << ") ms. Cooling down for " << kUnderflowCooldown.to_msecs()
                     << " milliseconds.";

      // Use our Reporter to log this to Cobalt, if enabled.
      REP(OutputUnderflow(output_underflow_duration, uptime));

      underflow_start_time_ = uptime;
      output_producer_->FillWithSilence(rb.virt(), rb.frames());
      zx_cache_flush(rb.virt(), rb.size(), ZX_CACHE_FLUSH_DATA);

      wav_writer_.Close();
    }

    // Regardless of whether this was the first or a subsequent underflow,
    // update the cooldown deadline (the time at which we will start producing
    // frames again, provided we don't underflow again)
    underflow_cooldown_deadline_ = zx::deadline_after(kUnderflowCooldown);
  }

  int64_t fill_target =
      clock_monotonic_to_output_frame.Apply((uptime + kDefaultHighWaterNsec).get()) +
      driver()->fifo_depth_frames();

  // Are we in the middle of an underflow cooldown? If so, check whether we have recovered yet.
  if (underflow_start_time_.get()) {
    if (uptime < underflow_cooldown_deadline_) {
      // Looks like we have not recovered yet.  Pretend to have produced the
      // frames we were going to produce and schedule the next wakeup time.
      frames_sent_ = fill_target;
      ScheduleNextLowWaterWakeup();
      return std::nullopt;
    } else {
      // Looks like we recovered.  Log and go back to mixing.
      FX_LOGS(WARNING) << "OUTPUT UNDERFLOW: Recovered after "
                       << (uptime - underflow_start_time_).to_msecs() << " ms.";
      underflow_start_time_ = zx::time(0);
      underflow_cooldown_deadline_ = zx::time(0);
    }
  }

  int64_t frames_in_flight = frames_sent_ - output_frames_emitted;
  FX_DCHECK((frames_in_flight >= 0) && (frames_in_flight <= rb.frames()));
  FX_DCHECK(frames_sent_ <= fill_target);
  int64_t desired_frames = fill_target - frames_sent_;

  // If we woke up too early to have any work to do, just get out now.
  if (desired_frames == 0) {
    return std::nullopt;
  }

  uint32_t rb_space = rb.frames() - static_cast<uint32_t>(frames_in_flight);
  if (desired_frames > rb.frames()) {
    FX_LOGS(ERROR) << "OUTPUT UNDERFLOW: want to produce " << desired_frames
                   << " but the ring buffer is only " << rb.frames() << " frames long.";
    return std::nullopt;
  }

  uint32_t frames_to_mix = static_cast<uint32_t>(std::min<int64_t>(rb_space, desired_frames));

  auto frames = MixStage::FrameSpan{
      .start = frames_sent_,
      .length = frames_to_mix,
  };
  // If we're muted we simply fill the ring buffer with silence.
  if (output_muted) {
    FillRingWithSilence(frames);
    ScheduleNextLowWaterWakeup();
    return std::nullopt;
  }
  return {frames};
}

void DriverOutput::WriteToRing(
    const MixStage::FrameSpan& span,
    fit::function<void(uint64_t offset, uint32_t length, void* dest_buf)> writer) {
  TRACE_DURATION("audio", "DriverOutput::FinishMixJob");
  const auto& rb = driver_ring_buffer();
  FX_DCHECK(rb != nullptr);

  size_t frames_left = span.length;
  size_t offset = 0;
  while (frames_left > 0) {
    uint32_t wr_ptr = (span.start + offset) % rb->frames();
    uint32_t contig_space = rb->frames() - wr_ptr;
    uint32_t to_send = frames_left;
    if (to_send > contig_space) {
      to_send = contig_space;
    }
    void* dest_buf = rb->virt() + (rb->frame_size() * wr_ptr);

    writer(offset, to_send, dest_buf);

    frames_left -= to_send;
    offset += to_send;
  }
  frames_sent_ += offset;
}

void DriverOutput::FinishMixJob(const MixStage::FrameSpan& span, float* buffer) {
  TRACE_DURATION("audio", "DriverOutput::FinishMixJob");
  WriteToRing(span, [this, buffer](uint64_t offset, uint32_t frames, void* dest_buf) {
    auto job_buf_offset = offset * output_producer_->channels();
    output_producer_->ProduceOutput(buffer + job_buf_offset, dest_buf, frames);

    size_t dest_buf_len = frames * output_producer_->bytes_per_frame();
    wav_writer_.Write(dest_buf, dest_buf_len);
    wav_writer_.UpdateHeader();
    zx_cache_flush(dest_buf, dest_buf_len, ZX_CACHE_FLUSH_DATA);
  });

  if (VERBOSE_TIMING_DEBUG) {
    auto now = async::Now(mix_domain().dispatcher());
    int64_t output_frames_consumed = clock_monotonic_to_output_frame_.Apply(now.get());
    int64_t playback_lead_start = frames_sent_ - output_frames_consumed;
    int64_t playback_lead_end = playback_lead_start + span.length;

    FX_LOGS(INFO) << "PLead [" << std::setw(4) << playback_lead_start << ", " << std::setw(4)
                  << playback_lead_end << "]";
  }

  ScheduleNextLowWaterWakeup();
}

void DriverOutput::FillRingWithSilence(const MixStage::FrameSpan& span) {
  WriteToRing(span, [this](auto offset, auto frames, auto dest_buf) {
    output_producer_->FillWithSilence(dest_buf, frames);
  });
}

void DriverOutput::ApplyGainLimits(fuchsia::media::AudioGainInfo* in_out_info, uint32_t set_flags) {
  TRACE_DURATION("audio", "DriverOutput::ApplyGainLimits");
  // See the comment at the start of StartMixJob.  The actual limits we set here
  // are going to eventually depend on what our HW gain control capabilities
  // are, and how we choose to apply them (based on policy)
  FX_DCHECK(in_out_info != nullptr);

  // We do not currently allow more than unity gain for audio outputs.
  if (in_out_info->gain_db > 0.0) {
    in_out_info->gain_db = 0;
  }

  // Audio outputs should never support AGC
  in_out_info->flags &= ~(fuchsia::media::AudioGainInfoFlag_AgcEnabled);
}

void DriverOutput::ScheduleNextLowWaterWakeup() {
  TRACE_DURATION("audio", "DriverOutput::ScheduleNextLowWaterWakeup");
  // Schedule next callback for the low water mark behind the write pointer.
  const auto& reference_clock_to_ring_buffer_frame = clock_monotonic_to_output_frame_;
  int64_t low_water_frames = frames_sent_ - low_water_frames_;
  int64_t low_water_time = reference_clock_to_ring_buffer_frame.ApplyInverse(low_water_frames);
  SetNextSchedTime(zx::time(low_water_time));
}

void DriverOutput::OnDriverInfoFetched() {
  TRACE_DURATION("audio", "DriverOutput::OnDriverInfoFetched");
  auto cleanup = fit::defer([this]() FXL_NO_THREAD_SAFETY_ANALYSIS {
    state_ = State::Shutdown;
    ShutdownSelf();
  });

  if (state_ != State::FetchingFormats) {
    FX_LOGS(ERROR) << "Unexpected GetFormatsComplete while in state "
                   << static_cast<uint32_t>(state_);
    return;
  }

  zx_status_t res;

  // TODO(mpuryear): Use the best driver-supported format, not hardwired default
  uint32_t pref_fps = kDefaultFramesPerSec;
  uint32_t pref_chan = kDefaultChannelCount;
  fuchsia::media::AudioSampleFormat pref_fmt = kDefaultAudioFmt;
  zx::duration min_rb_duration =
      kDefaultHighWaterNsec + kDefaultMaxRetentionNsec + kDefaultRetentionGapNsec;

  res = SelectBestFormat(driver()->format_ranges(), &pref_fps, &pref_chan, &pref_fmt);

  if (res != ZX_OK) {
    FX_LOGS(ERROR) << "Output: cannot match a driver format to this request: " << pref_fps
                   << " Hz, " << pref_chan << "-channel, sample format 0x" << std::hex
                   << static_cast<uint32_t>(pref_fmt);
    return;
  }

  // TODO(mpuryear): Save to the hub the configured format for this output.

  TimelineRate ns_to_frames(pref_fps, ZX_SEC(1));
  int64_t retention_frames = ns_to_frames.Scale(kDefaultMaxRetentionNsec.to_nsecs());
  FX_DCHECK(retention_frames != TimelineRate::kOverflow);
  FX_DCHECK(retention_frames <= std::numeric_limits<uint32_t>::max());
  driver()->SetEndFenceToStartFenceFrames(static_cast<uint32_t>(retention_frames));

  // Select our output producer
  Format format(fuchsia::media::AudioStreamType{
      .sample_format = pref_fmt,
      .channels = pref_chan,
      .frames_per_second = pref_fps,
  });
  output_producer_ = OutputProducer::Select(format.stream_type());
  if (!output_producer_) {
    FX_LOGS(ERROR) << "Output: OutputProducer cannot support this request: " << pref_fps << " Hz, "
                   << pref_chan << "-channel, sample format 0x" << std::hex
                   << static_cast<uint32_t>(pref_fmt);
    return;
  }

  // Start the process of configuring our driver
  res = driver()->Configure(format, min_rb_duration);
  if (res != ZX_OK) {
    FX_LOGS(ERROR) << "Output: failed to configure driver for: " << pref_fps << " Hz, " << pref_chan
                   << "-channel, sample format 0x" << std::hex << static_cast<uint32_t>(pref_fmt)
                   << " (res " << std::dec << res << ")";
    return;
  }

  if constexpr (kEnableFinalMixWavWriter) {
    std::string file_name_ = kDefaultWavFilePathName;
    uint32_t instance_count = final_mix_instance_num_.fetch_add(1);
    file_name_ += (std::to_string(instance_count) + kWavFileExtension);
    wav_writer_.Initialize(file_name_.c_str(), pref_fmt, pref_chan, pref_fps,
                           format.bytes_per_frame() * 8 / pref_chan);
  }

  // Tell AudioDeviceManager we are ready to be an active audio device.
  ActivateSelf();

  // Success; now wait until configuration completes.
  state_ = State::Configuring;
  cleanup.cancel();
}

void DriverOutput::OnDriverConfigComplete() {
  TRACE_DURATION("audio", "DriverOutput::OnDriverConfigComplete");
  auto cleanup = fit::defer([this]() FXL_NO_THREAD_SAFETY_ANALYSIS {
    state_ = State::Shutdown;
    ShutdownSelf();
  });

  if (state_ != State::Configuring) {
    FX_LOGS(ERROR) << "Unexpected ConfigComplete while in state " << static_cast<uint32_t>(state_);
    return;
  }

  // Driver is configured, we have all the needed info to compute minimum lead time for this output.
  SetMinLeadTime(driver()->external_delay() + driver()->fifo_depth_duration() +
                 kDefaultHighWaterNsec);

  // Fill our brand new ring buffer with silence
  FX_CHECK(driver_ring_buffer() != nullptr);
  const auto& rb = *driver_ring_buffer();
  FX_DCHECK(output_producer_ != nullptr);
  FX_DCHECK(rb.virt() != nullptr);
  output_producer_->FillWithSilence(rb.virt(), rb.frames());

  // Start the ring buffer running
  //
  // TODO(13292) : Don't actually start things up here. We should start only when we have clients
  // with work to do, and we should stop when we have no work to do.
  zx_status_t res = driver()->Start();
  if (res != ZX_OK) {
    FX_PLOGS(ERROR, res) << "Failed to start ring buffer";
    return;
  }

  // Start monitoring plug state.
  res = driver()->SetPlugDetectEnabled(true);
  if (res != ZX_OK) {
    FX_PLOGS(ERROR, res) << "Failed to enable plug detection";
    return;
  }

  // Success
  state_ = State::Starting;
  cleanup.cancel();
}

void DriverOutput::OnDriverStartComplete() {
  TRACE_DURATION("audio", "DriverOutput::OnDriverStartComplete");
  if (state_ != State::Starting) {
    FX_LOGS(ERROR) << "Unexpected StartComplete while in state " << static_cast<uint32_t>(state_);
    return;
  }

  // Compute the transformation from clock mono to the ring buffer read position
  // in frames, rounded up.  Then compute our low water mark (in frames) and
  // where we want to start mixing.  Finally kick off the mixing engine by
  // manually calling Process.
  auto format = driver()->GetFormat();
  FX_CHECK(format);

  uint32_t bytes_per_frame = format->bytes_per_frame();
  int64_t offset = static_cast<int64_t>(1) - bytes_per_frame;
  const TimelineFunction bytes_to_frames(0, offset, 1, bytes_per_frame);
  const TimelineFunction& t_bytes = device_reference_clock_to_ring_pos_bytes();

  clock_monotonic_to_output_frame_ = TimelineFunction::Compose(bytes_to_frames, t_bytes);
  clock_monotonic_to_output_frame_generation_.Next();

  // Set up the mix task in the AudioOutput.
  //
  // Configure our mix job output format. Note we want the same format (frame rate, channelization)
  // as our output, except output audio as FLOAT samples since that's the only output sample format
  // the Mixer supports. The conversion to the format required by the hardware ring buffer is done
  // after the mix is done (see FinishMixJob).
  //
  // TODO(39886): The intermediate buffer probably does not need to be as large as the entire ring
  // buffer.  Consider limiting this to be something only slightly larger than a nominal mix job.
  Format mix_format = Format(fuchsia::media::AudioStreamType{
      .sample_format = fuchsia::media::AudioSampleFormat::FLOAT,
      .channels = format->channels(),
      .frames_per_second = format->frames_per_second(),
  });
  SetupMixTask(mix_format, driver_ring_buffer()->frames(), clock_monotonic_to_output_frame_);

  const TimelineFunction& trans = clock_monotonic_to_output_frame_;
  uint32_t fd_frames = driver()->fifo_depth_frames();
  low_water_frames_ = fd_frames + trans.rate().Scale(kDefaultLowWaterNsec.get());
  frames_sent_ = low_water_frames_;

  if (VERBOSE_TIMING_DEBUG) {
    FX_LOGS(INFO) << "Audio output: FIFO depth (" << fd_frames << " frames " << std::fixed
                  << std::setprecision(3) << trans.rate().Inverse().Scale(fd_frames) / 1000000.0
                  << " mSec) Low Water (" << low_water_frames_ << " frames " << std::fixed
                  << std::setprecision(3)
                  << trans.rate().Inverse().Scale(low_water_frames_) / 1000000.0 << " mSec)";
  }

  state_ = State::Started;
  Process();
}

void DriverOutput::OnDriverPlugStateChange(bool plugged, zx::time plug_time) {
  TRACE_DURATION("audio", "DriverOutput::OnDriverPlugStateChange");
  // Reflect this message to the AudioDeviceManager so it can deal with the plug
  // state change.
  threading_model().FidlDomain().PostTask([output = fbl::RefPtr(this), plugged, plug_time]() {
    output->device_registry().OnPlugStateChanged(std::move(output), plugged, plug_time);
  });
}

}  // namespace media::audio
