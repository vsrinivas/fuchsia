// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/src/audio_server/platform/magenta/magenta_output.h"

#include <fcntl.h>
#include <magenta/device/audio2.h>
#include <magenta/process.h>
#include <mxio/io.h>
#include <mxtl/atomic.h>
#include <mxtl/auto_call.h>
#include <mxtl/limits.h>
#include <iomanip>

#include "apps/media/src/audio_server/audio_output_manager.h"
#include "lib/ftl/logging.h"

static constexpr bool VERBOSE_TIMING_DEBUG = false;

namespace media {
namespace audio {

static constexpr uint32_t kDefaultFramesPerSec = 48000;
static constexpr uint16_t kDefaultChannelCount = 2;
static constexpr audio2_sample_format_t kDefaultAudio2Fmt =
    AUDIO2_SAMPLE_FORMAT_16BIT;
static constexpr AudioSampleFormat kDefaultMediaFrameworkFmt =
    AudioSampleFormat::SIGNED_16;
static constexpr uint32_t kDefaultFrameSize = 4;
static constexpr uint32_t kDefaultRingBufferMsec = 40;
static constexpr uint32_t kDefaultRingBufferFrames =
    ((kDefaultRingBufferMsec * kDefaultFramesPerSec) + 999) / 1000;
static constexpr uint32_t kDefaultRingBufferBytes =
    kDefaultRingBufferFrames * kDefaultFrameSize;

static constexpr int64_t kDefaultLowWaterNsec = 2000000;   // 2 msec for now
static constexpr int64_t kDefaultHighWaterNsec = 4000000;  // 4 msec for now

static mxtl::atomic<mx_txid_t> TXID_GEN(1);
static thread_local mx_txid_t TXID = TXID_GEN.fetch_add(1);

AudioOutputPtr MagentaOutput::Create(mx::channel channel,
                                     AudioOutputManager* manager) {
  return AudioOutputPtr(new MagentaOutput(std::move(channel), manager));
}

MagentaOutput::MagentaOutput(mx::channel channel, AudioOutputManager* manager)
    : StandardOutputBase(manager), stream_channel_(std::move(channel)) {}

MagentaOutput::~MagentaOutput() {}

template <typename ReqType, typename RespType>
mx_status_t MagentaOutput::SyncDriverCall(const mx::channel& channel,
                                          const ReqType& req,
                                          RespType* resp,
                                          mx_handle_t* resp_handle_out) {
  constexpr mx_time_t CALL_TIMEOUT = 100000000u;
  mx_channel_call_args_t args;

  FTL_DCHECK((resp_handle_out == nullptr) ||
             (*resp_handle_out == MX_HANDLE_INVALID));

  args.wr_bytes = const_cast<ReqType*>(&req);
  args.wr_num_bytes = sizeof(ReqType);
  args.wr_handles = nullptr;
  args.wr_num_handles = 0;
  args.rd_bytes = resp;
  args.rd_num_bytes = sizeof(RespType);
  args.rd_handles = resp_handle_out;
  args.rd_num_handles = resp_handle_out ? 1 : 0;

  uint32_t bytes, handles;
  mx_status_t read_status, write_status;

  write_status =
      channel.call(0, mx_deadline_after(CALL_TIMEOUT), &args, &bytes, &handles, &read_status);

  if (write_status != NO_ERROR) {
    if (write_status == ERR_CALL_FAILED) {
      FTL_LOG(WARNING) << "Cmd read failure (cmd 0x" << std::hex
                       << std::setfill('0') << std::setw(4) << req.hdr.cmd
                       << ", res " << std::dec << std::setfill(' ')
                       << std::setw(0) << read_status << ")";
      return read_status;
    } else {
      FTL_LOG(WARNING) << "Cmd read failure (cmd 0x" << std::hex
                       << std::setfill('0') << std::setw(4) << req.hdr.cmd
                       << ", res " << std::dec << std::setfill(' ')
                       << std::setw(0) << write_status << ")";
      return write_status;
    }
  }

  if (bytes != sizeof(RespType)) {
    FTL_LOG(WARNING) << "Unexpected response size (got " << bytes
                     << ", expected " << sizeof(RespType) << ")";
    return ERR_INTERNAL;
  }

  return resp->result;
}

MediaResult MagentaOutput::Init() {
  // TODO(johngro): Refactor all of this to be asynchronous.

  // Configure our stream's output format, get our ring buffer back upon
  // success.  If anything goes wrong, immediately close all of our driver
  // resources.
  //
  // TODO(johngro): Actually do format negotiation here.  Don't depend on on
  // 48KHz 16-bit stereo.
  mx_status_t res;
  auto cleanup = mxtl::MakeAutoCall([&]() { Cleanup(); });

  {
    audio2_stream_cmd_set_format_req_t req;
    audio2_stream_cmd_set_format_resp_t resp;
    req.hdr.cmd = AUDIO2_STREAM_CMD_SET_FORMAT;
    req.hdr.transaction_id = TXID;
    req.frames_per_second = kDefaultFramesPerSec;
    req.channels = kDefaultChannelCount;
    req.sample_format = kDefaultAudio2Fmt;

    res =
        SyncDriverCall(stream_channel_, req, &resp, rb_channel_.get_address());
    if (res != NO_ERROR) {
      FTL_LOG(ERROR) << "Failed to set format " << req.frames_per_second
                     << "Hz " << req.channels << "-Ch 0x" << std::hex
                     << req.sample_format << "(res " << std::dec << res << ")";
      return MediaResult::UNSUPPORTED_CONFIG;
    }
  }

  // Fetch the initial plug state, and enable plug state notifications if
  // supported by the stream.  For now, process the result(s) using the
  // EventReflector asynchronously, not here.
  {
    audio2_stream_cmd_plug_detect_req_t req;
    req.hdr.cmd = AUDIO2_STREAM_CMD_PLUG_DETECT;
    req.hdr.transaction_id = std::numeric_limits<mx_txid_t>::max();
    req.flags = AUDIO2_PDF_ENABLE_NOTIFICATIONS;

    res = stream_channel_.write(0, &req, sizeof(req), nullptr, 0);
    if (res != NO_ERROR) {
      FTL_LOG(ERROR) << "Failed to request initial plug state (res "
                     << res << ")";
      return MediaResult::INTERNAL_ERROR;
    }

    // Create the reflector and hand the stream channel over to it.
    reflector_ = EventReflector::Create(manager_, weak_self_);
    FTL_DCHECK(reflector_ != nullptr);

    res = reflector_->Activate(mxtl::move(stream_channel_));
    if (res != NO_ERROR) {
      FTL_LOG(ERROR) << "Failed to activate event reflector (res "
                     << res << ")";
      return MediaResult::INTERNAL_ERROR;
    }
  }

  // Fetch the fifo depth of the ring buffer we just got back.  This determines
  // how far ahead of the current playout position (in bytes) the hardware may
  // read.
  {
    audio2_rb_cmd_get_fifo_depth_req req;
    audio2_rb_cmd_get_fifo_depth_resp resp;

    req.hdr.cmd = AUDIO2_RB_CMD_GET_FIFO_DEPTH;
    req.hdr.transaction_id = TXID;

    res = SyncDriverCall(rb_channel_, req, &resp);
    if (res != NO_ERROR) {
      FTL_LOG(ERROR) << "Failed to fetch ring buffer fifo depth (res " << res
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
    FTL_LOG(ERROR) << "Failed to find output formatter for format "
                   << config->frames_per_second << "Hz " << config->channels
                   << "-Ch 0x" << std::hex << config->sample_format;
    return MediaResult::UNSUPPORTED_CONFIG;
  }

  // Request a ring-buffer VMO from the ring buffer channel.
  {
    audio2_rb_cmd_get_buffer_req_t req;
    audio2_rb_cmd_get_buffer_resp_t resp;

    req.hdr.cmd = AUDIO2_RB_CMD_GET_BUFFER;
    req.hdr.transaction_id = TXID;
    req.min_ring_buffer_frames = kDefaultRingBufferFrames;
    req.notifications_per_ring = 0;

    res = SyncDriverCall(rb_channel_, req, &resp, rb_vmo_.get_address());

    // TODO(johngro): Do a better job of translating errors.
    if (res != NO_ERROR) {
      FTL_LOG(ERROR) << "Failed to get ring buffer VMO (res " << res << ")";
      return MediaResult::INSUFFICIENT_RESOURCES;
    }
  }

  // Fetch and sanity check the size of the VMO we got back from the ring buffer
  // channel.
  res = rb_vmo_.get_size(&rb_size_);
  if (res != NO_ERROR) {
    FTL_LOG(ERROR) << "Failed to get ring buffer VMO size (res " << res << ")";
    return MediaResult::INTERNAL_ERROR;
  }

  if (rb_size_ < kDefaultRingBufferBytes) {
    FTL_LOG(ERROR) << "Ring buffer size is smaller than we asked for ("
                   << rb_size_ << " < " << kDefaultRingBufferBytes << ")";
    return MediaResult::INTERNAL_ERROR;
  }

  if (rb_size_ % kDefaultFrameSize) {
    FTL_LOG(ERROR) << "Ring buffer size (" << rb_size_
                   << ") is not a multiple of the frame size ("
                   << kDefaultFrameSize << ")";
    return MediaResult::INTERNAL_ERROR;
  }

  rb_frames_ = rb_size_ / kDefaultFrameSize;

  // Map the VMO into our address space and fill it with silence.
  // TODO(johngro) : How do I specify the cache policy for this mapping?
  res = mx_vmar_map(mx_vmar_root_self(), 0u, rb_vmo_.get(), 0u, rb_size_,
                    MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE,
                    reinterpret_cast<uintptr_t*>(&rb_virt_));
  if (res != NO_ERROR) {
    FTL_LOG(ERROR) << "Failed to map ring buffer VMO (res " << res << ")";
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

void MagentaOutput::Cleanup() {
  if (started_) {
    audio2_rb_cmd_stop_req_t req;
    audio2_rb_cmd_stop_resp_t resp;

    req.hdr.cmd = AUDIO2_RB_CMD_STOP;
    req.hdr.transaction_id = TXID;

    SyncDriverCall(rb_channel_, req, &resp);
  }

  if (rb_virt_ != nullptr) {
    mx_vmar_unmap(rb_vmo_.get(), reinterpret_cast<uintptr_t>(rb_virt_),
                  rb_size_);
    rb_virt_ = nullptr;
  }

  rb_size_ = 0;
  rb_vmo_.reset();
  rb_channel_.reset();
  stream_channel_.reset();
}

bool MagentaOutput::StartMixJob(MixJob* job, ftl::TimePoint process_start) {
  int64_t now;

  if (!started_) {
    audio2_rb_cmd_start_req_t req;
    audio2_rb_cmd_start_resp_t resp;

    req.hdr.cmd = AUDIO2_RB_CMD_START;
    req.hdr.transaction_id = TXID;

    mx_status_t res = SyncDriverCall(rb_channel_, req, &resp);
    if (res != NO_ERROR) {
      // TODO(johngro): Ugh... if we cannot start the ring buffer, return
      // without scheduling a callback.  The StandardOutputBase implementation
      // will interpret this as a fatal error and should shut this output down.
      FTL_LOG(ERROR) << "Failed to start ring buffer (res " << res << ")";
      return false;
    }

    // Convert the start time from the mx_get_ticks timeline to the
    // mx_get_time(MX_CLOCK_MONOTONIC) timeline.
    //
    // TODO(johngro): This conversion makes a bunch of assumptions.  It would be
    // better to just convert the mixer to work in ticks instead of
    // CLOCK_MONOTONIC.  Eventually, we need to work clock recovery into this
    // mix, so this may all become a moot point.
    uint64_t ticks_per_sec = mx_ticks_per_second();
    FTL_DCHECK(ticks_per_sec <= mxtl::numeric_limits<uint32_t>::max());
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
      FTL_LOG(INFO) << "Audio output: FIFO depth (" << fifo_frames_
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

  if (!frames_to_mix_) {
    int64_t rd_ptr_frames = local_to_output_.Apply(now);
    if (rd_ptr_frames >= frames_sent_) {
      FTL_LOG(ERROR)
          << "Fatal underflow: implied read pointer " << rd_ptr_frames
          << " is greater than the number of frames we have sent so far "
          << frames_sent_ << ".";
      return false;
    }

    int64_t frames_in_flight = frames_sent_ - rd_ptr_frames;
    int64_t fill_target = local_to_output_.Apply(now + kDefaultHighWaterNsec);
    FTL_DCHECK((frames_in_flight >= 0) && (frames_in_flight <= rb_frames_));
    FTL_DCHECK(frames_sent_ < fill_target);

    uint32_t rb_space = rb_frames_ - static_cast<uint32_t>(frames_in_flight);
    int64_t desired_frames = fill_target - frames_sent_;
    FTL_DCHECK(desired_frames >= 0);

    if (desired_frames > rb_frames_) {
      FTL_LOG(ERROR) << "Fatal underflow: want to produce " << desired_frames
                     << " but the ring buffer is only " << rb_frames_
                     << " frames long.";
      return false;
    }

    frames_to_mix_ =
        static_cast<uint32_t>(mxtl::min<int64_t>(rb_space, desired_frames));
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

bool MagentaOutput::FinishMixJob(const MixJob& job) {
  // TODO(johngro): Flush cache here!

  if (VERBOSE_TIMING_DEBUG) {
    int64_t now = ftl::TimePoint::Now().ToEpochDelta().ToNanoseconds();
    int64_t rd_ptr_frames = local_to_output_.Apply(now);
    int64_t playback_lead_start = frames_sent_ - rd_ptr_frames;
    int64_t playback_lead_end = playback_lead_start + job.buf_frames;
    int64_t dma_lead_start = playback_lead_start - fifo_frames_;
    int64_t dma_lead_end = playback_lead_end - fifo_frames_;

    FTL_LOG(INFO) << "PLead [" << std::setw(4) << playback_lead_start << ", "
                  << std::setw(4) << playback_lead_end << "] DLead ["
                  << std::setw(4) << dma_lead_start << ", " << std::setw(4)
                  << dma_lead_end << "]";
  }

  FTL_DCHECK(frames_to_mix_ >= job.buf_frames);
  frames_sent_ += job.buf_frames;
  frames_to_mix_ -= job.buf_frames;

  if (!frames_to_mix_) {
    // Schedule the next callback for when we are at the low water mark behind
    // the write pointer.
    int64_t low_water_time =
        local_to_output_.ApplyInverse(frames_sent_ - low_water_frames_);
    SetNextSchedTime(ftl::TimePoint::FromEpochDelta(
        ftl::TimeDelta::FromNanoseconds(low_water_time)));
    return false;
  }

  return true;
}

mx_status_t MagentaOutput::EventReflector::Activate(
    mx::channel stream_channel) {
  auto ch = ::audio::DispatcherChannelAllocator::New();

  if (ch == nullptr)
    return ERR_NO_MEMORY;

  // Simply activate the channel and get out.  The dispatcher pool will hold a
  // reference to it while it is active.  There is no (current) reason for us
  // to hold a reference to it as we are only using it to listen for events,
  // never to send commands.
  return ch->Activate(mxtl::WrapRefPtr(this), mxtl::move(stream_channel));
}

mx_status_t MagentaOutput::EventReflector::ProcessChannel(
    DispatcherChannel* channel) {
  FTL_DCHECK(channel != nullptr);

  union {
    audio2_cmd_hdr_t hdr;
    audio2_stream_cmd_plug_detect_resp_t pd_resp;
    audio2_stream_plug_detect_notify_t   pd_notify;
  } msg;

  uint32_t bytes;
  mx_status_t res = channel->Read(&msg, sizeof(msg), &bytes);
  if (res != NO_ERROR) {
    FTL_LOG(ERROR) << "Failed to read message from driver (res " << res << ")";
    return res;
  }

  // The only types of messages we expect at the moment are reponses to the plug
  // detect command, and asynchronous plug detection notifications.
  switch (msg.hdr.cmd) {
    case AUDIO2_STREAM_CMD_PLUG_DETECT: {
      if (bytes != sizeof(msg.pd_resp)) {
        FTL_LOG(ERROR) << "Bad message length.  Expected "
                       << sizeof(msg.pd_resp)
                       << " Got " << bytes;
        return ERR_INVALID_ARGS;
      }

      // TODO(johngro) : If this stream supports plug detection, but requires
      // polling, set up that polling now.

      const auto& m = msg.pd_resp;
      HandlePlugStateChange(m.flags & AUDIO2_PDNF_PLUGGED, m.plug_state_time);
      return NO_ERROR;
    }

    case AUDIO2_STREAM_PLUG_DETECT_NOTIFY: {
      if (bytes != sizeof(msg.pd_notify)) {
        FTL_LOG(ERROR) << "Bad message length.  Expected "
                       << sizeof(msg.pd_notify) << " Got "
                       << bytes;
        return ERR_INVALID_ARGS;
      }

      const auto& m = msg.pd_notify;
      HandlePlugStateChange(m.flags & AUDIO2_PDNF_PLUGGED, m.plug_state_time);
      return NO_ERROR;
    }

    default:
      FTL_LOG(ERROR) << "Unexpected message type 0x" << std::hex << msg.hdr.cmd;
      return ERR_INVALID_ARGS;
  }
}

void MagentaOutput::EventReflector::NotifyChannelDeactivated(
    const DispatcherChannel& channel) {
  // If our stream channel has been unplugged out from under us, the deivce
  // which publishes our stream has been removed from the system (or the driver
  // has crashed).  We need to begin the process of shutting down this
  // AudioOutput.
  manager_->ScheduleMessageLoopTask(
      [manager = manager_, weak_output = output_]() {
        auto output = weak_output.lock();
        if (output) {
          manager->ShutdownOutput(output);
        }
      });
}

void MagentaOutput::EventReflector::HandlePlugStateChange(bool plugged,
                                                          mx_time_t plug_time) {
  // Reflect this message to the AudioOutputManager so it can deal with the plug
  // state change.
  manager_->ScheduleMessageLoopTask(
      [manager = manager_, weak_output = output_, plugged, plug_time]() {
        auto output = weak_output.lock();
        if (output) {
          FTL_DLOG(INFO) << "[" << plug_time << "] Plug state is now "
                         << (plugged ? "plugged" : "unplugged");
          FTL_DLOG(INFO) <<
            "TODO(johngro): Implement plug state handler in output manager";
        }
      });
}

}  // namespace audio
}  // namespace media
