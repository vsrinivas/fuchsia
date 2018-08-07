// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/audio_core/audio_driver.h"

#include <audio-proto-utils/format-utils.h>
#include <stdio.h>

#include "garnet/bin/media/audio_core/driver_utils.h"
#include "lib/fidl/cpp/clone.h"
#include "lib/fxl/logging.h"

namespace media {
namespace audio {

static constexpr zx_txid_t TXID = 1;
static constexpr zx_duration_t kDefaultShortCmdTimeout = ZX_MSEC(250);
static constexpr zx_duration_t kDefaultLongCmdTimeout = ZX_SEC(3);

AudioDriver::AudioDriver(AudioDevice* owner) : owner_(owner) {
  FXL_DCHECK(owner_ != nullptr);
  stream_channel_ = ::dispatcher::Channel::Create();
  rb_channel_ = ::dispatcher::Channel::Create();
  cmd_timeout_ = ::dispatcher::Timer::Create();
}

zx_status_t AudioDriver::Init(zx::channel stream_channel) {
  FXL_DCHECK(state_ == State::Uninitialized);

  if ((stream_channel_ == nullptr) || (rb_channel_ == nullptr) ||
      (cmd_timeout_ == nullptr)) {
    return ZX_ERR_NO_RESOURCES;
  }

  // Fetch the KOID of our stream channel.  We will end up using this unique ID
  // as our device's device token.
  zx_status_t res;
  zx_info_handle_basic_t sc_info;
  res = zx_object_get_info(stream_channel.get(), ZX_INFO_HANDLE_BASIC, &sc_info,
                           sizeof(sc_info), nullptr, nullptr);
  if (res != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to to fetch stream channel KOID (res " << res
                   << ")";
    return res;
  }
  stream_channel_koid_ = sc_info.koid;

  // Activate the stream channel.
  ::dispatcher::Channel::ProcessHandler process_handler(
      [this](::dispatcher::Channel* channel) -> zx_status_t {
        OBTAIN_EXECUTION_DOMAIN_TOKEN(token, owner_->mix_domain_);
        FXL_DCHECK(stream_channel_.get() == channel);
        return ProcessStreamChannelMessage();
      });

  ::dispatcher::Channel::ChannelClosedHandler channel_closed_handler(
      [this](const ::dispatcher::Channel* channel) {
        OBTAIN_EXECUTION_DOMAIN_TOKEN(token, owner_->mix_domain_);
        FXL_DCHECK(stream_channel_.get() == channel);
        ShutdownSelf("Stream channel closed unexpectedly");
      });

  res = stream_channel_->Activate(
      fbl::move(stream_channel), owner_->mix_domain_,
      fbl::move(process_handler), fbl::move(channel_closed_handler));
  if (res != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to activate stream channel for AudioDriver!  "
                   << "(res " << res << ")";
    return res;
  }

  // Activate the command timeout timer.
  ::dispatcher::Timer::ProcessHandler cmd_timeout_handler(
      [this](::dispatcher::Timer* timer) -> zx_status_t {
        OBTAIN_EXECUTION_DOMAIN_TOKEN(token, owner_->mix_domain_);
        FXL_DCHECK(cmd_timeout_.get() == timer);
        ShutdownSelf("Unexpected command timeout");
        return ZX_OK;
      });

  res = cmd_timeout_->Activate(owner_->mix_domain_,
                               fbl::move(cmd_timeout_handler));
  if (res != ZX_OK) {
    FXL_LOG(ERROR)
        << "Failed to activate command timeout timer for AudioDriver!  "
        << "(res " << res << ")";
    return res;
  }

  // We are now initialized, but we don't know any of our fundamental driver
  // level info.  Things like...
  //
  // 1) This device's persistent unique ID.
  // 2) The list of formats supported by this device.
  // 3) The user visible strings for this device (manufacturer, product, etc...)
  state_ = State::MissingDriverInfo;
  return ZX_OK;
}

void AudioDriver::Cleanup() {
  fbl::RefPtr<DriverRingBuffer> ring_buffer;
  {
    std::lock_guard<std::mutex> lock(ring_buffer_state_lock_);
    ring_buffer = std::move(ring_buffer_);
    clock_mono_to_ring_pos_bytes_ = TimelineFunction();
    ring_buffer_state_gen_.Next();
  }
  ring_buffer.reset();

  stream_channel_->Deactivate();
  rb_channel_->Deactivate();
  cmd_timeout_->Deactivate();
}

void AudioDriver::SnapshotRingBuffer(RingBufferSnapshot* snapshot) const {
  FXL_DCHECK(snapshot);
  std::lock_guard<std::mutex> lock(ring_buffer_state_lock_);

  snapshot->ring_buffer = ring_buffer_;
  snapshot->clock_mono_to_ring_pos_bytes = clock_mono_to_ring_pos_bytes_;
  snapshot->position_to_end_fence_frames =
      owner_->is_input() ? fifo_depth_frames() : 0;
  snapshot->end_fence_to_start_fence_frames = end_fence_to_start_fence_frames_;
  snapshot->gen_id = ring_buffer_state_gen_.get();
}

fuchsia::media::AudioStreamTypePtr AudioDriver::GetSourceFormat() const {
  std::lock_guard<std::mutex> lock(configured_format_lock_);

  if (!configured_format_)
    return nullptr;

  fuchsia::media::AudioStreamTypePtr result;
  fidl::Clone(configured_format_, &result);
  return result;
}

zx_status_t AudioDriver::GetDriverInfo() {
  // TODO(johngro) : Figure out a better way to assert this!
  OBTAIN_EXECUTION_DOMAIN_TOKEN(token, owner_->mix_domain_);

  // We have to be operational in order to fetch supported formats.
  if (!operational()) {
    FXL_LOG(ERROR)
        << "Cannot fetch supported formats while non-operational (state = "
        << static_cast<uint32_t>(state_) << ")";
    return ZX_ERR_BAD_STATE;
  }

  // If we are already in the process of fetching our initial driver info, just
  // get out now.  We will inform our owner when the process completes.
  if (fetching_driver_info()) {
    return ZX_OK;
  }

  // Send the commands to do the following.
  //
  // 1) Fetch our persistent unique ID.
  // 2) Fetch our manufacturer string.
  // 3) Fetch our product string.
  // 4) Fetch our current gain state/caps
  // 5) Fetch our supported format list.

  // Step #1, fetch unique IDs.
  {
    audio_stream_cmd_get_formats_req_t req;
    req.hdr.cmd = AUDIO_STREAM_CMD_GET_UNIQUE_ID;
    req.hdr.transaction_id = TXID;

    zx_status_t res = stream_channel_->Write(&req, sizeof(req));
    if (res != ZX_OK) {
      ShutdownSelf("Failed to request unique ID.", res);
      return res;
    }
  }

  // Steps #2-3, fetch strings.
  static const audio_stream_string_id_t kStringsToFetch[] = {
      AUDIO_STREAM_STR_ID_MANUFACTURER,
      AUDIO_STREAM_STR_ID_PRODUCT,
  };
  for (const auto string_id : kStringsToFetch) {
    audio_stream_cmd_get_string_req_t req;
    req.hdr.cmd = AUDIO_STREAM_CMD_GET_STRING;
    req.hdr.transaction_id = TXID;
    req.id = string_id;

    zx_status_t res = stream_channel_->Write(&req, sizeof(req));
    if (res != ZX_OK) {
      ShutdownSelf("Failed to request unique ID.", res);
      return res;
    }
  }

  // Step #4.  Fetch our current gain state.
  {
    audio_stream_cmd_get_gain_req_t req;
    req.hdr.cmd = AUDIO_STREAM_CMD_GET_GAIN;
    req.hdr.transaction_id = TXID;

    zx_status_t res = stream_channel_->Write(&req, sizeof(req));
    if (res != ZX_OK) {
      ShutdownSelf("Failed to request gain state.", res);
      return res;
    }
  }

  // Step #5.  Fetch our gain state.
  {
    FXL_DCHECK(format_ranges_.empty());

    // Actually send the request to the driver.
    audio_stream_cmd_get_formats_req_t req;
    req.hdr.cmd = AUDIO_STREAM_CMD_GET_FORMATS;
    req.hdr.transaction_id = TXID;

    zx_status_t res = stream_channel_->Write(&req, sizeof(req));
    if (res != ZX_OK) {
      ShutdownSelf("Failed to request supported format list.", res);
      return res;
    }
  }

  // Setup our command timeout.
  fetch_driver_info_timeout_ = zx_deadline_after(kDefaultShortCmdTimeout);
  SetupCommandTimeout();
  return ZX_OK;
}

zx_status_t AudioDriver::Configure(uint32_t frames_per_second,
                                   uint32_t channels,
                                   fuchsia::media::AudioSampleFormat fmt,
                                   zx_duration_t min_ring_buffer_duration) {
  // TODO(johngro) : Figure out a better way to assert this!
  OBTAIN_EXECUTION_DOMAIN_TOKEN(token, owner_->mix_domain_);

  // Sanity check arguments.
  audio_sample_format_t driver_format;
  if (!driver_utils::AudioSampleFormatToDriverSampleFormat(fmt,
                                                           &driver_format)) {
    FXL_LOG(ERROR) << "Failed to convert Fmt 0x" << std::hex
                   << static_cast<uint32_t>(fmt) << " to driver format.";
    return ZX_ERR_INVALID_ARGS;
  }

  if (channels > std::numeric_limits<uint16_t>::max()) {
    FXL_LOG(ERROR) << "Bad channel count: " << channels;
    return ZX_ERR_INVALID_ARGS;
  }

  // TODO(johngro) : sanity check the min_ring_buffer_sz.

  // Check our known format list for compatibility.
  bool found_format = false;
  for (const auto& fmt_range : format_ranges_) {
    found_format = ::audio::utils::FormatIsCompatible(
        frames_per_second, channels, driver_format, fmt_range);
    if (found_format) {
      break;
    }
  }

  if (!found_format) {
    FXL_LOG(ERROR) << "No compatible format range found when setting format to "
                   << frames_per_second << " Hz " << channels << " Ch Fmt 0x"
                   << std::hex << static_cast<uint32_t>(fmt);
    return ZX_ERR_INVALID_ARGS;
  }

  // We must be in the Unconfigured state in order to change formats.
  // TODO(johngro): Improve this.  We should permit changing formats if we are
  // in either the Unconfigured or Configured state.
  if (state_ != State::Unconfigured) {
    FXL_LOG(ERROR) << "Bad state while attempting to configure for "
                   << frames_per_second << " Hz " << channels << " Ch Fmt 0x"
                   << std::hex << static_cast<uint32_t>(fmt)
                   << " (state = " << static_cast<uint32_t>(state_) << ")";
    return ZX_ERR_BAD_STATE;
  }

  // Record the details of our intended target format
  frames_per_sec_ = frames_per_second;
  channel_count_ = static_cast<uint16_t>(channels);
  sample_format_ = driver_format;
  bytes_per_frame_ =
      ::audio::utils::ComputeFrameSize(channel_count_, sample_format_);
  ;
  min_ring_buffer_duration_ = min_ring_buffer_duration;

  // Start the process of configuring by sending the message to set the format.
  audio_stream_cmd_set_format_req_t req;

  req.hdr.cmd = AUDIO_STREAM_CMD_SET_FORMAT;
  req.hdr.transaction_id = TXID;
  req.frames_per_second = frames_per_sec_;
  req.channels = channel_count_;
  req.sample_format = sample_format_;

  {
    std::lock_guard<std::mutex> lock(configured_format_lock_);
    configured_format_ = fuchsia::media::AudioStreamType::New();
    configured_format_->sample_format = fmt;
    configured_format_->channels = channels;
    configured_format_->frames_per_second = frames_per_second;
  }

  zx_status_t res = stream_channel_->Write(&req, sizeof(req));
  if (res != ZX_OK) {
    ShutdownSelf("Failed to send set format command", res);
    return res;
  }

  // Change state, setup our command timeout and we are finished.
  state_ = State::Configuring_SettingFormat;
  configuration_timeout_ = zx_deadline_after(kDefaultLongCmdTimeout);
  SetupCommandTimeout();

  return ZX_OK;
}

zx_status_t AudioDriver::Start() {
  // TODO(johngro) : Figure out a better way to assert this!
  OBTAIN_EXECUTION_DOMAIN_TOKEN(token, owner_->mix_domain_);

  // In order to start, we must be in the Configured state.
  //
  // Note: Attempting to start while already started is considered to be an
  // error because (since we are already started) we will never deliver the
  // OnDriverStartComplete callback, and it would be confisuing to do so from
  // within the call to start itself (before the user's call to Start even
  // returned)
  if (state_ != State::Configured) {
    FXL_LOG(ERROR) << "Bad state while attempting start (state = "
                   << static_cast<uint32_t>(state_) << ")";
    return ZX_ERR_BAD_STATE;
  }

  // Send the command to start the ring buffer.
  audio_rb_cmd_start_req_t req;
  req.hdr.cmd = AUDIO_RB_CMD_START;
  req.hdr.transaction_id = TXID;
  zx_status_t res = rb_channel_->Write(&req, sizeof(req));
  if (res != ZX_OK) {
    ShutdownSelf("Failed to send set start command", res);
    return res;
  }

  // Change state, setup our command timeout and we are finished.
  state_ = State::Starting;
  configuration_timeout_ = zx_deadline_after(kDefaultShortCmdTimeout);
  SetupCommandTimeout();

  return ZX_OK;
}

zx_status_t AudioDriver::Stop() {
  // TODO(johngro) : Figure out a better way to assert this!
  OBTAIN_EXECUTION_DOMAIN_TOKEN(token, owner_->mix_domain_);

  // In order to stop, we must be in the Started state.
  // TODO(johngro): consider relaxing this restriction to make Stop completely
  // idempotent.  Care would to be taken to handle the case where a user
  // attempts to stop while a start operation is in flight but has not
  // completed.  Allowing multiple start/stop operations to be in flight
  // simultaneously could get quite confusing.
  if (state_ != State::Started) {
    FXL_LOG(ERROR) << "Bad state while attempting stop (state = "
                   << static_cast<uint32_t>(state_) << ")";
    return ZX_ERR_BAD_STATE;
  }

  // Invalidate our timeline transformation here.  To outside observers, we are
  // now stopped.
  {
    std::lock_guard<std::mutex> lock(ring_buffer_state_lock_);
    clock_mono_to_ring_pos_bytes_ = TimelineFunction();
    ring_buffer_state_gen_.Next();
  }

  // Send the command to stop the ring buffer.
  audio_rb_cmd_start_req_t req;
  req.hdr.cmd = AUDIO_RB_CMD_STOP;
  req.hdr.transaction_id = TXID;
  zx_status_t res = rb_channel_->Write(&req, sizeof(req));
  if (res != ZX_OK) {
    ShutdownSelf("Failed to send set stop command", res);
    return res;
  }

  // Since we were just recently in steady state, we should be able to assert
  // that we have no configuration timeout at this point.
  FXL_DCHECK(configuration_timeout_ == ZX_TIME_INFINITE);

  // We are now in the process of stopping.
  state_ = State::Stopping;
  configuration_timeout_ = zx_deadline_after(kDefaultShortCmdTimeout);
  SetupCommandTimeout();

  return ZX_OK;
}

zx_status_t AudioDriver::SetPlugDetectEnabled(bool enabled) {
  // TODO(johngro) : Figure out a better way to assert this!
  OBTAIN_EXECUTION_DOMAIN_TOKEN(token, owner_->mix_domain_);

  if (enabled == pd_enabled_) {
    return ZX_OK;
  }

  audio_stream_cmd_plug_detect_req_t req;
  if (enabled) {
    req.hdr.cmd = AUDIO_STREAM_CMD_PLUG_DETECT;
    req.flags = AUDIO_PDF_ENABLE_NOTIFICATIONS;
    pd_enable_timeout_ = zx_deadline_after(kDefaultShortCmdTimeout);
  } else {
    req.hdr.cmd = static_cast<audio_cmd_t>(AUDIO_STREAM_CMD_PLUG_DETECT |
                                           AUDIO_FLAG_NO_ACK);
    req.flags = AUDIO_PDF_DISABLE_NOTIFICATIONS;
    pd_enable_timeout_ = ZX_TIME_INFINITE;
  }
  req.hdr.transaction_id = TXID;

  zx_status_t res = stream_channel_->Write(&req, sizeof(req));
  if (res != ZX_OK) {
    ShutdownSelf("Failed to request send plug state request", res);
    return res;
  }

  pd_enabled_ = enabled;
  SetupCommandTimeout();

  return ZX_OK;
}

zx_status_t AudioDriver::ReadMessage(
    const fbl::RefPtr<::dispatcher::Channel>& channel, void* buf,
    uint32_t buf_size, uint32_t* bytes_read_out, zx::handle* handle_out) {
  FXL_DCHECK(buf != nullptr);
  FXL_DCHECK(bytes_read_out != nullptr);
  FXL_DCHECK(handle_out != nullptr);
  FXL_DCHECK(buf_size >= sizeof(audio_cmd_hdr_t));

  if (!operational()) {
    return ZX_ERR_BAD_STATE;
  }

  zx_status_t res;
  res = channel->Read(buf, buf_size, bytes_read_out, handle_out);
  if (res != ZX_OK) {
    ShutdownSelf("Error attempting to read channel response", res);
    return res;
  }

  if (*bytes_read_out < sizeof(audio_cmd_hdr_t)) {
    FXL_LOG(ERROR) << "Channel response is too small to hold even a "
                   << "message header (" << *bytes_read_out << " < "
                   << sizeof(audio_cmd_hdr_t) << ").";
    ShutdownSelf();
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

zx_status_t AudioDriver::ProcessStreamChannelMessage() {
  zx_status_t res;
  zx::handle rxed_handle;
  uint32_t bytes_read;
  union {
    audio_cmd_hdr_t hdr;
    audio_stream_cmd_get_unique_id_resp_t get_unique_id;
    audio_stream_cmd_get_string_resp_t get_string;
    audio_stream_cmd_get_gain_resp_t get_gain;
    audio_stream_cmd_get_formats_resp_t get_formats;
    audio_stream_cmd_set_format_resp_t set_format;
    audio_stream_cmd_plug_detect_resp_t pd_resp;
    audio_stream_plug_detect_notify_t pd_notify;
  } msg;
  static_assert(sizeof(msg) <= 256,
                "Message buffer is becoming too large to hold on the stack!");

  res = ReadMessage(stream_channel_, &msg, sizeof(msg), &bytes_read,
                    &rxed_handle);
  if (res != ZX_OK) {
    return res;
  }

  bool plug_state;
  switch (msg.hdr.cmd) {
    case AUDIO_STREAM_CMD_GET_UNIQUE_ID:
      CHECK_RESP(AUDIO_STREAM_CMD_GET_UNIQUE_ID, get_unique_id, false, false);
      persistent_unique_id_ = msg.get_unique_id.unique_id;
      res = OnDriverInfoFetched(kDriverInfoHasUniqueId);
      break;

    case AUDIO_STREAM_CMD_GET_STRING:
      CHECK_RESP(AUDIO_STREAM_CMD_GET_STRING, get_string, false, false);
      res = ProcessGetStringResponse(msg.get_string);
      break;

    case AUDIO_STREAM_CMD_GET_GAIN:
      CHECK_RESP(AUDIO_STREAM_CMD_GET_GAIN, get_gain, false, false);
      res = ProcessGetGainResponse(msg.get_gain);
      break;

    case AUDIO_STREAM_CMD_GET_FORMATS:
      CHECK_RESP(AUDIO_STREAM_CMD_GET_FORMATS, get_formats, false, false);
      res = ProcessGetFormatsResponse(msg.get_formats);
      break;

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

      ReportPlugStateChange(plug_state, msg.pd_resp.plug_state_time);

      pd_enable_timeout_ = ZX_TIME_INFINITE;
      SetupCommandTimeout();
      break;

    case AUDIO_STREAM_PLUG_DETECT_NOTIFY:
      CHECK_RESP(AUDIO_STREAM_CMD_PLUG_DETECT, pd_resp, false, true);
      plug_state = ((msg.pd_resp.flags & AUDIO_PDNF_PLUGGED) != 0);
      ReportPlugStateChange(plug_state, msg.pd_resp.plug_state_time);
      break;

    default:
      FXL_LOG(ERROR) << "Unrecognized stream channel response 0x" << std::hex
                     << msg.hdr.cmd;
      return ZX_ERR_BAD_STATE;
  }

  if (res != ZX_OK) {
    ShutdownSelf("Error while processing stream channel message", res);
  }

  return res;
}

zx_status_t AudioDriver::ProcessRingBufferChannelMessage() {
  zx_status_t res;
  zx::handle rxed_handle;
  uint32_t bytes_read;
  union {
    audio_cmd_hdr_t hdr;
    audio_rb_cmd_get_fifo_depth_resp_t get_fifo_depth;
    audio_rb_cmd_get_buffer_resp_t get_buffer;
    audio_rb_cmd_start_resp_t start;
    audio_rb_cmd_stop_resp_t stop;
  } msg;
  static_assert(sizeof(msg) <= 256,
                "Message buffer is becoming too large to hold on the stack!");

  res = ReadMessage(rb_channel_, &msg, sizeof(msg), &bytes_read, &rxed_handle);
  if (res != ZX_OK) {
    return res;
  }

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

    case AUDIO_RB_CMD_STOP:
      CHECK_RESP(AUDIO_RB_CMD_STOP, stop, false, false);
      res = ProcessStopResponse(msg.stop);
      break;

    default:
      FXL_LOG(ERROR) << "Unrecognized ring buffer channel response 0x"
                     << std::hex << msg.hdr.cmd;
      return ZX_ERR_BAD_STATE;
  }

  if (res != ZX_OK) {
    ShutdownSelf("Error while processing ring buffer message", res);
  }

  return res;
}
#undef CHECK_RESP

zx_status_t AudioDriver::ProcessGetStringResponse(
    audio_stream_cmd_get_string_resp_t& resp) {
  std::string* tgt_string;
  uint32_t info_bit;

  if (state_ != State::MissingDriverInfo) {
    FXL_LOG(ERROR) << "Bad state (" << static_cast<uint32_t>(state_)
                   << ") while handling get string response.";
    return ZX_ERR_BAD_STATE;
  }

  if (resp.result != ZX_OK) {
    FXL_LOG(WARNING) << "Error ( " << resp.result
                     << ") attempting to fetch string id " << resp.id
                     << ".  Replacing with <unknown>.";
    resp.strlen = static_cast<uint32_t>(snprintf(
        reinterpret_cast<char*>(resp.str), sizeof(resp.str), "<unknown>"));
  }

  switch (resp.id) {
    case AUDIO_STREAM_STR_ID_MANUFACTURER:
      info_bit = kDriverInfoHasMfrStr;
      tgt_string = &manufacturer_name_;
      break;

    case AUDIO_STREAM_STR_ID_PRODUCT:
      info_bit = kDriverInfoHasProdStr;
      tgt_string = &product_name_;
      break;

    default:
      FXL_LOG(ERROR) << "Unrecognized string id (" << resp.id << ").";
      return ZX_ERR_INVALID_ARGS;
  }

  if (resp.strlen > sizeof(resp.str)) {
    FXL_LOG(ERROR) << "Bad string length " << resp.strlen
                   << " attempting to fetch string id " << resp.id << ".";
    return ZX_ERR_INTERNAL;
  }

  // Stash the string we just received and update our progress in fetching our
  // initial driver info.
  FXL_DCHECK(tgt_string != nullptr);
  tgt_string->assign(reinterpret_cast<char*>(resp.str), resp.strlen);
  return OnDriverInfoFetched(info_bit);
}

zx_status_t AudioDriver::ProcessGetGainResponse(
    audio_stream_cmd_get_gain_resp_t& resp) {
  hw_gain_state_.cur_mute = resp.cur_mute;
  hw_gain_state_.cur_agc = resp.cur_agc;
  hw_gain_state_.cur_gain = resp.cur_gain;
  hw_gain_state_.can_mute = resp.can_mute;
  hw_gain_state_.can_agc = resp.can_agc;
  hw_gain_state_.min_gain = resp.min_gain;
  hw_gain_state_.max_gain = resp.max_gain;
  hw_gain_state_.gain_step = resp.gain_step;

  return OnDriverInfoFetched(kDriverInfoHasGainState);
}

zx_status_t AudioDriver::ProcessGetFormatsResponse(
    const audio_stream_cmd_get_formats_resp_t& resp) {
  if (!fetching_driver_info()) {
    FXL_LOG(ERROR) << "Received unsolicited get formats response.";
    return ZX_ERR_BAD_STATE;
  }

  // Is this the first response?  If so, resize our format vector
  // before proceeding.
  if (resp.first_format_range_ndx == 0) {
    format_ranges_.reserve(resp.format_range_count);
  }

  // Sanity checks
  if (resp.first_format_range_ndx >= resp.format_range_count) {
    FXL_LOG(ERROR) << "Bad format range index in get formats response! ("
                   << resp.first_format_range_ndx
                   << " >= " << resp.format_range_count;
    return ZX_ERR_INVALID_ARGS;
  }

  if (resp.first_format_range_ndx != format_ranges_.size()) {
    FXL_LOG(ERROR) << "Out of order message in get formats response! ("
                   << resp.first_format_range_ndx
                   << " != " << format_ranges_.size();
    return ZX_ERR_INVALID_ARGS;
  }

  // Add this set of formats to our list.
  uint16_t todo =
      std::min<uint16_t>(resp.format_range_count - resp.first_format_range_ndx,
                         AUDIO_STREAM_CMD_GET_FORMATS_MAX_RANGES_PER_RESPONSE);
  for (uint16_t i = 0; i < todo; ++i) {
    format_ranges_.emplace_back(resp.format_ranges[i]);
  }

  // Record the fact that we have now fetched our format list.  This will handle
  // transitioning to the Unconfigured state and letting our owner know if we
  // have managed to fetch all of the initial driver info we need to operate.
  return OnDriverInfoFetched(kDriverInfoHasFormats);
}

zx_status_t AudioDriver::ProcessSetFormatResponse(
    const audio_stream_cmd_set_format_resp_t& resp, zx::channel rb_channel) {
  if (state_ != State::Configuring_SettingFormat) {
    FXL_LOG(ERROR) << "Received unexpected set format response while in state "
                   << static_cast<uint32_t>(state_);
    return ZX_ERR_BAD_STATE;
  }

  if (resp.result != ZX_OK) {
    FXL_LOG(WARNING) << "Error attempting to set format: " << frames_per_sec_
                     << "Hz " << channel_count_ << "-Ch 0x" << std::hex
                     << sample_format_ << "(res " << std::dec << resp.result
                     << ")";
    return resp.result;
  }

  // TODO(johngro) : See MTWN-61.  Update capturers and outputs to take external
  // delay into account when sampling.
  external_delay_nsec_ = resp.external_delay_nsec;

  // Activate out ring buffer channel in our execution domain.
  ::dispatcher::Channel::ProcessHandler process_handler(
      [this](::dispatcher::Channel* channel) -> zx_status_t {
        OBTAIN_EXECUTION_DOMAIN_TOKEN(token, owner_->mix_domain_);
        FXL_DCHECK(rb_channel_.get() == channel);
        return ProcessRingBufferChannelMessage();
      });

  ::dispatcher::Channel::ChannelClosedHandler channel_closed_handler(
      [this](const ::dispatcher::Channel* channel) {
        OBTAIN_EXECUTION_DOMAIN_TOKEN(token, owner_->mix_domain_);
        FXL_DCHECK(rb_channel_.get() == channel);
        ShutdownSelf("Ring buffer channel closed unexpectedly");
      });

  zx_status_t res;
  res = rb_channel_->Activate(fbl::move(rb_channel), owner_->mix_domain_,
                              fbl::move(process_handler),
                              fbl::move(channel_closed_handler));
  if (res != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to activate ring buffer channel (res = " << res
                   << ")";
    return res;
  }

  // Fetch the fifo depth of the ring buffer we just got back.  This determines
  // how far ahead of the current playout position (in bytes) the hardware may
  // read.  We need to know this number in order to size the ring buffer vmo
  // appropriately
  audio_rb_cmd_get_fifo_depth_req req;

  req.hdr.cmd = AUDIO_RB_CMD_GET_FIFO_DEPTH;
  req.hdr.transaction_id = TXID;

  res = rb_channel_->Write(&req, sizeof(req));
  if (res != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to request ring buffer fifo depth.";
    return res;
  }

  // Things went well, proceed to the next step in the state machine.
  state_ = State::Configuring_GettingFifoDepth;
  configuration_timeout_ = zx_deadline_after(kDefaultShortCmdTimeout);
  SetupCommandTimeout();
  return ZX_OK;
}

zx_status_t AudioDriver::ProcessGetFifoDepthResponse(
    const audio_rb_cmd_get_fifo_depth_resp_t& resp) {
  if (state_ != State::Configuring_GettingFifoDepth) {
    FXL_LOG(ERROR) << "Received unexpected fifo depth response while in state "
                   << static_cast<uint32_t>(state_);
    return ZX_ERR_BAD_STATE;
  }

  if (resp.result != ZX_OK) {
    FXL_LOG(ERROR) << "Error when fetching ring buffer fifo depth (res = "
                   << resp.result << ").";
    return resp.result;
  }

  fifo_depth_bytes_ = resp.fifo_depth;
  fifo_depth_frames_ =
      (fifo_depth_bytes_ + bytes_per_frame_ - 1) / bytes_per_frame_;

  // Figure out how many frames we need in our ring buffer.
  int64_t min_frames_64 = TimelineRate::Scale(
      min_ring_buffer_duration_, bytes_per_frame_ * frames_per_sec_, ZX_SEC(1));
  int64_t overhead =
      static_cast<int64_t>(fifo_depth_bytes_) + bytes_per_frame_ - 1;
  bool overflow =
      ((min_frames_64 == TimelineRate::kOverflow) ||
       (min_frames_64 > (std::numeric_limits<int64_t>::max() - overhead)));

  if (!overflow) {
    min_frames_64 += overhead;
    min_frames_64 /= bytes_per_frame_;
    overflow = min_frames_64 > std::numeric_limits<uint32_t>::max();
  }

  if (overflow) {
    FXL_LOG(ERROR)
        << "Overflow while attempting to compute ring buffer size in frames.";
    FXL_LOG(ERROR) << "duration        : " << min_ring_buffer_duration_;
    FXL_LOG(ERROR) << "bytes per frame : " << bytes_per_frame_;
    FXL_LOG(ERROR) << "frames per sec  : " << frames_per_sec_;
    FXL_LOG(ERROR) << "fifo depth      : " << fifo_depth_bytes_;
    FXL_LOG(ERROR) << "bytes per frame : " << bytes_per_frame_;
    return ZX_ERR_INTERNAL;
  }

  // Request the ring buffer.
  audio_rb_cmd_get_buffer_req_t req;
  req.hdr.cmd = AUDIO_RB_CMD_GET_BUFFER;
  req.hdr.transaction_id = TXID;
  req.min_ring_buffer_frames = static_cast<uint32_t>(min_frames_64);
  req.notifications_per_ring = 0;

  zx_status_t res = rb_channel_->Write(&req, sizeof(req));
  if (res != ZX_OK) {
    ShutdownSelf("Failed to request ring buffer vmo", res);
    return res;
  }

  state_ = State::Configuring_GettingRingBuffer;
  configuration_timeout_ = zx_deadline_after(kDefaultShortCmdTimeout);
  SetupCommandTimeout();
  return ZX_OK;
}

zx_status_t AudioDriver::ProcessGetBufferResponse(
    const audio_rb_cmd_get_buffer_resp_t& resp, zx::vmo rb_vmo) {
  if (state_ != State::Configuring_GettingRingBuffer) {
    FXL_LOG(ERROR) << "Received unexpected get buffer response while in state "
                   << static_cast<uint32_t>(state_);
    return ZX_ERR_BAD_STATE;
  }

  if (resp.result != ZX_OK) {
    ShutdownSelf("Error when fetching ring buffer vmo", resp.result);
    return resp.result;
  }

  {
    std::lock_guard<std::mutex> lock(ring_buffer_state_lock_);

    ring_buffer_ = DriverRingBuffer::Create(fbl::move(rb_vmo), bytes_per_frame_,
                                            resp.num_ring_buffer_frames,
                                            owner_->is_input());
    if (ring_buffer_ == nullptr) {
      ShutdownSelf("Failed to allocate and map driver ring buffer");
      return ZX_ERR_INTERNAL;
    }
    FXL_DCHECK(!clock_mono_to_ring_pos_bytes_.invertable());

    ring_buffer_state_gen_.Next();
  }

  // We are now configured.  Let our owner know about this important milestone.
  state_ = State::Configured;
  configuration_timeout_ = ZX_TIME_INFINITE;
  SetupCommandTimeout();
  owner_->OnDriverConfigComplete();
  return ZX_OK;
}

zx_status_t AudioDriver::ProcessStartResponse(
    const audio_rb_cmd_start_resp_t& resp) {
  if (state_ != State::Starting) {
    FXL_LOG(ERROR) << "Received unexpected start response while in state "
                   << static_cast<uint32_t>(state_);
    return ZX_ERR_BAD_STATE;
  }

  if (resp.result != ZX_OK) {
    ShutdownSelf("Error when starting ring buffer", resp.result);
    return resp.result;
  }

  // Now that we have started up, compute the transformation from clock
  // monotonic to the ring buffer position (in bytes) Then update the ring
  // buffer state's transformation and bump the generation counter.
  TimelineFunction func(0, resp.start_time, frames_per_sec_ * bytes_per_frame_,
                        ZX_SEC(1));
  {
    std::lock_guard<std::mutex> lock(ring_buffer_state_lock_);
    FXL_DCHECK(!clock_mono_to_ring_pos_bytes_.invertable());
    FXL_DCHECK(ring_buffer_ != nullptr);
    clock_mono_to_ring_pos_bytes_ = func;
    ring_buffer_state_gen_.Next();
  }

  // We are now configured.  Let our owner know about this important milestone.
  state_ = State::Started;
  configuration_timeout_ = ZX_TIME_INFINITE;
  SetupCommandTimeout();
  owner_->OnDriverStartComplete();
  return ZX_OK;
}

zx_status_t AudioDriver::ProcessStopResponse(
    const audio_rb_cmd_stop_resp_t& resp) {
  if (state_ != State::Stopping) {
    FXL_LOG(ERROR) << "Received unexpected stop response while in state "
                   << static_cast<uint32_t>(state_);
    return ZX_ERR_BAD_STATE;
  }

  if (resp.result != ZX_OK) {
    ShutdownSelf("Error when stopping ring buffer", resp.result);
    return resp.result;
  }

  // We are now stopped.  Let our owner know about this important milestone.
  state_ = State::Configured;
  configuration_timeout_ = ZX_TIME_INFINITE;
  SetupCommandTimeout();
  owner_->OnDriverStopComplete();
  return ZX_OK;
}

void AudioDriver::ShutdownSelf(const char* debug_reason,
                               zx_status_t debug_status) {
  if (state_ == State::Shutdown) {
    return;
  }

  if (debug_reason != nullptr) {
    FXL_LOG(INFO) << "AudioDriver ("
                  << (owner_->is_input() ? "input" : "output")
                  << ") shutting down: reason = \"" << debug_reason
                  << "\" (status = " << debug_status << ")";
  }

  // Release all of our resources.
  Cleanup();

  owner_->ShutdownSelf();
  state_ = State::Shutdown;
}

void AudioDriver::SetupCommandTimeout() {
  zx_time_t timeout;

  timeout = fetch_driver_info_timeout_;
  timeout = fbl::min(timeout, configuration_timeout_);
  timeout = fbl::min(timeout, pd_enable_timeout_);

  if (last_set_timeout_ != timeout) {
    if (timeout != ZX_TIME_INFINITE) {
      cmd_timeout_->Arm(timeout);
    } else {
      cmd_timeout_->Cancel();
    }
    last_set_timeout_ = timeout;
  }
}

void AudioDriver::ReportPlugStateChange(bool plugged, zx_time_t plug_time) {
  {
    fbl::AutoLock lock(&plugged_lock_);
    plugged_ = plugged;
    plug_time_ = plug_time;
  }

  if (pd_enabled_) {
    owner_->OnDriverPlugStateChange(plugged, plug_time);
  }
}

zx_status_t AudioDriver::OnDriverInfoFetched(uint32_t info) {
  // We should never fetch the same info twice.
  if (fetched_driver_info_ & info) {
    ShutdownSelf("Duplicate driver info fetch\n");
    return ZX_ERR_BAD_STATE;
  }

  // Record the new piece of info we just fetched.
  FXL_DCHECK(state_ == State::MissingDriverInfo);
  fetched_driver_info_ |= info;

  // Have we finished fetching our initial driver info?  If so, cancel the
  // timeout, transition to the unconfigured state, and let our owner know that
  // we have finished.
  if ((fetched_driver_info_ & kDriverInfoHasAll) == kDriverInfoHasAll) {
    // We are done.  Clear the fetch driver info timeout and let our owner know.
    fetch_driver_info_timeout_ = ZX_TIME_INFINITE;
    state_ = State::Unconfigured;
    SetupCommandTimeout();
    owner_->OnDriverInfoFetched();
  }

  return ZX_OK;
}

zx_status_t AudioDriver::SendSetGain(
    const AudioDeviceSettings::GainState& gain_state,
    audio_set_gain_flags_t set_flags) {
  audio_stream_cmd_set_gain_req_t req;
  req.hdr.cmd =
      static_cast<audio_cmd_t>(AUDIO_STREAM_CMD_SET_GAIN | AUDIO_FLAG_NO_ACK);
  req.hdr.transaction_id = TXID;

  // clang-format off
  req.flags = static_cast<audio_set_gain_flags_t>(
      set_flags |
      (gain_state.muted ? AUDIO_SGF_MUTE : 0) |
      (gain_state.agc_enabled ? AUDIO_SGF_AGC : 0));
  // clang-format on
  req.gain = gain_state.db_gain;

  return stream_channel_->Write(&req, sizeof(req));
}

}  // namespace audio
}  // namespace media
