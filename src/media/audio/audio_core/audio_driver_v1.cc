// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.
#include <lib/async/cpp/time.h>
#include <lib/fidl/cpp/clone.h>
#include <lib/trace/event.h>
#include <lib/zx/clock.h>
#include <zircon/status.h>

#include <algorithm>
#include <cstdio>
#include <iomanip>

#include <audio-proto-utils/format-utils.h>

#include "src/media/audio/audio_core/audio_driver.h"
#include "src/media/audio/lib/clock/clone_mono.h"
#include "src/media/audio/lib/clock/utils.h"
#include "src/media/audio/lib/format/driver_format.h"
#include "src/media/audio/lib/logging/logging.h"

namespace media::audio {
namespace {

static constexpr zx_txid_t TXID = 1;

static constexpr bool kEnablePositionNotifications = false;
// To what extent should position notification messages be logged? If logging level is SPEW, every
// notification is logged (specified by Spew const). If TRACE, log less frequently, specified by
// Trace const. If INFO, even less frequently per Info const (INFO is default for DEBUG builds).
// Default for audio_core in NDEBUG builds is WARNING, so by default we do not log any of these
// messages on Release builds. Set to false to not log at all, even for unsolicited notifications.
static constexpr bool kLogPositionNotifications = false;
static constexpr uint16_t kPositionNotificationSpewInterval = 1;
static constexpr uint16_t kPositionNotificationTraceInterval = 60;
static constexpr uint16_t kPositionNotificationInfoInterval = 3600;

// TODO(fxbug.dev/39092): Log a cobalt metric for this.
void LogMissedCommandDeadline(zx::duration delay) {
  FX_LOGS(WARNING) << "Driver command missed deadline by " << delay.to_nsecs() << "ns";
}

}  // namespace

AudioDriverV1::AudioDriverV1(AudioDevice* owner) : AudioDriverV1(owner, LogMissedCommandDeadline) {}

AudioDriverV1::AudioDriverV1(AudioDevice* owner, DriverTimeoutHandler timeout_handler)
    : owner_(owner),
      timeout_handler_(std::move(timeout_handler)),
      ref_clock_to_fractional_frames_(fbl::MakeRefCounted<VersionedTimelineFunction>()) {
  FX_DCHECK(owner_ != nullptr);

  // We create the clock as a clone of MONOTONIC, but once the driver provides details (such as the
  // clock domain), this may become a recovered clock, based on DMA progress across the ring buffer.
  // TODO(mpuryear): Clocks should be per-domain not per-driver; default is the MONO domain's clock.
  audio_clock_ = AudioClock::CreateAsDeviceStatic(audio::clock::AdjustableCloneOfMonotonic(),
                                                  AudioClock::kMonotonicDomain);
  FX_DCHECK(audio_clock_.is_valid()) << "AdjustableCloneOfMonotonic failed";
}

zx_status_t AudioDriverV1::Init(zx::channel stream_channel) {
  TRACE_DURATION("audio", "AudioDriverV1::Init");
  // TODO(MTWN-385): Figure out a better way to assert this!
  OBTAIN_EXECUTION_DOMAIN_TOKEN(token, &owner_->mix_domain());
  FX_DCHECK(state_ == State::Uninitialized);

  // Fetch the KOID of our stream channel. We use this unique ID as our device's device token.
  zx_status_t res;
  zx_info_handle_basic_t sc_info;
  res = stream_channel.get_info(ZX_INFO_HANDLE_BASIC, &sc_info, sizeof(sc_info), nullptr, nullptr);
  if (res != ZX_OK) {
    FX_PLOGS(ERROR, res) << "Failed to to fetch stream channel KOID";
    return res;
  }
  stream_channel_koid_ = sc_info.koid;

  // Setup async wait on channel.
  stream_channel_wait_.set_object(stream_channel.get());
  stream_channel_wait_.set_trigger(ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED);
  stream_channel_wait_.set_handler([this](async_dispatcher_t* dispatcher, async::WaitBase* wait,
                                          zx_status_t status, const zx_packet_signal_t* signal) {
    OBTAIN_EXECUTION_DOMAIN_TOKEN(token, &owner_->mix_domain());
    StreamChannelSignalled(dispatcher, wait, status, signal);
  });
  res = stream_channel_wait_.Begin(owner_->mix_domain().dispatcher());
  if (res != ZX_OK) {
    FX_PLOGS(ERROR, res) << "Failed to wait on stream channel for AudioDriverV1";
    return res;
  }
  stream_channel_ = std::move(stream_channel);

  cmd_timeout_.set_handler([this] {
    OBTAIN_EXECUTION_DOMAIN_TOKEN(token, &owner_->mix_domain());
    DriverCommandTimedOut();
  });

  // We are now initialized, but we don't know any fundamental driver level info, such as:
  //
  // 1) This device's persistent unique ID.
  // 2) The list of formats supported by this device.
  // 3) The user-visible strings for this device (manufacturer, product, etc...).
  state_ = State::MissingDriverInfo;
  return ZX_OK;
}

void AudioDriverV1::Cleanup() {
  TRACE_DURATION("audio", "AudioDriverV1::Cleanup");
  // TODO(MTWN-385): Figure out a better way to assert this!
  OBTAIN_EXECUTION_DOMAIN_TOKEN(token, &owner_->mix_domain());
  std::shared_ptr<ReadableRingBuffer> readable_ring_buffer;
  std::shared_ptr<WritableRingBuffer> writable_ring_buffer;
  {
    std::lock_guard<std::mutex> lock(ring_buffer_state_lock_);
    readable_ring_buffer = std::move(readable_ring_buffer_);
    writable_ring_buffer = std::move(writable_ring_buffer_);
  }

  ref_clock_to_fractional_frames_->Update({});
  readable_ring_buffer = nullptr;
  writable_ring_buffer = nullptr;

  stream_channel_wait_.Cancel();
  ring_buffer_channel_wait_.Cancel();
  cmd_timeout_.Cancel();
}

std::optional<Format> AudioDriverV1::GetFormat() const {
  TRACE_DURATION("audio", "AudioDriverV1::GetFormat");
  std::lock_guard<std::mutex> lock(configured_format_lock_);
  return configured_format_;
}

zx_status_t AudioDriverV1::GetDriverInfo() {
  TRACE_DURATION("audio", "AudioDriverV1::GetDriverInfo");
  // TODO(MTWN-385): Figure out a better way to assert this!
  OBTAIN_EXECUTION_DOMAIN_TOKEN(token, &owner_->mix_domain());

  // We have to be operational in order to fetch supported formats.
  if (!operational()) {
    FX_LOGS(ERROR) << "Cannot fetch supported formats while non-operational (state = "
                   << static_cast<uint32_t>(state_) << ")";
    return ZX_ERR_BAD_STATE;
  }

  // If already fetching initial driver info, get out now and inform our owner when this completes.
  if (fetching_driver_info()) {
    return ZX_OK;
  }

  // Send the commands to do the following.
  //
  // 1) Fetch our persistent unique ID.
  // 2) Fetch our manufacturer string.
  // 3) Fetch our product string.
  // 4) Fetch our current gain state and capabilities.
  // 5) Fetch our supported format list.
  // 6) Fetch our clock domain.

  // Step #1, fetch unique IDs.
  {
    audio_stream_cmd_get_formats_req_t req;
    req.hdr.cmd = AUDIO_STREAM_CMD_GET_UNIQUE_ID;
    req.hdr.transaction_id = TXID;

    zx_status_t res = stream_channel_.write(0, &req, sizeof(req), nullptr, 0);
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

    zx_status_t res = stream_channel_.write(0, &req, sizeof(req), nullptr, 0);
    if (res != ZX_OK) {
      ShutdownSelf("Failed to request string.", res);
      return res;
    }
  }

  // Step #4. Fetch our current gain state.
  {
    audio_stream_cmd_get_gain_req_t req;
    req.hdr.cmd = AUDIO_STREAM_CMD_GET_GAIN;
    req.hdr.transaction_id = TXID;

    zx_status_t res = stream_channel_.write(0, &req, sizeof(req), nullptr, 0);
    if (res != ZX_OK) {
      ShutdownSelf("Failed to request gain state.", res);
      return res;
    }
  }

  // Step #5. Fetch our list of supported formats.
  {
    FX_DCHECK(format_ranges_.empty());

    // Actually send the request to the driver.
    audio_stream_cmd_get_formats_req_t req;
    req.hdr.cmd = AUDIO_STREAM_CMD_GET_FORMATS;
    req.hdr.transaction_id = TXID;

    zx_status_t res = stream_channel_.write(0, &req, sizeof(req), nullptr, 0);
    if (res != ZX_OK) {
      ShutdownSelf("Failed to request supported format list.", res);
      return res;
    }
  }

  // Step #6. Fetch our clock domain.
  {
    audio_stream_cmd_get_clock_domain_req_t req;
    req.hdr.cmd = AUDIO_STREAM_CMD_GET_CLOCK_DOMAIN;
    req.hdr.transaction_id = TXID;

    zx_status_t res = stream_channel_.write(0, &req, sizeof(req), nullptr, 0);
    if (res != ZX_OK) {
      ShutdownSelf("Failed to request clock domain.", res);
      return res;
    }
  }

  // Setup our command timeout.
  fetch_driver_info_deadline_ =
      async::Now(owner_->mix_domain().dispatcher()) + kDefaultShortCmdTimeout;
  SetupCommandTimeout();
  return ZX_OK;
}

zx_status_t AudioDriverV1::Configure(const Format& format, zx::duration min_ring_buffer_duration) {
  TRACE_DURATION("audio", "AudioDriverV1::Configure");
  // TODO(MTWN-385): Figure out a better way to assert this!
  OBTAIN_EXECUTION_DOMAIN_TOKEN(token, &owner_->mix_domain());

  uint32_t channels = format.channels();
  uint32_t frames_per_second = format.frames_per_second();
  fuchsia::media::AudioSampleFormat sample_format = format.sample_format();

  // Sanity check arguments.
  audio_sample_format_t driver_format;
  if (!AudioSampleFormatToDriverSampleFormat(sample_format, &driver_format)) {
    FX_LOGS(ERROR) << "Failed to convert Fmt 0x" << std::hex << static_cast<uint32_t>(sample_format)
                   << " to driver format.";
    return ZX_ERR_INVALID_ARGS;
  }

  if (channels > std::numeric_limits<uint16_t>::max()) {
    FX_LOGS(ERROR) << "Bad channel count: " << channels;
    return ZX_ERR_INVALID_ARGS;
  }

  // TODO(MTWN-386): sanity check the min_ring_buffer_duration.

  // Check our known format list for compatibility.
  bool found_format = false;
  for (const auto& fmt_range : format_ranges_) {
    found_format =
        ::audio::utils::FormatIsCompatible(frames_per_second, channels, driver_format, fmt_range);
    if (found_format) {
      break;
    }
  }

  if (!found_format) {
    FX_LOGS(ERROR) << "No compatible format range found when setting format to "
                   << frames_per_second << " Hz " << channels << " Ch Fmt 0x" << std::hex
                   << static_cast<uint32_t>(sample_format);
    return ZX_ERR_INVALID_ARGS;
  }

  // We must be in Unconfigured state to change formats.
  // TODO(MTWN-387): Also permit this if we are in Configured state.
  if (state_ != State::Unconfigured) {
    FX_LOGS(ERROR) << "Bad state while attempting to configure for " << frames_per_second << " Hz "
                   << channels << " Ch Fmt 0x" << std::hex << static_cast<uint32_t>(sample_format)
                   << " (state = " << static_cast<uint32_t>(state_) << ")";
    return ZX_ERR_BAD_STATE;
  }

  // Record the details of our intended target format
  min_ring_buffer_duration_ = min_ring_buffer_duration;
  {
    std::lock_guard<std::mutex> lock(configured_format_lock_);
    configured_format_ = {format};
  }

  // Start the process of configuring by sending the message to set the format.
  audio_stream_cmd_set_format_req_t req;

  req.hdr.cmd = AUDIO_STREAM_CMD_SET_FORMAT;
  req.hdr.transaction_id = TXID;
  req.frames_per_second = frames_per_second;
  req.channels = channels;
  req.sample_format = driver_format;

  zx_status_t res = stream_channel_.write(0, &req, sizeof(req), nullptr, 0);
  if (res != ZX_OK) {
    ShutdownSelf("Failed to send set format command", res);
    return res;
  }

  // Change state, setup our command timeout and we are finished.
  state_ = State::Configuring_SettingFormat;
  configuration_deadline_ = async::Now(owner_->mix_domain().dispatcher()) + kDefaultLongCmdTimeout;
  SetupCommandTimeout();

  return ZX_OK;
}

zx_status_t AudioDriverV1::Start() {
  TRACE_DURATION("audio", "AudioDriverV1::Start");
  // TODO(MTWN-385): Figure out a better way to assert this!
  OBTAIN_EXECUTION_DOMAIN_TOKEN(token, &owner_->mix_domain());

  // In order to start, we must be in the Configured state.
  //
  // Note: Attempting to start while already started is considered an error because (since we are
  // already started) we will never deliver the OnDriverStartComplete callback. It would be
  // confusing to call it directly from here -- before the user's call to Start even returned.
  if (state_ != State::Configured) {
    FX_LOGS(ERROR) << "Bad state while attempting start (state = " << static_cast<uint32_t>(state_)
                   << ")";
    return ZX_ERR_BAD_STATE;
  }

  // Send the command to start the ring buffer.
  audio_rb_cmd_start_req_t req;
  req.hdr.cmd = AUDIO_RB_CMD_START;
  req.hdr.transaction_id = TXID;
  zx_status_t res = ring_buffer_channel_.write(0, &req, sizeof(req), nullptr, 0);
  if (res != ZX_OK) {
    ShutdownSelf("Failed to send set start command", res);
    return res;
  }

  // Change state, setup our command timeout and we are finished.
  state_ = State::Starting;
  configuration_deadline_ = async::Now(owner_->mix_domain().dispatcher()) + kDefaultShortCmdTimeout;
  SetupCommandTimeout();

  return ZX_OK;
}

zx_status_t AudioDriverV1::Stop() {
  TRACE_DURATION("audio", "AudioDriverV1::Stop");
  // TODO(MTWN-385): Figure out a better way to assert this!
  OBTAIN_EXECUTION_DOMAIN_TOKEN(token, &owner_->mix_domain());

  // In order to stop, we must be in the Started state.
  // TODO(MTWN-388): make Stop idempotent. Allow Stop when Configured/Stopping; disallow if
  // Shutdown; consider what to do if Uninitialized/MissingDriverInfo/Unconfigured/Configuring. Most
  // importantly, if driver is Starting, queue the request until Start completes (as we cannot
  // cancel driver commands). Finally, handle multiple Stop calls to be in-flight concurrently.
  if (state_ != State::Started) {
    FX_LOGS(ERROR) << "Bad state while attempting stop (state = " << static_cast<uint32_t>(state_)
                   << ")";
    return ZX_ERR_BAD_STATE;
  }

  // Invalidate our timeline transformation here. To outside observers, we are now stopped.
  ref_clock_to_fractional_frames_->Update({});

  // Send the command to stop the ring buffer.
  audio_rb_cmd_start_req_t req;
  req.hdr.cmd = AUDIO_RB_CMD_STOP;
  req.hdr.transaction_id = TXID;
  zx_status_t res = ring_buffer_channel_.write(0, &req, sizeof(req), nullptr, 0);
  if (res != ZX_OK) {
    ShutdownSelf("Failed to send set stop command", res);
    return res;
  }

  // We were recently in steady state, so assert that we have no configuration timeout at this time.
  FX_DCHECK(configuration_deadline_ == zx::time::infinite());

  // We are now in the Stopping state.
  state_ = State::Stopping;
  configuration_deadline_ = async::Now(owner_->mix_domain().dispatcher()) + kDefaultShortCmdTimeout;
  SetupCommandTimeout();

  return ZX_OK;
}

zx_status_t AudioDriverV1::SetPlugDetectEnabled(bool enabled) {
  TRACE_DURATION("audio", "AudioDriverV1::SetPlugDetectEnabled");
  // TODO(MTWN-385): Figure out a better way to assert this!
  OBTAIN_EXECUTION_DOMAIN_TOKEN(token, &owner_->mix_domain());

  if (enabled == pd_enabled_) {
    return ZX_OK;
  }

  audio_stream_cmd_plug_detect_req_t req;
  if (enabled) {
    req.hdr.cmd = AUDIO_STREAM_CMD_PLUG_DETECT;
    req.flags = AUDIO_PDF_ENABLE_NOTIFICATIONS;
    pd_enable_deadline_ = async::Now(owner_->mix_domain().dispatcher()) + kDefaultShortCmdTimeout;
  } else {
    req.hdr.cmd = static_cast<audio_cmd_t>(AUDIO_STREAM_CMD_PLUG_DETECT | AUDIO_FLAG_NO_ACK);
    req.flags = AUDIO_PDF_DISABLE_NOTIFICATIONS;
    pd_enable_deadline_ = zx::time::infinite();
  }
  req.hdr.transaction_id = TXID;

  zx_status_t res = stream_channel_.write(0, &req, sizeof(req), nullptr, 0);
  if (res != ZX_OK) {
    ShutdownSelf("Failed to request send plug state request", res);
    return res;
  }

  pd_enabled_ = enabled;
  SetupCommandTimeout();

  return ZX_OK;
}

zx_status_t AudioDriverV1::ReadMessage(const zx::channel& channel, void* buf, uint32_t buf_size,
                                       uint32_t* bytes_read_out, zx::handle* handle_out) {
  TRACE_DURATION("audio", "AudioDriverV1::ReadMessage");
  FX_DCHECK(buf != nullptr);
  FX_DCHECK(bytes_read_out != nullptr);
  FX_DCHECK(handle_out != nullptr);
  FX_DCHECK(buf_size >= sizeof(audio_cmd_hdr_t));

  if (!operational()) {
    return ZX_ERR_BAD_STATE;
  }

  zx_status_t res;
  res = channel.read(0, buf, handle_out ? handle_out->reset_and_get_address() : nullptr, buf_size,
                     handle_out ? 1 : 0, bytes_read_out, nullptr);
  if (res != ZX_OK) {
    ShutdownSelf("Error attempting to read channel response", res);
    return res;
  }

  if (*bytes_read_out < sizeof(audio_cmd_hdr_t)) {
    FX_LOGS(ERROR) << "Channel response is too small to hold even a "
                   << "message header (" << *bytes_read_out << " < " << sizeof(audio_cmd_hdr_t)
                   << ").";
    ShutdownSelf("Channel response too small", ZX_ERR_INVALID_ARGS);
    return ZX_ERR_INVALID_ARGS;
  }

  return ZX_OK;
}

#define CHECK_RESP(_ioctl, _payload, _expect_handle, _is_notif)                                \
  do {                                                                                         \
    if ((_expect_handle) != rxed_handle.is_valid()) {                                          \
      /* If SET_FORMAT, we will provide better error info later */                             \
      if (msg.hdr.cmd != AUDIO_STREAM_CMD_SET_FORMAT) {                                        \
        FX_LOGS(ERROR) << ((_expect_handle) ? "Missing" : "Unexpected")                        \
                       << " handle in " #_ioctl " response";                                   \
        return ZX_ERR_INVALID_ARGS;                                                            \
      }                                                                                        \
    }                                                                                          \
    if ((msg.hdr.transaction_id == AUDIO_INVALID_TRANSACTION_ID) != (_is_notif)) {             \
      FX_LOGS(ERROR) << "Bad txn id " << msg.hdr.transaction_id << " in " #_ioctl " response"; \
      return ZX_ERR_INVALID_ARGS;                                                              \
    }                                                                                          \
    if (bytes_read != sizeof(msg._payload)) {                                                  \
      FX_LOGS(ERROR) << "Bad " #_ioctl " response length (" << bytes_read                      \
                     << " != " << sizeof(msg._payload) << ")";                                 \
      return ZX_ERR_INVALID_ARGS;                                                              \
    }                                                                                          \
  } while (0)

zx_status_t AudioDriverV1::ProcessStreamChannelMessage() {
  TRACE_DURATION("audio", "AudioDriverV1::ProcessStreamChannelMessage");
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
    audio_stream_cmd_get_clock_domain_resp_t get_clock_domain;
    audio_stream_cmd_plug_detect_resp_t pd_resp;
    audio_stream_plug_detect_notify_t pd_notify;
  } msg;
  static_assert(sizeof(msg) <= 256, "Message buffer is becoming too large to hold on the stack!");

  res = ReadMessage(stream_channel_, &msg, sizeof(msg), &bytes_read, &rxed_handle);
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
      res = ProcessSetFormatResponse(msg.set_format, zx::channel(rxed_handle.release()));
      break;

    case AUDIO_STREAM_CMD_GET_CLOCK_DOMAIN:
      CHECK_RESP(AUDIO_STREAM_CMD_GET_CLOCK_DOMAIN, get_clock_domain, false, false);
      res = ProcessGetClockDomainResponse(msg.get_clock_domain);
      break;

    case AUDIO_STREAM_CMD_PLUG_DETECT:
      CHECK_RESP(AUDIO_STREAM_CMD_PLUG_DETECT, pd_resp, false, false);
      if ((msg.pd_resp.flags & AUDIO_PDNF_HARDWIRED) != 0) {
        plug_state = true;
      } else {
        plug_state = ((msg.pd_resp.flags & AUDIO_PDNF_PLUGGED) != 0);
        if ((msg.pd_resp.flags & AUDIO_PDNF_CAN_NOTIFY) == 0) {
          // TODO(MTWN-389): If we encounter hardware which must be polled for plug detection, set
          // a timer to periodically check this; don't just assume that output is always plugged in.
          FX_LOGS(WARNING) << "Stream is incapable of async plug detection notifications. Assuming "
                              "that the stream is always plugged in for now.";
          plug_state = true;
        }
      }

      ReportPlugStateChange(plug_state, zx::time(msg.pd_resp.plug_state_time));

      pd_enable_deadline_ = zx::time::infinite();
      SetupCommandTimeout();
      break;

    case AUDIO_STREAM_PLUG_DETECT_NOTIFY:
      CHECK_RESP(AUDIO_STREAM_CMD_PLUG_DETECT_NOTIFY, pd_notify, false, true);
      plug_state = ((msg.pd_notify.flags & AUDIO_PDNF_PLUGGED) != 0);
      ReportPlugStateChange(plug_state, zx::time(msg.pd_notify.plug_state_time));
      break;

    default:
      FX_LOGS(ERROR) << "Unrecognized stream channel response 0x" << std::hex << msg.hdr.cmd;
      return ZX_ERR_BAD_STATE;
  }

  if (res != ZX_OK) {
    ShutdownSelf("Error while processing stream channel message", res);
  }

  return res;
}

zx_status_t AudioDriverV1::ProcessRingBufferChannelMessage() {
  TRACE_DURATION("audio", "AudioDriverV1::ProcessRingBufferChannelMessage");
  zx_status_t res;
  zx::handle rxed_handle;
  uint32_t bytes_read;
  union {
    audio_cmd_hdr_t hdr;
    audio_rb_cmd_get_fifo_depth_resp_t get_fifo_depth;
    audio_rb_cmd_get_buffer_resp_t get_buffer;
    audio_rb_cmd_start_resp_t start;
    audio_rb_cmd_stop_resp_t stop;
    audio_rb_position_notify_t pos_notify;
  } msg;
  static_assert(sizeof(msg) <= 256, "Message buffer is becoming too large to hold on the stack!");

  res = ReadMessage(ring_buffer_channel_, &msg, sizeof(msg), &bytes_read, &rxed_handle);
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
      res = ProcessGetBufferResponse(msg.get_buffer, zx::vmo(rxed_handle.release()));
      break;

    case AUDIO_RB_CMD_START:
      CHECK_RESP(AUDIO_RB_CMD_START, start, false, false);
      res = ProcessStartResponse(msg.start);
      break;

    case AUDIO_RB_CMD_STOP:
      CHECK_RESP(AUDIO_RB_CMD_STOP, stop, false, false);
      res = ProcessStopResponse(msg.stop);
      break;

    case AUDIO_RB_POSITION_NOTIFY:
      CHECK_RESP(AUDIO_RB_POSITION_NOTIFY, pos_notify, false, true);
      res = ProcessPositionNotify(msg.pos_notify);
      break;

    default:
      FX_LOGS(ERROR) << "Unrecognized ring buffer channel response 0x" << std::hex << msg.hdr.cmd;
      return ZX_ERR_BAD_STATE;
  }

  if (res != ZX_OK) {
    ShutdownSelf("Error while processing ring buffer message", res);
  }

  return res;
}
#undef CHECK_RESP

zx_status_t AudioDriverV1::ProcessGetStringResponse(audio_stream_cmd_get_string_resp_t& resp) {
  TRACE_DURATION("audio", "AudioDriverV1::ProcessGetStringResponse");
  std::string* tgt_string;
  uint32_t info_bit;

  if (state_ != State::MissingDriverInfo) {
    FX_LOGS(ERROR) << "Bad state (" << static_cast<uint32_t>(state_)
                   << ") while handling get string response.";
    return ZX_ERR_BAD_STATE;
  }

  if (resp.result != ZX_OK) {
    FX_LOGS(WARNING) << "Error ( " << resp.result << ") attempting to fetch string id " << resp.id
                     << ". Replacing with <unknown>.";
    resp.strlen = static_cast<uint32_t>(
        snprintf(reinterpret_cast<char*>(resp.str), sizeof(resp.str), "<unknown>"));
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
      FX_LOGS(ERROR) << "Unrecognized string id (" << resp.id << ").";
      return ZX_ERR_INVALID_ARGS;
  }

  if (resp.strlen > sizeof(resp.str)) {
    FX_LOGS(ERROR) << "Bad string length " << resp.strlen << " attempting to fetch string id "
                   << resp.id << ".";
    return ZX_ERR_INTERNAL;
  }

  // Stash the string we just received and update our progress in fetching our initial driver info.
  FX_DCHECK(tgt_string != nullptr);
  tgt_string->assign(reinterpret_cast<char*>(resp.str), resp.strlen);
  return OnDriverInfoFetched(info_bit);
}

zx_status_t AudioDriverV1::ProcessGetGainResponse(audio_stream_cmd_get_gain_resp_t& resp) {
  TRACE_DURATION("audio", "AudioDriverV1::ProcessGetGainResponse");
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

zx_status_t AudioDriverV1::ProcessGetFormatsResponse(
    const audio_stream_cmd_get_formats_resp_t& resp) {
  TRACE_DURATION("audio", "AudioDriverV1::ProcessGetFormatsResponse");
  if (!fetching_driver_info()) {
    FX_LOGS(ERROR) << "Received unsolicited get formats response.";
    return ZX_ERR_BAD_STATE;
  }

  // Is this the first response? If so, resize our format vector before proceeding.
  if (resp.first_format_range_ndx == 0) {
    format_ranges_.reserve(resp.format_range_count);
  }

  // Sanity checks
  if (resp.format_range_count == 0) {
    FX_LOGS(ERROR) << "Driver reported that it supports no format ranges!";
    return ZX_ERR_INVALID_ARGS;
  }

  if (resp.first_format_range_ndx >= resp.format_range_count) {
    FX_LOGS(ERROR) << "Bad format range index in get formats response! (index "
                   << resp.first_format_range_ndx << " should be < total "
                   << resp.format_range_count << ")";
    return ZX_ERR_INVALID_ARGS;
  }

  if (resp.first_format_range_ndx != format_ranges_.size()) {
    FX_LOGS(ERROR) << "Out of order message in get formats response! (index "
                   << resp.first_format_range_ndx << " != the expected " << format_ranges_.size()
                   << ")";
    return ZX_ERR_INVALID_ARGS;
  }

  // Add this set of formats to our list.
  uint16_t todo = std::min<uint16_t>(resp.format_range_count - resp.first_format_range_ndx,
                                     AUDIO_STREAM_CMD_GET_FORMATS_MAX_RANGES_PER_RESPONSE);
  for (uint16_t i = 0; i < todo; ++i) {
    format_ranges_.emplace_back(resp.format_ranges[i]);
  }

  // Record that we have fetched our format list. This will transition us to Unconfigured state and
  // let our owner know if we are done fetching all the initial driver info needed to operate.
  return OnDriverInfoFetched(kDriverInfoHasFormats);
}

zx_status_t AudioDriverV1::ProcessSetFormatResponse(const audio_stream_cmd_set_format_resp_t& resp,
                                                    zx::channel rb_channel) {
  TRACE_DURATION("audio", "AudioDriverV1::ProcessSetFormatResponse");
  if (state_ != State::Configuring_SettingFormat) {
    FX_LOGS(ERROR) << "Received unexpected set format response while in state "
                   << static_cast<uint32_t>(state_);
    return ZX_ERR_BAD_STATE;
  }

  auto format = GetFormat();
  if (resp.result != ZX_OK) {
    FX_PLOGS(WARNING, resp.result)
        << "Error attempting to set format: " << format->frames_per_second() << " Hz, "
        << format->channels() << "-chan, 0x" << std::hex
        << fidl::ToUnderlying(format->sample_format());
    if (resp.result == ZX_ERR_ACCESS_DENIED) {
      FX_LOGS(ERROR) << "Another client has likely already opened this device!";
    }
    return resp.result;
  }

  // TODO(MTWN-61): Update AudioCapturers and outputs to incorporate external delay when resampling.
  external_delay_ = zx::nsec(resp.external_delay_nsec);

  // Setup async wait on channel.
  ring_buffer_channel_wait_.set_object(rb_channel.get());
  ring_buffer_channel_wait_.set_trigger(ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED);
  ring_buffer_channel_wait_.set_handler([this](async_dispatcher_t* dispatcher,
                                               async::WaitBase* wait, zx_status_t status,
                                               const zx_packet_signal_t* signal) {
    OBTAIN_EXECUTION_DOMAIN_TOKEN(token, &owner_->mix_domain());
    RingBufferChannelSignalled(dispatcher, wait, status, signal);
  });
  zx_status_t res = ring_buffer_channel_wait_.Begin(owner_->mix_domain().dispatcher());
  if (res != ZX_OK) {
    FX_PLOGS(ERROR, res) << "Failed to wait on ring buffer channel for AudioDriverV1";
    return res;
  }
  ring_buffer_channel_ = std::move(rb_channel);

  // Fetch the fifo depth of the ring buffer we just received. This determines how far ahead of
  // current playout position (in bytes) the hardware may read. We need to know this number, in
  // order to size the ring buffer vmo appropriately.
  audio_rb_cmd_get_fifo_depth_req req;

  req.hdr.cmd = AUDIO_RB_CMD_GET_FIFO_DEPTH;
  req.hdr.transaction_id = TXID;

  res = ring_buffer_channel_.write(0, &req, sizeof(req), nullptr, 0);
  if (res != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to request ring buffer fifo depth.";
    return res;
  }

  // Things went well, proceed to the next step in the state machine.
  state_ = State::Configuring_GettingFifoDepth;
  configuration_deadline_ = async::Now(owner_->mix_domain().dispatcher()) + kDefaultShortCmdTimeout;
  SetupCommandTimeout();
  return ZX_OK;
}

zx_status_t AudioDriverV1::ProcessGetClockDomainResponse(
    audio_stream_cmd_get_clock_domain_resp_t& resp) {
  TRACE_DURATION("audio", "AudioDriverV1::ProcessGetClockDomainResponse");
  clock_domain_ = resp.clock_domain;

  AUDIO_LOG(DEBUG) << "Received clock domain " << clock_domain_;

  return OnDriverInfoFetched(kDriverInfoHasClockDomain);
}

zx_status_t AudioDriverV1::ProcessGetFifoDepthResponse(
    const audio_rb_cmd_get_fifo_depth_resp_t& resp) {
  TRACE_DURATION("audio", "AudioDriverV1::ProcessGetFifoDepthResponse");
  if (state_ != State::Configuring_GettingFifoDepth) {
    FX_LOGS(ERROR) << "Received unexpected fifo depth response while in state "
                   << static_cast<uint32_t>(state_);
    return ZX_ERR_BAD_STATE;
  }

  if (resp.result != ZX_OK) {
    FX_PLOGS(ERROR, resp.result) << "Error when fetching ring buffer fifo depth";
    return resp.result;
  }

  auto format = GetFormat();
  auto bytes_per_frame = format->bytes_per_frame();
  auto frames_per_second = format->frames_per_second();

  uint32_t fifo_depth_bytes = resp.fifo_depth;
  fifo_depth_frames_ = (fifo_depth_bytes + bytes_per_frame - 1) / bytes_per_frame;
  fifo_depth_duration_ =
      zx::nsec(TimelineRate::Scale(fifo_depth_frames_, ZX_SEC(1), frames_per_second));

  AUDIO_LOG(DEBUG) << "Received fifo depth response (in frames) of " << fifo_depth_frames_;

  // Figure out how many frames we need in our ring buffer.
  int64_t min_frames_64 = TimelineRate::Scale(min_ring_buffer_duration_.to_nsecs(),
                                              bytes_per_frame * frames_per_second, ZX_SEC(1));
  int64_t overhead = static_cast<int64_t>(fifo_depth_bytes) + bytes_per_frame - 1;
  bool overflow = ((min_frames_64 == TimelineRate::kOverflow) ||
                   (min_frames_64 > (std::numeric_limits<int64_t>::max() - overhead)));

  if (!overflow) {
    min_frames_64 += overhead;
    min_frames_64 /= bytes_per_frame;
    overflow = min_frames_64 > std::numeric_limits<uint32_t>::max();
  }

  if (overflow) {
    FX_LOGS(ERROR) << "Overflow while attempting to compute ring buffer size in frames.";
    FX_LOGS(ERROR) << "duration        : " << min_ring_buffer_duration_.get();
    FX_LOGS(ERROR) << "bytes per frame : " << bytes_per_frame;
    FX_LOGS(ERROR) << "frames per sec  : " << frames_per_second;
    FX_LOGS(ERROR) << "fifo depth      : " << fifo_depth_bytes;
    return ZX_ERR_INTERNAL;
  }

  AUDIO_LOG_OBJ(DEBUG, this) << "for audio " << (owner_->is_input() ? "input" : "output")
                             << " -- fifo_depth_bytes:" << fifo_depth_bytes
                             << ", fifo_depth_frames:" << fifo_depth_frames_
                             << ", bytes_per_frame:" << bytes_per_frame;

  // Request the ring buffer.
  audio_rb_cmd_get_buffer_req_t req;
  req.hdr.cmd = AUDIO_RB_CMD_GET_BUFFER;
  req.hdr.transaction_id = TXID;
  req.min_ring_buffer_frames = static_cast<uint32_t>(min_frames_64);
  req.notifications_per_ring = (kEnablePositionNotifications ? 2 : 0);

  zx_status_t res = ring_buffer_channel_.write(0, &req, sizeof(req), nullptr, 0);
  if (res != ZX_OK) {
    ShutdownSelf("Failed to request ring buffer vmo", res);
    return res;
  }

  state_ = State::Configuring_GettingRingBuffer;
  configuration_deadline_ = async::Now(owner_->mix_domain().dispatcher()) + kDefaultShortCmdTimeout;
  SetupCommandTimeout();
  return ZX_OK;
}

zx_status_t AudioDriverV1::ProcessGetBufferResponse(const audio_rb_cmd_get_buffer_resp_t& resp,
                                                    zx::vmo rb_vmo) {
  TRACE_DURATION("audio", "AudioDriverV1::ProcessGetBufferResponse");
  if (state_ != State::Configuring_GettingRingBuffer) {
    FX_LOGS(ERROR) << "Received unexpected get buffer response while in state "
                   << static_cast<uint32_t>(state_);
    return ZX_ERR_BAD_STATE;
  }

  if (resp.result != ZX_OK) {
    ShutdownSelf("Error when fetching ring buffer vmo", resp.result);
    return resp.result;
  }

  auto format = GetFormat();
  FX_CHECK(format) << "ProcessGetBufferResponse without an assigned format";
  {
    std::lock_guard<std::mutex> lock(ring_buffer_state_lock_);

    if (owner_->is_input()) {
      readable_ring_buffer_ = BaseRingBuffer::CreateReadableHardwareBuffer(
          *format, ref_clock_to_fractional_frames_, reference_clock(), std::move(rb_vmo),
          resp.num_ring_buffer_frames, fifo_depth_frames(), [this]() {
            OBTAIN_EXECUTION_DOMAIN_TOKEN(token, &owner_->mix_domain());
            auto t = audio_clock_.Read();
            return safe_read_or_write_ref_clock_to_frames_.Apply(t.get());
          });
    } else {
      writable_ring_buffer_ = BaseRingBuffer::CreateWritableHardwareBuffer(
          *format, ref_clock_to_fractional_frames_, reference_clock(), std::move(rb_vmo),
          resp.num_ring_buffer_frames, 0, [this]() {
            OBTAIN_EXECUTION_DOMAIN_TOKEN(token, &owner_->mix_domain());
            auto t = audio_clock_.Read();
            return safe_read_or_write_ref_clock_to_frames_.Apply(t.get());
          });
    }
    if (!readable_ring_buffer_ && !writable_ring_buffer_) {
      ShutdownSelf("Failed to allocate and map driver ring buffer", ZX_ERR_NO_MEMORY);
      return ZX_ERR_NO_MEMORY;
    }
    FX_DCHECK(!ref_clock_to_fractional_frames_->get().first.invertible());
  }

  // We are now Configured. Let our owner know about this important milestone.
  state_ = State::Configured;
  configuration_deadline_ = zx::time::infinite();
  SetupCommandTimeout();
  owner_->OnDriverConfigComplete();
  return ZX_OK;
}

zx_status_t AudioDriverV1::ProcessStartResponse(const audio_rb_cmd_start_resp_t& resp) {
  if (state_ != State::Starting) {
    FX_LOGS(ERROR) << "Received unexpected start response while in state "
                   << static_cast<uint32_t>(state_);
    return ZX_ERR_BAD_STATE;
  }

  if (resp.result != ZX_OK) {
    ShutdownSelf("Error when starting ring buffer", resp.result);
    return resp.result;
  }

  auto format = GetFormat();

  // TODO(johngro) : Drivers _always_ report their start time in clock monotonic
  // time, but we want to set up all of our transformations to be defined in our
  // reference timeline.
  //
  // When we get to the point that audio devices don't always use
  // clock_monotonic as their reference, and instead may be recovering their own
  // reference clock, we want to come back here and make sure that we use our
  // device's kernel clock to transform this clock monotonic start time into a
  // reference clock start time.
  //
  // Note that it is not safe to assume that every time a driver starts that
  // it's reference clock will (initially) be a clone of clock monotonic.
  // Inputs and outputs operating in the same non-monotonic clock domain will be
  // sharing a reference clock which may have already deviated from monotonic by
  // the time that this driver starts.  Also, eventually drivers should be
  // stopping when idle and starting again later when there is work to do.  This
  // does not destroy the clock for the domain, so the second time that the
  // driver starts, it is very likely that the driver reference clock is no
  // longer clock monotonic.
  //
  mono_start_time_ = zx::time(resp.start_time);
  ref_start_time_ = reference_clock().ReferenceTimeFromMonotonicTime(mono_start_time_);

  // We are almost Started. Compute various useful timeline functions.
  // See the comments for the accessors in audio_device.h for detailed descriptions.
  zx::time first_ptscts_time = ref_start_time() + external_delay_;
  uint32_t frames_per_sec = format->frames_per_second();
  uint32_t frac_frames_per_sec = Fixed(frames_per_sec).raw_value();

  ptscts_ref_clock_to_fractional_frames_ = TimelineFunction{
      0,                        // First frac_frame presented/captured at ring buffer is always 0.
      first_ptscts_time.get(),  // First pres/cap time is the start time + the external delay.
      frac_frames_per_sec,      // number of fractional frames per second
      zx::sec(1).get()          // number of clock ticks per second
  };

  safe_read_or_write_ref_clock_to_frames_ = TimelineFunction{
      fifo_depth_frames_,      // The first safe frame at startup is a FIFO's distance away.
      ref_start_time().get(),  // TX/RX start time
      frames_per_sec,          // number of frames per second
      zx::sec(1).get()         // number of clock ticks per second
  };

  ref_clock_to_fractional_frames_->Update(ptscts_ref_clock_to_fractional_frames_);

  // We are now Started. Let our owner know about this important milestone.
  state_ = State::Started;
  configuration_deadline_ = zx::time::infinite();
  SetupCommandTimeout();
  owner_->OnDriverStartComplete();
  return ZX_OK;
}

zx_status_t AudioDriverV1::ProcessStopResponse(const audio_rb_cmd_stop_resp_t& resp) {
  TRACE_DURATION("audio", "AudioDriverV1::ProcessStopResponse");
  if (state_ != State::Stopping) {
    FX_LOGS(ERROR) << "Received unexpected stop response while in state "
                   << static_cast<uint32_t>(state_);
    return ZX_ERR_BAD_STATE;
  }

  if (resp.result != ZX_OK) {
    ShutdownSelf("Error when stopping ring buffer", resp.result);
    return resp.result;
  }

  // We are now stopped and in Configured state. Let our owner know about this important milestone.
  state_ = State::Configured;
  configuration_deadline_ = zx::time::infinite();
  SetupCommandTimeout();
  owner_->OnDriverStopComplete();
  return ZX_OK;
}

// Currently we ignore driver-reported position, using the system-internal clock instead. This is
// benign and can be safely ignored. However, we did not request it, so this may indicate some other
// problem in the driver state machine. Issue a (debug-only) warning, eat the msg, and continue.
zx_status_t AudioDriverV1::ProcessPositionNotify(const audio_rb_position_notify_t& notify) {
  TRACE_DURATION("audio", "AudioDriverV1::ProcessPositionNotify");
  if constexpr (kLogPositionNotifications) {
    if ((kPositionNotificationInfoInterval > 0) &&
        (position_notification_count_ % kPositionNotificationInfoInterval == 0)) {
      AUDIO_LOG_OBJ(INFO, this) << (kEnablePositionNotifications ? "Notification"
                                                                 : "Unsolicited notification")
                                << " (1/" << kPositionNotificationInfoInterval
                                << ") Time:" << notify.monotonic_time << ", Pos:" << std::setw(6)
                                << notify.ring_buffer_pos;
    } else if ((kPositionNotificationTraceInterval > 0) &&
               (position_notification_count_ % kPositionNotificationTraceInterval == 0)) {
      AUDIO_LOG_OBJ(DEBUG, this) << (kEnablePositionNotifications ? "Notification"
                                                                  : "Unsolicited notification")
                                 << " (1/" << kPositionNotificationTraceInterval
                                 << ") Time:" << notify.monotonic_time << ",  Pos:" << std::setw(6)
                                 << notify.ring_buffer_pos;
    } else if ((kPositionNotificationSpewInterval > 0) &&
               (position_notification_count_ % kPositionNotificationSpewInterval == 0)) {
      AUDIO_LOG_OBJ(TRACE, this) << (kEnablePositionNotifications ? "Notification"
                                                                  : "Unsolicited notification")
                                 << " (1/" << kPositionNotificationSpewInterval
                                 << ") Time:" << notify.monotonic_time << ", Pos:" << std::setw(6)
                                 << notify.ring_buffer_pos;
    }
  }
  // Even if we don't log them, keep a running count of position notifications since START.
  ++position_notification_count_;

  return ZX_OK;
}

void AudioDriverV1::ShutdownSelf(const char* reason, zx_status_t status) {
  TRACE_DURATION("audio", "AudioDriverV1::ShutdownSelf");
  if (state_ == State::Shutdown) {
    return;
  }

  if (reason != nullptr) {
    FX_LOGS(INFO) << (owner_->is_input() ? " Input" : "Output") << " shutting down '" << reason
                  << "', status:" << status;
  }

  // Our owner will call our Cleanup function within this call.
  owner_->ShutdownSelf();
  state_ = State::Shutdown;
}

void AudioDriverV1::SetupCommandTimeout() {
  TRACE_DURATION("audio", "AudioDriverV1::SetupCommandTimeout");

  // If we have received a late response, report it now.
  if (driver_last_timeout_ != zx::time::infinite()) {
    auto delay = async::Now(owner_->mix_domain().dispatcher()) - driver_last_timeout_;
    driver_last_timeout_ = zx::time::infinite();
    FX_DCHECK(timeout_handler_);
    timeout_handler_(delay);
  }

  zx::time deadline;

  deadline = fetch_driver_info_deadline_;
  deadline = std::min(deadline, configuration_deadline_);
  deadline = std::min(deadline, pd_enable_deadline_);

  if (cmd_timeout_.last_deadline() != deadline) {
    if (deadline != zx::time::infinite()) {
      cmd_timeout_.PostForTime(owner_->mix_domain().dispatcher(), deadline);
    } else {
      cmd_timeout_.Cancel();
    }
  }
}

void AudioDriverV1::ReportPlugStateChange(bool plugged, zx::time plug_time) {
  TRACE_DURATION("audio", "AudioDriverV1::ReportPlugStateChange");
  {
    std::lock_guard<std::mutex> lock(plugged_lock_);
    plugged_ = plugged;
    plug_time_ = plug_time;
  }

  if (pd_enabled_) {
    owner_->OnDriverPlugStateChange(plugged, plug_time);
  }
}

zx_status_t AudioDriverV1::OnDriverInfoFetched(uint32_t info) {
  TRACE_DURATION("audio", "AudioDriverV1::OnDriverInfoFetched");
  // We should never fetch the same info twice.
  if (fetched_driver_info_ & info) {
    ShutdownSelf("Duplicate driver info fetch\n", ZX_ERR_BAD_STATE);
    return ZX_ERR_BAD_STATE;
  }

  // Record the new piece of info we just fetched.
  FX_DCHECK(state_ == State::MissingDriverInfo);
  fetched_driver_info_ |= info;

  // Have we finished fetching our initial driver info? If so, cancel the timeout, transition to
  // Unconfigured state, and let our owner know that we have finished.
  if ((fetched_driver_info_ & kDriverInfoHasAll) == kDriverInfoHasAll) {
    // We are done. Clear the fetch driver info timeout and let our owner know.
    fetch_driver_info_deadline_ = zx::time::infinite();
    state_ = State::Unconfigured;
    SetupCommandTimeout();
    owner_->OnDriverInfoFetched();
  }

  return ZX_OK;
}

zx_status_t AudioDriverV1::SetGain(const AudioDeviceSettings::GainState& gain_state,
                                   audio_set_gain_flags_t set_flags) {
  TRACE_DURATION("audio", "AudioDriverV1::SetGain");
  audio_stream_cmd_set_gain_req_t req;
  req.hdr.cmd = static_cast<audio_cmd_t>(AUDIO_STREAM_CMD_SET_GAIN | AUDIO_FLAG_NO_ACK);
  req.hdr.transaction_id = TXID;

  // clang-format off
  req.flags = static_cast<audio_set_gain_flags_t>(
      set_flags |
      (gain_state.muted ? AUDIO_SGF_MUTE : 0) |
      (gain_state.agc_enabled ? AUDIO_SGF_AGC : 0));
  // clang-format on
  req.gain = gain_state.gain_db;

  return stream_channel_.write(0, &req, sizeof(req), nullptr, 0);
}

zx_status_t AudioDriverV1::SelectBestFormat(
    uint32_t* frames_per_second_inout, uint32_t* channels_inout,
    fuchsia::media::AudioSampleFormat* sample_format_inout) {
  return media::audio::SelectBestFormat(format_ranges_, frames_per_second_inout, channels_inout,
                                        sample_format_inout);
}

void AudioDriverV1::StreamChannelSignalled(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                                           zx_status_t status, const zx_packet_signal_t* signal) {
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Async wait failed";
    ShutdownSelf("Failed to wait on stream channel");
    return;
  }
  bool readable_asserted = signal->observed & ZX_CHANNEL_READABLE;
  bool peer_closed_asserted = signal->observed & ZX_CHANNEL_PEER_CLOSED;
  if (readable_asserted) {
    zx_status_t status = ProcessStreamChannelMessage();
    if (status != ZX_OK) {
      FX_PLOGS(ERROR, status) << "Failed to process stream channel message";
      ShutdownSelf("Failed to process stream channel message");
      return;
    }
    if (!peer_closed_asserted) {
      wait->Begin(dispatcher);
    }
  }
  if (peer_closed_asserted) {
    ShutdownSelf("Stream channel closed unexpectedly", ZX_ERR_PEER_CLOSED);
  }
}

void AudioDriverV1::RingBufferChannelSignalled(async_dispatcher_t* dispatcher,
                                               async::WaitBase* wait, zx_status_t status,
                                               const zx_packet_signal_t* signal) {
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Async wait failed";
    ShutdownSelf("Failed to wait on ring buffer channel");
    return;
  }
  bool readable_asserted = signal->observed & ZX_CHANNEL_READABLE;
  bool peer_closed_asserted = signal->observed & ZX_CHANNEL_PEER_CLOSED;
  if (readable_asserted) {
    zx_status_t status = ProcessRingBufferChannelMessage();
    if (status != ZX_OK) {
      FX_PLOGS(ERROR, status) << "Failed to process ring buffer channel message";
      ShutdownSelf("Failed to process channel message");
      return;
    }
    if (!peer_closed_asserted) {
      wait->Begin(dispatcher);
    }
  }
  if (peer_closed_asserted) {
    ShutdownSelf("Ring buffer channel closed", ZX_ERR_PEER_CLOSED);
  }
}

void AudioDriverV1::DriverCommandTimedOut() {
  FX_LOGS(WARNING) << "Unexpected driver timeout";
  driver_last_timeout_ = async::Now(owner_->mix_domain().dispatcher());
}

}  // namespace media::audio
