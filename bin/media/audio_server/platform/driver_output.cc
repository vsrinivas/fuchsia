// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/audio_server/platform/driver_output.h"

#include <fbl/atomic.h>
#include <fbl/auto_call.h>
#include <fbl/limits.h>
#include <fcntl.h>
#include <zircon/device/audio.h>
#include <zircon/process.h>
#include <fdio/io.h>
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
static constexpr uint32_t kDefaultRingBufferMsec = 40;
static constexpr uint32_t kDefaultRingBufferFrames =
    ((kDefaultRingBufferMsec * kDefaultFramesPerSec) + 999) / 1000;
static constexpr uint32_t kDefaultRingBufferBytes =
    kDefaultRingBufferFrames * kDefaultFrameSize;
static constexpr int64_t kDefaultLowWaterNsec = 15000000;   // 15 msec for now
static constexpr int64_t kDefaultHighWaterNsec = 20000000;  // 20 msec for now
static constexpr zx_duration_t kUnderflowCooldown = ZX_SEC(1);

static fbl::atomic<zx_txid_t> TXID_GEN(1);
static thread_local zx_txid_t TXID = TXID_GEN.fetch_add(1);

AudioOutputPtr DriverOutput::Create(zx::channel channel,
                                    AudioOutputManager* manager) {
  return AudioOutputPtr(new DriverOutput(std::move(channel), manager));
}

DriverOutput::DriverOutput(zx::channel channel, AudioOutputManager* manager)
    : StandardOutputBase(manager), stream_channel_(std::move(channel)) {}

DriverOutput::~DriverOutput() {}

template <typename ReqType, typename RespType>
zx_status_t DriverOutput::SyncDriverCall(const zx::channel& channel,
                                         const ReqType& req, RespType* resp,
                                         zx_handle_t* resp_handle_out) {
  constexpr zx_time_t CALL_TIMEOUT = ZX_MSEC(500u);
  zx_channel_call_args_t args;

  FXL_DCHECK((resp_handle_out == nullptr) ||
             (*resp_handle_out == ZX_HANDLE_INVALID));

  args.wr_bytes = const_cast<ReqType*>(&req);
  args.wr_num_bytes = sizeof(ReqType);
  args.wr_handles = nullptr;
  args.wr_num_handles = 0;
  args.rd_bytes = resp;
  args.rd_num_bytes = sizeof(RespType);
  args.rd_handles = resp_handle_out;
  args.rd_num_handles = resp_handle_out ? 1 : 0;

  uint32_t bytes, handles;
  zx_status_t read_status, write_status;

  write_status = channel.call(0, zx_deadline_after(CALL_TIMEOUT), &args, &bytes,
                              &handles, &read_status);

  if (write_status != ZX_OK) {
    if (write_status == ZX_ERR_CALL_FAILED) {
      FXL_LOG(WARNING) << "Cmd read failure (cmd 0x" << std::hex
                       << std::setfill('0') << std::setw(4) << req.hdr.cmd
                       << ", res " << std::dec << std::setfill(' ')
                       << std::setw(0) << read_status << ")";
      return read_status;
    } else {
      FXL_LOG(WARNING) << "Cmd read failure (cmd 0x" << std::hex
                       << std::setfill('0') << std::setw(4) << req.hdr.cmd
                       << ", res " << std::dec << std::setfill(' ')
                       << std::setw(0) << write_status << ")";
      return write_status;
    }
  }

  if (bytes != sizeof(RespType)) {
    FXL_LOG(WARNING) << "Unexpected response size (got " << bytes
                     << ", expected " << sizeof(RespType) << ")";
    return ZX_ERR_INTERNAL;
  }

  return resp->result;
}

MediaResult DriverOutput::Init() {
  // TODO(johngro): Refactor all of this to be asynchronous.

  // Configure our stream's output format, get our ring buffer back upon
  // success.  If anything goes wrong, immediately close all of our driver
  // resources.
  //
  // TODO(johngro): Actually do format negotiation here.  Don't depend on on
  // 48KHz 16-bit stereo.
  zx_status_t res;
  auto cleanup = fbl::MakeAutoCall([&]() { Cleanup(); });

  {
    audio_stream_cmd_set_format_req_t req;
    audio_stream_cmd_set_format_resp_t resp;
    req.hdr.cmd = AUDIO_STREAM_CMD_SET_FORMAT;
    req.hdr.transaction_id = TXID;
    req.frames_per_second = kDefaultFramesPerSec;
    req.channels = kDefaultChannelCount;
    req.sample_format = kDefaultAudioFmt;

    res = SyncDriverCall(stream_channel_, req, &resp,
                         rb_channel_.reset_and_get_address());
    if (res != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to set format " << req.frames_per_second
                     << "Hz " << req.channels << "-Ch 0x" << std::hex
                     << req.sample_format << "(res " << std::dec << res << ")";
      return MediaResult::UNSUPPORTED_CONFIG;
    }
  }

  // Fetch the initial plug state, and enable plug state notifications if
  // supported by the stream.  For now, process the result(s) using the
  // EventReflector asynchronously, not here.
  {
    audio_stream_cmd_plug_detect_req_t req;
    req.hdr.cmd = AUDIO_STREAM_CMD_PLUG_DETECT;
    req.hdr.transaction_id = std::numeric_limits<zx_txid_t>::max();
    req.flags = AUDIO_PDF_ENABLE_NOTIFICATIONS;

    res = stream_channel_.write(0, &req, sizeof(req), nullptr, 0);
    if (res != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to request initial plug state (res " << res
                     << ")";
      return MediaResult::INTERNAL_ERROR;
    }

    // Create the reflector and hand the stream channel over to it.
    reflector_ = EventReflector::Create(manager_, weak_self_);
    FXL_DCHECK(reflector_ != nullptr);

    res = reflector_->Activate(fbl::move(stream_channel_));
    if (res != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to activate event reflector (res " << res
                     << ")";
      return MediaResult::INTERNAL_ERROR;
    }
  }

  // Fetch the fifo depth of the ring buffer we just got back.  This determines
  // how far ahead of the current playout position (in bytes) the hardware may
  // read.
  {
    audio_rb_cmd_get_fifo_depth_req req;
    audio_rb_cmd_get_fifo_depth_resp resp;

    req.hdr.cmd = AUDIO_RB_CMD_GET_FIFO_DEPTH;
    req.hdr.transaction_id = TXID;

    res = SyncDriverCall(rb_channel_, req, &resp);
    if (res != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to fetch ring buffer fifo depth (res " << res
                     << ")";
      return MediaResult::INTERNAL_ERROR;
    }

    rb_fifo_depth_ = resp.fifo_depth;

    // TODO(johngro): Base the requested ring buffer size on the fifo depth of
    // the ring buffer channel, do not hard code it.
  }

  // Select our output formatter
  AudioMediaTypeDetailsPtr config(AudioMediaTypeDetails::New());
  config->frames_per_second = kDefaultFramesPerSec;
  config->channels = kDefaultChannelCount;
  config->sample_format = kDefaultMediaFrameworkFmt;

  output_formatter_ = OutputFormatter::Select(config);
  if (!output_formatter_) {
    FXL_LOG(ERROR) << "Failed to find output formatter for format "
                   << config->frames_per_second << "Hz " << config->channels
                   << "-Ch 0x" << std::hex << config->sample_format;
    return MediaResult::UNSUPPORTED_CONFIG;
  }

  // Request a ring-buffer VMO from the ring buffer channel.
  {
    audio_rb_cmd_get_buffer_req_t req;
    audio_rb_cmd_get_buffer_resp_t resp;

    req.hdr.cmd = AUDIO_RB_CMD_GET_BUFFER;
    req.hdr.transaction_id = TXID;
    req.min_ring_buffer_frames = kDefaultRingBufferFrames;
    req.notifications_per_ring = 0;

    res = SyncDriverCall(rb_channel_, req, &resp,
                         rb_vmo_.reset_and_get_address());

    // TODO(johngro): Do a better job of translating errors.
    if (res != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to get ring buffer VMO (res " << res << ")";
      return MediaResult::INSUFFICIENT_RESOURCES;
    }
  }

  // Fetch and sanity check the size of the VMO we got back from the ring buffer
  // channel.
  res = rb_vmo_.get_size(&rb_size_);
  if (res != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to get ring buffer VMO size (res " << res << ")";
    return MediaResult::INTERNAL_ERROR;
  }

  if (rb_size_ < kDefaultRingBufferBytes) {
    FXL_LOG(ERROR) << "Ring buffer size is smaller than we asked for ("
                   << rb_size_ << " < " << kDefaultRingBufferBytes << ")";
    return MediaResult::INTERNAL_ERROR;
  }

  if (rb_size_ % kDefaultFrameSize) {
    FXL_LOG(ERROR) << "Ring buffer size (" << rb_size_
                   << ") is not a multiple of the frame size ("
                   << kDefaultFrameSize << ")";
    return MediaResult::INTERNAL_ERROR;
  }

  rb_frames_ = rb_size_ / kDefaultFrameSize;

  // Map the VMO into our address space and fill it with silence.
  // TODO(johngro) : How do I specify the cache policy for this mapping?
  res = zx_vmar_map(zx_vmar_root_self(), 0u, rb_vmo_.get(), 0u, rb_size_,
                    ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE,
                    reinterpret_cast<uintptr_t*>(&rb_virt_));
  if (res != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to map ring buffer VMO (res " << res << ")";
    return MediaResult::INTERNAL_ERROR;
  }

  // Set up the intermediate buffer at the StandardOutputBase level
  //
  // TODO(johngro): The intermediate buffer probably does not need to be as
  // large as the entire ring buffer.  Consider limiting this to be something
  // only slightly larger than a nominal mix job.
  SetupMixBuffer(rb_frames_);

  // TODO(johngro): Flush the cache (if needed) here.
  output_formatter_->FillWithSilence(rb_virt_, rb_frames_);

  // Success!
  cleanup.cancel();
  return MediaResult::OK;
}

void DriverOutput::Cleanup() {
  if (started_) {
    audio_rb_cmd_stop_req_t req;
    audio_rb_cmd_stop_resp_t resp;

    req.hdr.cmd = AUDIO_RB_CMD_STOP;
    req.hdr.transaction_id = TXID;

    SyncDriverCall(rb_channel_, req, &resp);
  }

  if (rb_virt_ != nullptr) {
    zx_vmar_unmap(rb_vmo_.get(), reinterpret_cast<uintptr_t>(rb_virt_),
                  rb_size_);
    rb_virt_ = nullptr;
  }

  rb_size_ = 0;
  rb_vmo_.reset();
  rb_channel_.reset();
  stream_channel_.reset();
}

bool DriverOutput::StartMixJob(MixJob* job, fxl::TimePoint process_start) {
  int64_t now;

  // TODO(johngro) : See MG-940.  Eliminate this as soon as we have a more
  // official way of meeting real-time latency requirements.
  if (!mix_job_prio_bumped_) {
    zx_thread_set_priority(24 /* HIGH_PRIORITY in LK */);
    mix_job_prio_bumped_ = true;
  }

  if (!started_) {
    audio_rb_cmd_start_req_t req;
    audio_rb_cmd_start_resp_t resp;

    req.hdr.cmd = AUDIO_RB_CMD_START;
    req.hdr.transaction_id = TXID;

    zx_status_t res = SyncDriverCall(rb_channel_, req, &resp);
    if (res != ZX_OK) {
      // TODO(johngro): Ugh... if we cannot start the ring buffer, return
      // without scheduling a callback.  The StandardOutputBase implementation
      // will interpret this as a fatal error and should shut this output down.
      FXL_LOG(ERROR) << "Failed to start ring buffer (res " << res << ")";
      return false;
    }

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
        TimelineRate::Scale(resp.start_ticks, 1000000000u, ticks_per_sec);

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

    started_ = true;
    frames_to_mix_ = 0;
    now = local_start;
  } else {
    now = process_start.ToEpochDelta().ToNanoseconds();
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

void DriverOutput::ScheduleNextLowWaterWakeup() {
  // Schedule the next callback for when we are at the low water mark behind
  // the write pointer.
  int64_t low_water_frames = frames_sent_ - low_water_frames_;
  int64_t low_water_time = local_to_output_.ApplyInverse(low_water_frames);
  SetNextSchedTime(fxl::TimePoint::FromEpochDelta(
      fxl::TimeDelta::FromNanoseconds(low_water_time)));
}

// static
fbl::RefPtr<DriverOutput::EventReflector> DriverOutput::EventReflector::Create(
    AudioOutputManager* manager, AudioOutputWeakPtr output) {
  auto domain = ExecutionDomain::Create();
  if (domain == nullptr) {
    return nullptr;
  }

  return fbl::AdoptRef(new EventReflector(manager, output, fbl::move(domain)));
}

zx_status_t DriverOutput::EventReflector::Activate(zx::channel stream_channel) {
  auto ch = ::audio::dispatcher::Channel::Create();

  if (ch == nullptr) return ZX_ERR_NO_MEMORY;

  // Activate our device channel.  If something goes wrong, clear out the
  // internal device_channel_ reference.
  ::audio::dispatcher::Channel::ProcessHandler
      phandler([reflector = fbl::WrapRefPtr(this)](
                   ::audio::dispatcher::Channel * channel)
                   ->zx_status_t {
                     return reflector->ProcessChannelMessage(channel);
                   });

  ::audio::dispatcher::Channel::ChannelClosedHandler
  chandler([reflector = fbl::WrapRefPtr(this)](
               const ::audio::dispatcher::Channel* channel)
               ->void { reflector->ProcessChannelDeactivate(channel); });

  // Simply activate the channel and get out.  The dispatcher pool will hold a
  // reference to it while it is active.  There is no (current) reason for us
  // to hold a reference to it as we are only using it to listen for events,
  // never to send commands.
  return ch->Activate(fbl::move(stream_channel), default_domain_,
                      fbl::move(phandler), fbl::move(chandler));
}

zx_status_t DriverOutput::EventReflector::ProcessChannelMessage(
    Channel* channel) {
  FXL_DCHECK(channel != nullptr);

  union {
    audio_cmd_hdr_t hdr;
    audio_stream_cmd_plug_detect_resp_t pd_resp;
    audio_stream_plug_detect_notify_t pd_notify;
  } msg;

  uint32_t bytes;
  zx_status_t res = channel->Read(&msg, sizeof(msg), &bytes);
  if (res != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to read message from driver (res " << res << ")";
    return res;
  }

  // The only types of messages we expect at the moment are reponses to the plug
  // detect command, and asynchronous plug detection notifications.
  switch (msg.hdr.cmd) {
    case AUDIO_STREAM_CMD_PLUG_DETECT: {
      if (bytes != sizeof(msg.pd_resp)) {
        FXL_LOG(ERROR) << "Bad message length.  Expected "
                       << sizeof(msg.pd_resp) << " Got " << bytes;
        return ZX_ERR_INVALID_ARGS;
      }

      // TODO(johngro) : If this stream supports plug detection, but requires
      // polling, set up that polling now.

      const auto& m = msg.pd_resp;
      HandlePlugStateChange(m.flags & AUDIO_PDNF_PLUGGED, m.plug_state_time);
      return ZX_OK;
    }

    case AUDIO_STREAM_PLUG_DETECT_NOTIFY: {
      if (bytes != sizeof(msg.pd_notify)) {
        FXL_LOG(ERROR) << "Bad message length.  Expected "
                       << sizeof(msg.pd_notify) << " Got " << bytes;
        return ZX_ERR_INVALID_ARGS;
      }

      const auto& m = msg.pd_notify;
      HandlePlugStateChange(m.flags & AUDIO_PDNF_PLUGGED, m.plug_state_time);
      return ZX_OK;
    }

    default:
      FXL_LOG(ERROR) << "Unexpected message type 0x" << std::hex << msg.hdr.cmd;
      return ZX_ERR_INVALID_ARGS;
  }
}

void DriverOutput::EventReflector::ProcessChannelDeactivate(
    const Channel* channel) {
  // If our stream channel has been unplugged out from under us, the device
  // which publishes our stream has been removed from the system (or the driver
  // has crashed).  We need to begin the process of shutting down this
  // AudioOutput.
  manager_->ScheduleMessageLoopTask(
      [ manager = manager_, weak_output = output_ ]() {
        auto output = weak_output.lock();
        if (output) {
          manager->ShutdownOutput(output);
        }
      });
}

void DriverOutput::EventReflector::HandlePlugStateChange(bool plugged,
                                                         zx_time_t plug_time) {
  // If this was a hardwired output, just use the current time as the plug time.
  if (!plug_time) {
    plug_time = zx_time_get(ZX_CLOCK_MONOTONIC);
  }

  // Reflect this message to the AudioOutputManager so it can deal with the plug
  // state change.
  manager_->ScheduleMessageLoopTask(
      [ manager = manager_, weak_output = output_, plugged, plug_time ]() {
        auto output = weak_output.lock();
        if (output) {
          manager->HandlePlugStateChange(output, plugged, plug_time);
        }
      });
}

}  // namespace audio
}  // namespace media
