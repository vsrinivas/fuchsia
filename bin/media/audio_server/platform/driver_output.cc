// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/audio_server/platform/driver_output.h"

#include <dispatcher-pool/dispatcher-channel.h>
#include <fbl/atomic.h>
#include <fbl/auto_call.h>
#include <fbl/limits.h>
#include <fcntl.h>
#include <fdio/io.h>
#include <zircon/process.h>
#include <iomanip>

#include "garnet/bin/media/audio_server/audio_output_manager.h"
#include "lib/fxl/logging.h"

static constexpr bool VERBOSE_TIMING_DEBUG = false;

namespace media {
namespace audio {

static constexpr uint32_t kDefaultFramesPerSec = 48000;
static constexpr uint16_t kDefaultChannelCount = 2;
static constexpr audio_sample_format_t kDefaultAudioFmt =
    AUDIO_SAMPLE_FORMAT_16BIT;
static constexpr AudioSampleFormat kDefaultMediaFrameworkFmt =
    AudioSampleFormat::SIGNED_16;
static constexpr uint32_t kDefaultFrameSize = 4;
static constexpr int64_t kDefaultLowWaterNsec = ZX_MSEC(20);
static constexpr int64_t kDefaultHighWaterNsec = ZX_MSEC(30);
static constexpr int64_t kDefaultBufferOverheadNsec = ZX_MSEC(1);
static constexpr zx_duration_t kUnderflowCooldown = ZX_SEC(1);
static constexpr zx_duration_t kDefaultCmdTimeout = ZX_SEC(3);

static fbl::atomic<zx_txid_t> TXID_GEN(1);
static thread_local zx_txid_t TXID = TXID_GEN.fetch_add(1);

fbl::RefPtr<AudioOutput> DriverOutput::Create(zx::channel stream_channel,
                                              AudioOutputManager* manager) {
  auto output =
      fbl::AdoptRef(new DriverOutput(manager, fbl::move(stream_channel)));

  if ((output->stream_channel_ == nullptr) ||
      (output->rb_channel_ == nullptr)) {
    return nullptr;
  }

  return fbl::move(output);
}

DriverOutput::DriverOutput(AudioOutputManager* manager,
                           zx::channel initial_stream_channel)
    : StandardOutputBase(manager),
      initial_stream_channel_(fbl::move(initial_stream_channel)) {
  stream_channel_ = ::audio::dispatcher::Channel::Create();
  rb_channel_ = ::audio::dispatcher::Channel::Create();
  cmd_timeout_ = ::audio::dispatcher::Timer::Create();
}

DriverOutput::~DriverOutput() {}

MediaResult DriverOutput::Init() {
  FXL_DCHECK(state_ == State::Uninitialized);

  if ((stream_channel_ == nullptr) || (rb_channel_ == nullptr) ||
      (cmd_timeout_ == nullptr)) {
    return MediaResult::INSUFFICIENT_RESOURCES;
  }

  MediaResult init_res = StandardOutputBase::Init();
  if (init_res != MediaResult::OK) {
    return init_res;
  }

  // Activate the stream channel.
  // clang-format off
  ::audio::dispatcher::Channel::ProcessHandler process_handler(
    [ output = fbl::WrapRefPtr(this) ]
    (::audio::dispatcher::Channel* channel) -> zx_status_t {
      OBTAIN_EXECUTION_DOMAIN_TOKEN(token, output->mix_domain_);
      FXL_DCHECK(output->stream_channel_.get() == channel);
      return output->ProcessStreamChannelMessage();
    });

  ::audio::dispatcher::Channel::ChannelClosedHandler channel_closed_handler(
    [ output = fbl::WrapRefPtr(this) ]
    (const ::audio::dispatcher::Channel* channel) {
      OBTAIN_EXECUTION_DOMAIN_TOKEN(token, output->mix_domain_);
      FXL_DCHECK(output->stream_channel_.get() == channel);
      output->ProcessChannelClosed();
    });
  // clang-format on

  zx_status_t res;
  res = stream_channel_->Activate(fbl::move(initial_stream_channel_),
                                  mix_domain_, fbl::move(process_handler),
                                  fbl::move(channel_closed_handler));
  if (res != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to activate stream channel for DriverOutput!  "
                   << "(res " << res << ")";
    return MediaResult::INTERNAL_ERROR;
  }

  // Activate the command timeout timer.
  // clang-format off
  ::audio::dispatcher::Timer::ProcessHandler cmd_timeout_handler(
    [ output = fbl::WrapRefPtr(this) ]
    (::audio::dispatcher::Timer* timer) -> zx_status_t {
      OBTAIN_EXECUTION_DOMAIN_TOKEN(token, output->mix_domain_);
      FXL_DCHECK(output->cmd_timeout_.get() == timer);
      return output->OnCommandTimeout();
    });
  // clang-format on

  res = cmd_timeout_->Activate(mix_domain_, fbl::move(cmd_timeout_handler));
  if (res != ZX_OK) {
    FXL_LOG(ERROR)
        << "Failed to activate command timeout timer for DriverOutput!  "
        << "(res " << res << ")";
    return MediaResult::INTERNAL_ERROR;
  }

  // Wait until the output manager pokes our wakeup event.
  state_ = State::WaitingToSetup;
  return MediaResult::OK;
}

void DriverOutput::OnWakeup() {
  // If we are not waiting to get started, there is nothing to do here.
  if (state_ != State::WaitingToSetup)
    return;

  // Select our output formatter
  frames_per_sec_ = kDefaultFramesPerSec;
  channel_count_ = kDefaultChannelCount;
  sample_format_ = kDefaultAudioFmt;
  bytes_per_frame_ = kDefaultFrameSize;

  AudioMediaTypeDetailsPtr config(AudioMediaTypeDetails::New());
  config->frames_per_second = frames_per_sec_;
  config->channels = channel_count_;
  config->sample_format = kDefaultMediaFrameworkFmt;

  output_formatter_ = OutputFormatter::Select(config);
  if (!output_formatter_) {
    FXL_LOG(ERROR) << "Failed to find output formatter for format "
                   << frames_per_sec_ << "Hz " << channel_count_ << "-Ch 0x"
                   << std::hex << config->sample_format;
    state_ = State::FatalError;
    ShutdownSelf();
    return;
  }

  // Kick off the process of initialization by sending the message to configure
  // our stream format.
  audio_stream_cmd_set_format_req_t req;

  req.hdr.cmd = AUDIO_STREAM_CMD_SET_FORMAT;
  req.hdr.transaction_id = TXID;
  req.frames_per_second = frames_per_sec_;
  req.channels = channel_count_;
  req.sample_format = sample_format_;

  zx_status_t res = stream_channel_->Write(&req, sizeof(req));
  if (res != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to send set format msg: " << frames_per_sec_
                   << "Hz " << channel_count_ << "-Ch 0x" << std::hex
                   << sample_format_ << "(res " << std::dec << res << ")";
    state_ = State::FatalError;
    ShutdownSelf();
    return;
  }

  state_ = State::WaitingForSetFormatResponse;
  cmd_timeout_->Arm(zx_deadline_after(kDefaultCmdTimeout));
}

void DriverOutput::Cleanup() {
  if (rb_virt_ != nullptr) {
    zx_vmar_unmap(rb_vmo_.get(), reinterpret_cast<uintptr_t>(rb_virt_),
                  rb_size_);
    rb_virt_ = nullptr;
  }
  rb_vmo_.reset();
  rb_size_ = 0;

  rb_channel_.reset();
  stream_channel_.reset();
}

bool DriverOutput::StartMixJob(MixJob* job, fxl::TimePoint process_start) {
  int64_t now;

  if (state_ == State::Starting) {
    // Convert the start time from the zx_get_ticks timeline to the
    // zx_get_time(ZX_CLOCK_MONOTONIC) timeline.
    //
    // TODO(johngro): This conversion makes a bunch of assumptions.  It would be
    // better to just convert the mixer to work in ticks instead of
    // CLOCK_MONOTONIC.  Eventually, we need to work clock recovery into this
    // mix, so this may all become a moot point.
    uint64_t ticks_per_sec = zx_ticks_per_second();
    FXL_DCHECK(ticks_per_sec <= fbl::numeric_limits<uint32_t>::max());
    int64_t local_start =
        TimelineRate::Scale(start_ticks_, 1000000000u, ticks_per_sec);

    local_to_frames_ = TimelineRate(kDefaultFramesPerSec, 1000000000u);
    local_to_output_ = TimelineFunction(local_start, 0, local_to_frames_);
    fifo_frames_ =
        ((rb_fifo_depth_ + kDefaultFrameSize - 1) / kDefaultFrameSize);
    low_water_frames_ =
        fifo_frames_ + local_to_frames_.Scale(kDefaultLowWaterNsec);
    frames_sent_ = low_water_frames_;

    if (VERBOSE_TIMING_DEBUG) {
      FXL_LOG(INFO) << "Audio output: FIFO depth (" << fifo_frames_
                    << " frames " << std::fixed << std::setprecision(3)
                    << local_to_frames_.Inverse().Scale(fifo_frames_) /
                           1000000.0
                    << " mSec) Low Water (" << low_water_frames_ << " frames "
                    << std::fixed << std::setprecision(3)
                    << local_to_frames_.Inverse().Scale(low_water_frames_) /
                           1000000.0
                    << " mSec)";
    }

    state_ = State::Started;
    frames_to_mix_ = 0;
    now = local_start;
  } else {
    now = process_start.ToEpochDelta().ToNanoseconds();
  }

  if (state_ != State::Started) {
    FXL_LOG(ERROR) << "Bad state during StartMixJob "
                   << static_cast<uint32_t>(state_);
    state_ = State::FatalError;
    ShutdownSelf();
    return false;
  }

  // If frames_to_mix_ is 0, then this is the start of a new cycle.  Check to
  // make sure we have not underflowed while we were sleeping, then compute how
  // many frames we need to mix during this wakeup cycle, and return a job
  // containing the largest contiguous buffer we can mix during this phase of
  // this cycle.
  if (!frames_to_mix_) {
    int64_t rd_ptr_frames = local_to_output_.Apply(now);
    int64_t fifo_threshold = rd_ptr_frames + fifo_frames_;

    if (fifo_threshold >= frames_sent_) {
      if (!underflow_start_time_) {
        // If this was the first time we missed our limit, log a message, mark
        // the start time of the underflow event, and fill our entire ring
        // buffer with silence.
        int64_t rd_limit_miss = rd_ptr_frames - frames_sent_;
        int64_t fifo_limit_miss = rd_limit_miss + fifo_frames_;
        int64_t low_water_limit_miss = rd_limit_miss + low_water_frames_;

        FXL_LOG(ERROR)
            << "UNDERFLOW: Missed mix target by (Rd, Fifo, LowWater) = ("
            << std::fixed << std::setprecision(3)
            << local_to_frames_.Inverse().Scale(rd_limit_miss) / 1000000.0
            << ", "
            << local_to_frames_.Inverse().Scale(fifo_limit_miss) / 1000000.0
            << ", "
            << local_to_frames_.Inverse().Scale(low_water_limit_miss) /
                   1000000.0
            << ") mSec.  Cooling down for at least "
            << kUnderflowCooldown / 1000000.0 << " mSec.";

        underflow_start_time_ = now;
        output_formatter_->FillWithSilence(rb_virt_, rb_frames_);
      }

      // Regardless of whether this was the first or a subsequent underflow,
      // update the cooldown deadline (the time at which we will start producing
      // frames again, provided we don't underflow again)
      underflow_cooldown_deadline_ = zx_deadline_after(kUnderflowCooldown);
    }

    int64_t fill_target = local_to_output_.Apply(now + kDefaultHighWaterNsec);

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
    FXL_DCHECK((frames_in_flight >= 0) && (frames_in_flight <= rb_frames_));
    FXL_DCHECK(frames_sent_ < fill_target);

    uint32_t rb_space = rb_frames_ - static_cast<uint32_t>(frames_in_flight);
    int64_t desired_frames = fill_target - frames_sent_;
    FXL_DCHECK(desired_frames >= 0);

    if (desired_frames > rb_frames_) {
      FXL_LOG(ERROR) << "Fatal underflow: want to produce " << desired_frames
                     << " but the ring buffer is only " << rb_frames_
                     << " frames long.";
      return false;
    }

    frames_to_mix_ =
        static_cast<uint32_t>(fbl::min<int64_t>(rb_space, desired_frames));
  }

  uint32_t to_mix = frames_to_mix_;
  uint32_t wr_ptr = frames_sent_ % rb_frames_;
  uint32_t contig_space = rb_frames_ - wr_ptr;

  if (to_mix > contig_space) {
    to_mix = contig_space;
  }

  job->buf =
      reinterpret_cast<uint8_t*>(rb_virt_) + (kDefaultFrameSize * wr_ptr);
  job->buf_frames = to_mix;
  job->start_pts_of = frames_sent_;
  job->local_to_output = &local_to_output_;
  job->local_to_output_gen = 1;

  return true;
}

bool DriverOutput::FinishMixJob(const MixJob& job) {
  // TODO(johngro): Flush cache here!

  if (VERBOSE_TIMING_DEBUG) {
    int64_t now = fxl::TimePoint::Now().ToEpochDelta().ToNanoseconds();
    int64_t rd_ptr_frames = local_to_output_.Apply(now);
    int64_t playback_lead_start = frames_sent_ - rd_ptr_frames;
    int64_t playback_lead_end = playback_lead_start + job.buf_frames;
    int64_t dma_lead_start = playback_lead_start - fifo_frames_;
    int64_t dma_lead_end = playback_lead_end - fifo_frames_;

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

zx_status_t DriverOutput::ReadMessage(
    const fbl::RefPtr<::audio::dispatcher::Channel>& channel,
    void* buf,
    uint32_t buf_size,
    uint32_t* bytes_read_out,
    zx::handle* handle_out) {
  FXL_DCHECK(buf != nullptr);
  FXL_DCHECK(bytes_read_out != nullptr);
  FXL_DCHECK(handle_out != nullptr);
  FXL_DCHECK(buf_size >= sizeof(audio_cmd_hdr_t));

  if ((state_ == State::Uninitialized) || (state_ == State::FatalError)) {
    return ZX_ERR_BAD_STATE;
  }

  zx_status_t res;
  res = channel->Read(buf, buf_size, bytes_read_out, handle_out);
  if (res != ZX_OK) {
    FXL_LOG(ERROR) << "Error attempting to read channel response "
                   << "(res = " << res << ").";
    return res;
  }

  if (*bytes_read_out < sizeof(audio_cmd_hdr_t)) {
    FXL_LOG(ERROR) << "Channel response is too small to hold even a "
                   << "message header (" << *bytes_read_out << " < "
                   << sizeof(audio_cmd_hdr_t) << ").";
    return ZX_ERR_INVALID_ARGS;
  }

  return ZX_OK;
}

#define CHECK_RESP(_ioctl, _payload, _expect_handle, _is_notif)           \
  do {                                                                    \
    if (_expect_handle != rxed_handle.is_valid()) {                       \
      FXL_LOG(ERROR) << (_expect_handle ? "Missing" : "Unexpected")       \
                     << " handle in " #_ioctl " response";                \
      return ZX_ERR_INVALID_ARGS;                                         \
    }                                                                     \
    if ((msg.hdr.transaction_id == AUDIO_INVALID_TRANSACTION_ID) !=       \
        _is_notif) {                                                      \
      FXL_LOG(ERROR) << "Bad txn id " << msg.hdr.transaction_id           \
                     << " in " #_ioctl " response";                       \
      return ZX_ERR_INVALID_ARGS;                                         \
    }                                                                     \
    if (bytes_read != sizeof(msg._payload)) {                             \
      FXL_LOG(ERROR) << "Bad " #_ioctl " response length (" << bytes_read \
                     << " != " << sizeof(msg._payload) << ")";            \
      return ZX_ERR_INVALID_ARGS;                                         \
    }                                                                     \
  } while (0)

zx_status_t DriverOutput::ProcessStreamChannelMessage() {
  zx_status_t res;
  zx::handle rxed_handle;
  uint32_t bytes_read;
  union {
    audio_cmd_hdr_t hdr;
    audio_stream_cmd_set_format_resp_t set_format;
    audio_stream_cmd_plug_detect_resp_t pd_resp;
    audio_stream_plug_detect_notify_t pd_notify;
  } msg;

  res = ReadMessage(stream_channel_, &msg, sizeof(msg), &bytes_read,
                    &rxed_handle);
  if (res != ZX_OK) {
    return res;
  }

  bool plug_state;
  switch (msg.hdr.cmd) {
    case AUDIO_STREAM_CMD_SET_FORMAT:
      CHECK_RESP(AUDIO_STREAM_CMD_SET_FORMAT, set_format, true, false);
      res = ProcessSetFormatResponse(msg.set_format,
                                     zx::channel(rxed_handle.release()));
      break;

    case AUDIO_STREAM_CMD_PLUG_DETECT:
      CHECK_RESP(AUDIO_STREAM_CMD_PLUG_DETECT, pd_resp, false, false);

      if ((msg.pd_resp.flags & AUDIO_PDNF_HARDWIRED) != 0) {
        plug_state = true;
      } else {
        plug_state = ((msg.pd_resp.flags & AUDIO_PDNF_PLUGGED) != 0);
        if ((msg.pd_resp.flags & AUDIO_PDNF_CAN_NOTIFY) == 0) {
          // TODO(johngro) : If we ever encounter hardware which must be polled
          // in order for plug detection to function properly, we should set up
          // a timer to periodically poll the plug state instead of just
          // assuming that the output is always plugged in.
          FXL_LOG(WARNING)
              << "Stream is incapable of async plug detection notifications.  "
                 "Assuming that the stream is always plugged in for now.";
          plug_state = true;
        }
      }

      res = ProcessPlugStateChange(plug_state, msg.pd_resp.plug_state_time);
      break;

    case AUDIO_STREAM_PLUG_DETECT_NOTIFY:
      CHECK_RESP(AUDIO_STREAM_CMD_PLUG_DETECT, pd_resp, false, true);
      plug_state = ((msg.pd_resp.flags & AUDIO_PDNF_PLUGGED) != 0);
      res = ProcessPlugStateChange(plug_state, msg.pd_resp.plug_state_time);
      break;

    default:
      FXL_LOG(ERROR) << "Unrecognized stream channel response 0x" << std::hex
                     << msg.hdr.cmd;
      return ZX_ERR_BAD_STATE;
  }

  return res;
}

zx_status_t DriverOutput::ProcessRingBufferChannelMessage() {
  zx_status_t res;
  zx::handle rxed_handle;
  uint32_t bytes_read;
  union {
    audio_cmd_hdr_t hdr;
    audio_rb_cmd_get_fifo_depth_resp_t get_fifo_depth;
    audio_rb_cmd_get_buffer_resp_t get_buffer;
    audio_rb_cmd_start_resp_t start;
  } msg;

  res = ReadMessage(rb_channel_, &msg, sizeof(msg), &bytes_read, &rxed_handle);

  switch (msg.hdr.cmd) {
    case AUDIO_RB_CMD_GET_FIFO_DEPTH:
      CHECK_RESP(AUDIO_RB_CMD_GET_FIFO_DEPTH, get_fifo_depth, false, false);
      res = ProcessGetFifoDepthResponse(msg.get_fifo_depth);
      break;

    case AUDIO_RB_CMD_GET_BUFFER:
      CHECK_RESP(AUDIO_RB_CMD_GET_BUFFER, get_buffer, true, false);
      res = ProcessGetBufferResponse(msg.get_buffer,
                                     zx::vmo(rxed_handle.release()));
      break;

    case AUDIO_RB_CMD_START:
      CHECK_RESP(AUDIO_RB_CMD_START, start, false, false);
      res = ProcessStartResponse(msg.start);
      break;

    default:
      FXL_LOG(ERROR) << "Unrecognized ring buffer channel response 0x"
                     << std::hex << msg.hdr.cmd;
      return ZX_ERR_BAD_STATE;
  }

  return ZX_OK;
}
#undef CHECK_RESP

void DriverOutput::ProcessChannelClosed() {
  ShutdownSelf();
}

zx_status_t DriverOutput::ProcessSetFormatResponse(
    const audio_stream_cmd_set_format_resp_t& resp,
    zx::channel rb_channel) {
  if (state_ != State::WaitingForSetFormatResponse) {
    FXL_LOG(ERROR) << "Received unexpected set format response while in state "
                   << static_cast<uint32_t>(state_);
    return ZX_ERR_BAD_STATE;
  }

  zx_status_t res = resp.result;
  auto cleanup = fbl::MakeAutoCall([&]() {
    FXL_LOG(ERROR) << "Error attempting to set format: " << frames_per_sec_
                   << "Hz " << channel_count_ << "-Ch 0x" << std::hex
                   << sample_format_ << "(res " << std::dec << res << ")";
  });

  if (res != ZX_OK) {
    FXL_LOG(ERROR) << "Driver rejected set format request";
    return res;
  }

  // Activate the ring buffer channel.
  // clang-format off
  ::audio::dispatcher::Channel::ProcessHandler process_handler(
    [ output = fbl::WrapRefPtr(this) ]
    (::audio::dispatcher::Channel * channel) -> zx_status_t {
      OBTAIN_EXECUTION_DOMAIN_TOKEN(token, output->mix_domain_);
      FXL_DCHECK(output->rb_channel_.get() == channel);
      return output->ProcessRingBufferChannelMessage();
    });

  ::audio::dispatcher::Channel::ChannelClosedHandler channel_closed_handler(
    [ output = fbl::WrapRefPtr(this) ]
    (const ::audio::dispatcher::Channel* channel) {
      OBTAIN_EXECUTION_DOMAIN_TOKEN(token, output->mix_domain_);
      FXL_DCHECK(output->rb_channel_.get() == channel);
      output->ProcessChannelClosed();
    });
  // clang-format on

  res = rb_channel_->Activate(fbl::move(rb_channel), mix_domain_,
                              fbl::move(process_handler),
                              fbl::move(channel_closed_handler));
  if (res != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to activate ring buffer channel";
    return res;
  }

  // Send a request to query the initial plug detection state and enable plug
  // detect notifications.
  {
    audio_stream_cmd_plug_detect_req_t req;

    req.hdr.cmd = AUDIO_STREAM_CMD_PLUG_DETECT;
    req.hdr.transaction_id = TXID;
    req.flags = AUDIO_PDF_ENABLE_NOTIFICATIONS;

    res = stream_channel_->Write(&req, sizeof(req));
    if (res != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to request initial plug state.";
      return res;
    }
  }

  // Fetch the fifo depth of the ring buffer we just got back.  This determines
  // how far ahead of the current playout position (in bytes) the hardware may
  // read.  We need to know this number in order to size the ring buffer vmo
  // appropriately
  {
    audio_rb_cmd_get_fifo_depth_req req;

    req.hdr.cmd = AUDIO_RB_CMD_GET_FIFO_DEPTH;
    req.hdr.transaction_id = TXID;

    res = rb_channel_->Write(&req, sizeof(req));
    if (res != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to request ring buffer fifo depth.";
      return res;
    }
  }

  // Things went well, proceed to the next step in the state machine.
  state_ = State::WaitingForRingBufferFifoDepth;
  cmd_timeout_->Arm(zx_deadline_after(kDefaultCmdTimeout));
  cleanup.cancel();
  return ZX_OK;
}

zx_status_t DriverOutput::ProcessPlugStateChange(bool plugged,
                                                 zx_time_t plug_time) {
  // If this was a hardwired output, just use the current time as the plug time.
  if (!plug_time) {
    plug_time = zx_time_get(ZX_CLOCK_MONOTONIC);
  }

  // Reflect this message to the AudioOutputManager so it can deal with the plug
  // state change.
  // clang-format off
  manager_->ScheduleMessageLoopTask(
    [ manager = manager_,
      output = fbl::WrapRefPtr(this),
      plugged,
      plug_time ]() {
      manager->HandlePlugStateChange(output, plugged, plug_time);
    });
  // clang-format on

  return ZX_OK;
}

zx_status_t DriverOutput::ProcessGetFifoDepthResponse(
    const audio_rb_cmd_get_fifo_depth_resp_t& resp) {
  if (state_ != State::WaitingForRingBufferFifoDepth) {
    FXL_LOG(ERROR) << "Received unexpected fifo depth response while in state "
                   << static_cast<uint32_t>(state_);
    return ZX_ERR_BAD_STATE;
  }

  if (resp.result != ZX_OK) {
    FXL_LOG(ERROR) << "Error when fetching ring buffer fifo depth (res = "
                   << resp.result << ").";
    return resp.result;
  }

  rb_fifo_depth_ = resp.fifo_depth;

  // Request a ring-buffer VMO from the ring buffer channel.  Demand that it be
  // large enough to cover the FIFO read-ahead in addition to the amount of
  // buffering we want in order to hit our high water mark.  Do not request any
  // any notifications from the channel when setting up the buffer, we will
  // manage buffering using just timing.
  uint64_t min_frames_64 = kDefaultHighWaterNsec + kDefaultBufferOverheadNsec;
  min_frames_64 *= (bytes_per_frame_ * frames_per_sec_);
  min_frames_64 /= ZX_SEC(1);
  min_frames_64 += rb_fifo_depth_ + bytes_per_frame_ - 1;
  min_frames_64 /= bytes_per_frame_;
  FXL_DCHECK(min_frames_64 < fbl::numeric_limits<uint32_t>::max());

  rb_size_ = min_frames_64 *= bytes_per_frame_;

  audio_rb_cmd_get_buffer_req_t req;
  req.hdr.cmd = AUDIO_RB_CMD_GET_BUFFER;
  req.hdr.transaction_id = TXID;
  req.min_ring_buffer_frames = static_cast<uint32_t>(min_frames_64);
  req.notifications_per_ring = 0;

  state_ = State::WaitingForRingBufferVmo;
  cmd_timeout_->Arm(zx_deadline_after(kDefaultCmdTimeout));
  return rb_channel_->Write(&req, sizeof(req));
}

zx_status_t DriverOutput::ProcessGetBufferResponse(
    const audio_rb_cmd_get_buffer_resp_t& resp,
    zx::vmo rb_vmo) {
  if (state_ != State::WaitingForRingBufferVmo) {
    FXL_LOG(ERROR) << "Received unexpected get buffer response while in state "
                   << static_cast<uint32_t>(state_);
    return ZX_ERR_BAD_STATE;
  }

  if (resp.result != ZX_OK) {
    FXL_LOG(ERROR) << "Error when fetching ring buffer vmo (res = "
                   << resp.result << ").";
    return resp.result;
  }

  // Fetch and sanity check the size of the VMO we got back from the ring buffer
  // channel.
  uint64_t tmp;
  rb_vmo_ = fbl::move(rb_vmo);
  zx_status_t res = rb_vmo_.get_size(&tmp);
  if (res != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to get ring buffer VMO size (res " << res << ")";
    return ZX_ERR_INTERNAL;
  }

  if (tmp < rb_size_) {
    FXL_LOG(ERROR) << "Ring buffer size is smaller than we asked for (" << tmp
                   << " < " << rb_size_ << ")";
    return ZX_ERR_INTERNAL;
  }
  rb_size_ = tmp;

  if (rb_size_ % kDefaultFrameSize) {
    FXL_LOG(ERROR) << "Ring buffer size (" << rb_size_
                   << ") is not a multiple of the frame size ("
                   << kDefaultFrameSize << ")";
    return ZX_ERR_INTERNAL;
  }

  rb_frames_ = rb_size_ / bytes_per_frame_;

  // Map the VMO into our address space and fill it with silence.
  // TODO(johngro) : How do I specify the cache policy for this mapping?
  res = zx_vmar_map(zx_vmar_root_self(), 0u, rb_vmo_.get(), 0u, rb_size_,
                    ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE,
                    reinterpret_cast<uintptr_t*>(&rb_virt_));
  if (res != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to map ring buffer VMO (res " << res << ")";
    return ZX_ERR_INTERNAL;
  }

  // TODO(johngro): Flush the cache (if needed) here.
  output_formatter_->FillWithSilence(rb_virt_, rb_frames_);

  // Set up the intermediate buffer at the StandardOutputBase level
  //
  // TODO(johngro): The intermediate buffer probably does not need to be as
  // large as the entire ring buffer.  Consider limiting this to be something
  // only slightly larger than a nominal mix job.
  SetupMixBuffer(rb_frames_);

  // Send the command to start the ring buffer.
  // TODO(johngro): Wait to do this until we know that we have clients.
  audio_rb_cmd_start_req_t req;
  req.hdr.cmd = AUDIO_RB_CMD_START;
  req.hdr.transaction_id = TXID;
  state_ = State::Starting;

  cmd_timeout_->Arm(zx_deadline_after(kDefaultCmdTimeout));
  return rb_channel_->Write(&req, sizeof(req));
}

zx_status_t DriverOutput::ProcessStartResponse(
    const audio_rb_cmd_start_resp_t& resp) {
  if (state_ != State::Starting) {
    FXL_LOG(ERROR) << "Received unexpected start response while in state "
                   << static_cast<uint32_t>(state_);
    return ZX_ERR_BAD_STATE;
  }

  if (resp.result != ZX_OK) {
    FXL_LOG(ERROR) << "Error when starting ring buffer (res = " << resp.result
                   << ").";
    return resp.result;
  }

  start_ticks_ = resp.start_ticks;
  cmd_timeout_->Cancel();
  Process();

  return ZX_OK;
}

zx_status_t DriverOutput::OnCommandTimeout() {
  FXL_LOG(ERROR) << "Command timeout while in state "
                 << static_cast<uint32_t>(state_);
  ShutdownSelf();
  return ZX_OK;
}

void DriverOutput::ScheduleNextLowWaterWakeup() {
  // Schedule the next callback for when we are at the low water mark behind
  // the write pointer.
  int64_t low_water_frames = frames_sent_ - low_water_frames_;
  int64_t low_water_time = local_to_output_.ApplyInverse(low_water_frames);
  SetNextSchedTime(fxl::TimePoint::FromEpochDelta(
      fxl::TimeDelta::FromNanoseconds(low_water_time)));
}

}  // namespace audio
}  // namespace media
