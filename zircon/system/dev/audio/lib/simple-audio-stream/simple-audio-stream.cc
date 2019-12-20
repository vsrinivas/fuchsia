// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/simple-audio-stream/simple-audio-stream.h>
#include <lib/zx/clock.h>
#include <lib/zx/process.h>
#include <lib/zx/thread.h>
#include <zircon/device/audio.h>

#include <limits>
#include <utility>

#include <audio-proto-utils/format-utils.h>
#include <ddk/debug.h>
namespace audio {

void SimpleAudioStream::Shutdown() {
  if (!is_shutdown_) {
    loop_.Shutdown();

    // We have shutdown our loop, it is now safe to assert we are holding the domain token.
    ScopedToken t(domain_token());

    {
      // Now we explicitly destroy the channels.
      fbl::AutoLock channel_lock(&channel_lock_);
      DeactivateRingBufferChannel(rb_channel_.get());

      plug_notify_channels_.clear();
      stream_channels_.clear();
      stream_channel_ = nullptr;
    }

    ShutdownHook();
    is_shutdown_ = true;
  }
}

zx_status_t SimpleAudioStream::CreateInternal() {
  zx_status_t res;

  {
    // We have not created the domain yet, it should be safe to pretend that
    // we have the token (since we know that no dispatches are going to be
    // invoked from the non-existent domain at this point)
    ScopedToken t(domain_token());
    res = Init();
    if (res != ZX_OK) {
      zxlogf(ERROR, "Init failure in %s (res %d)\n", __PRETTY_FUNCTION__, res);
      return res;
    }
    // If no subclass has set this, we need to do so here.
    if (plug_time_ == 0) {
      plug_time_ = zx::clock::get_monotonic().get();
    }
  }

  // TODO(37372): Add profile configuration.
  loop_.StartThread("simple-audio-stream-loop");

  res = PublishInternal();
  if (res != ZX_OK) {
    zxlogf(ERROR, "Publish failure in %s (res %d)\n", __PRETTY_FUNCTION__, res);
    return res;
  }

  return ZX_OK;
}

zx_status_t SimpleAudioStream::PublishInternal() {
  device_name_[sizeof(device_name_) - 1] = 0;
  if (!strlen(device_name_)) {
    zxlogf(ERROR, "Zero-length device name in %s\n", __PRETTY_FUNCTION__);
    return ZX_ERR_BAD_STATE;
  }

  // If we succeed in adding our device, add an explicit reference to
  // ourselves to represent the reference now being held by the DDK.  We will
  // get this reference back when the DDK (eventually) calls release.
  zx_status_t res = DdkAdd(device_name_);
  if (res == ZX_OK) {
    AddRef();
  }

  return res;
}

// Called by a child subclass during Init, to establish the properties of this plug.
// Caller must include only flags defined for audio_stream_cmd_plug_detect_resp_t.
void SimpleAudioStream::SetInitialPlugState(audio_pd_notify_flags_t initial_state) {
  audio_pd_notify_flags_t known_flags =
      AUDIO_PDNF_HARDWIRED | AUDIO_PDNF_CAN_NOTIFY | AUDIO_PDNF_PLUGGED;
  ZX_DEBUG_ASSERT((initial_state & known_flags) == initial_state);

  pd_flags_ = initial_state;
  plug_time_ = zx::clock::get_monotonic().get();
}

// Called by a child subclass when a dynamic plug state change occurs.
// Special behavior if this isn't actually a change, or if we should not be able to unplug.
zx_status_t SimpleAudioStream::SetPlugState(bool plugged) {
  if (plugged == ((pd_flags_ & AUDIO_PDNF_PLUGGED) != 0)) {
    return ZX_OK;
  }

  ZX_DEBUG_ASSERT(((pd_flags_ & AUDIO_PDNF_HARDWIRED) == 0) || plugged);

  if (plugged) {
    pd_flags_ |= AUDIO_PDNF_PLUGGED;
  } else {
    pd_flags_ &= ~AUDIO_PDNF_PLUGGED;
  }
  plug_time_ = zx::clock::get_monotonic().get();

  if (pd_flags_ & AUDIO_PDNF_CAN_NOTIFY) {
    return NotifyPlugDetect();
  }

  return ZX_OK;
}

// Asynchronously notify of plug state changes.
zx_status_t SimpleAudioStream::NotifyPlugDetect() {
  audio_proto::PlugDetectNotify notif;

  notif.hdr.transaction_id = AUDIO_INVALID_TRANSACTION_ID;
  notif.hdr.cmd = AUDIO_STREAM_PLUG_DETECT_NOTIFY;

  notif.flags = pd_flags_;
  notif.plug_state_time = plug_time_;

  for (auto& channel : plug_notify_channels_) {
    // Any error also triggers ChannelClosedHandler; no need to handle it here.
    (void)channel.Write(&notif, sizeof(notif));
  }
  return ZX_OK;
}

zx_status_t SimpleAudioStream::NotifyPosition(const audio_proto::RingBufPositionNotify& notif) {
  if (!expected_notifications_per_ring_.load() || (rb_channel_ == nullptr)) {
    return ZX_ERR_BAD_STATE;
  }

  return rb_channel_->Write(&notif, sizeof(notif));
}

void SimpleAudioStream::DdkUnbindDeprecated() {
  Shutdown();

  // TODO(johngro): We need to signal our SimpleAudioStream owner to let them
  // know that we have been unbound and are in the process of shutting down.

  // Unpublish our device node.
  DdkRemoveDeprecated();
}

void SimpleAudioStream::DdkRelease() {
  // Recover our ref from the DDK, then let it fall out of scope.
  auto thiz = fbl::ImportFromRawPtr(this);
}

void SimpleAudioStream::DdkSuspendNew(ddk::SuspendTxn txn) {
  // TODO(fxb/42613): Implement proper power management based on the requested state.
  Shutdown();
  txn.Reply(ZX_OK, txn.requested_state());
}

void SimpleAudioStream::GetChannel(GetChannelCompleter::Sync completer) {
  fbl::AutoLock channel_lock(&channel_lock_);
  // Attempt to allocate a new driver channel and bind it to us.  If we don't
  // already have an stream_channel_, flag this channel is the privileged
  // connection (The connection which is allowed to do things like change
  // formats).
  bool privileged = (stream_channel_ == nullptr);

  zx::channel stream_channel_local;
  zx::channel stream_channel_remote;
  auto status = zx::channel::create(0, &stream_channel_local, &stream_channel_remote);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Could not create channel in %s\n", __PRETTY_FUNCTION__);
    completer.Close(ZX_ERR_NO_MEMORY);
    return;
  }

  auto stream_channel = StreamChannel::Create<StreamChannel>(std::move(stream_channel_local));
  // We keep alive all channels in stream_channels_ (protected by channel_lock_).
  stream_channels_.push_back(stream_channel);
  // We only use the channels outside channel_lock_ when passing it into the channel handler
  // below, this handler processing is protected by the domain.
  stream_channel->SetHandler([stream = fbl::RefPtr(this), channel = stream_channel.get(),
                              privileged](async_dispatcher_t* dispatcher, async::WaitBase* wait,
                                          zx_status_t status, const zx_packet_signal_t* signal) {
    ScopedToken t(stream->domain_token());
    stream->StreamChannelSignalled(dispatcher, wait, status, signal, channel, privileged);
  });
  status = stream_channel->BeginWait(loop_.dispatcher());
  if (status != ZX_OK) {
    zxlogf(ERROR, "Could not begin wait in %s\n", __PRETTY_FUNCTION__);
    completer.Close(ZX_ERR_NO_MEMORY);
    // We let stream_channel_remote go out of scope to trigger channel deactivation via peer close.
    return;
  }
  if (privileged) {
    ZX_DEBUG_ASSERT(stream_channel_ == nullptr);
    stream_channel_ = stream_channel;
  }
  completer.Reply(std::move(stream_channel_remote));
}

void SimpleAudioStream::StreamChannelSignalled(async_dispatcher_t* dispatcher,
                                               async::WaitBase* wait, zx_status_t status,
                                               const zx_packet_signal_t* signal,
                                               StreamChannel* channel, bool privileged) {
  if (status != ZX_OK) {
    if (status != ZX_ERR_CANCELED) {  // Cancel is expected.
      zxlogf(ERROR, "%s handler error %d\n", __PRETTY_FUNCTION__, status);
    }
    return;
  }
  bool readable_asserted = signal->observed & ZX_CHANNEL_READABLE;
  bool peer_closed_asserted = signal->observed & ZX_CHANNEL_PEER_CLOSED;
  if (readable_asserted) {
    zx_status_t status = ProcessStreamChannel(channel, privileged);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s processing stream channel error %d\n", __PRETTY_FUNCTION__, status);
      return;
    }
    if (!peer_closed_asserted) {
      wait->Begin(dispatcher);
    }
  }
  if (peer_closed_asserted) {
    fbl::AutoLock channel_lock(&this->channel_lock_);
    DeactivateStreamChannel(channel);
  }
}

#define HREQ(_cmd, _payload, _handler, _allow_noack, ...)                    \
  case _cmd:                                                                 \
    if (req_size != sizeof(req._payload)) {                                  \
      zxlogf(ERROR, "Bad " #_cmd " response length (%u != %zu)\n", req_size, \
             sizeof(req._payload));                                          \
      return ZX_ERR_INVALID_ARGS;                                            \
    }                                                                        \
    if (!_allow_noack && (req.hdr.cmd & AUDIO_FLAG_NO_ACK)) {                \
      zxlogf(ERROR, "NO_ACK flag not allowed for " #_cmd "\n");              \
      return ZX_ERR_INVALID_ARGS;                                            \
    }                                                                        \
    return _handler(std::move(channel), req._payload, ##__VA_ARGS__);

zx_status_t SimpleAudioStream::ProcessStreamChannel(StreamChannel* channel, bool privileged) {
  union {
    audio_proto::CmdHdr hdr;
    audio_proto::StreamGetFmtsReq get_formats;
    audio_proto::StreamSetFmtReq set_format;
    audio_proto::GetGainReq get_gain;
    audio_proto::SetGainReq set_gain;
    audio_proto::PlugDetectReq plug_detect;
    audio_proto::GetUniqueIdReq get_unique_id;
    audio_proto::GetStringReq get_string;
  } req;

  static_assert(sizeof(req) <= 256,
                "Request buffer is getting to be too large to hold on the stack!");

  uint32_t req_size;
  zx_status_t res = channel->Read(&req, sizeof(req), &req_size);
  if (res != ZX_OK)
    return res;

  if ((req_size < sizeof(req.hdr) || (req.hdr.transaction_id == AUDIO_INVALID_TRANSACTION_ID))) {
    zxlogf(ERROR, "Bad request in %s\n", __PRETTY_FUNCTION__);
    return ZX_ERR_INVALID_ARGS;
  }

  // Strip the NO_ACK flag from the request before selecting the dispatch target.
  auto cmd = static_cast<audio_proto::Cmd>(req.hdr.cmd & ~AUDIO_FLAG_NO_ACK);
  switch (cmd) {
    HREQ(AUDIO_STREAM_CMD_GET_FORMATS, get_formats, OnGetStreamFormats, false);
    HREQ(AUDIO_STREAM_CMD_SET_FORMAT, set_format, OnSetStreamFormat, false, privileged);
    HREQ(AUDIO_STREAM_CMD_GET_GAIN, get_gain, OnGetGain, false);
    HREQ(AUDIO_STREAM_CMD_SET_GAIN, set_gain, OnSetGain, true);
    HREQ(AUDIO_STREAM_CMD_PLUG_DETECT, plug_detect, OnPlugDetect, true);
    HREQ(AUDIO_STREAM_CMD_GET_UNIQUE_ID, get_unique_id, OnGetUniqueId, false);
    HREQ(AUDIO_STREAM_CMD_GET_STRING, get_string, OnGetString, false);
    default:
      zxlogf(ERROR, "Unrecognized stream command 0x%04x\n", req.hdr.cmd);
      return ZX_ERR_NOT_SUPPORTED;
  }
}

zx_status_t SimpleAudioStream::ProcessRingBufferChannel(Channel* channel) {
  ZX_DEBUG_ASSERT(channel != nullptr);

  union {
    audio_proto::CmdHdr hdr;
    audio_proto::RingBufGetFifoDepthReq get_fifo_depth;
    audio_proto::RingBufGetBufferReq get_buffer;
    audio_proto::RingBufStartReq rb_start;
    audio_proto::RingBufStopReq rb_stop;
  } req;

  static_assert(sizeof(req) <= 256,
                "Request buffer is getting to be too large to hold on the stack!");

  uint32_t req_size;
  zx_status_t res = channel->Read(&req, sizeof(req), &req_size);
  if (res != ZX_OK)
    return res;

  if ((req_size < sizeof(req.hdr) || (req.hdr.transaction_id == AUDIO_INVALID_TRANSACTION_ID))) {
    zxlogf(ERROR, "Bad request in %s\n", __PRETTY_FUNCTION__);
    return ZX_ERR_INVALID_ARGS;
  }

  // Strip the NO_ACK flag from the request before selecting the dispatch target.
  auto cmd = static_cast<audio_proto::Cmd>(req.hdr.cmd & ~AUDIO_FLAG_NO_ACK);
  switch (cmd) {
    HREQ(AUDIO_RB_CMD_GET_FIFO_DEPTH, get_fifo_depth, OnGetFifoDepth, false);
    HREQ(AUDIO_RB_CMD_GET_BUFFER, get_buffer, OnGetBuffer, false);
    HREQ(AUDIO_RB_CMD_START, rb_start, OnStart, false);
    HREQ(AUDIO_RB_CMD_STOP, rb_stop, OnStop, false);
    default:
      zxlogf(ERROR, "Unrecognized ring buffer command 0x%04x\n", req.hdr.cmd);
      return ZX_ERR_NOT_SUPPORTED;
  }
}
#undef HREQ

void SimpleAudioStream::DeactivateStreamChannel(StreamChannel* channel) {
  if (stream_channel_.get() == channel) {
    stream_channel_ = nullptr;
  }
  if (channel->in_plug_notify_list()) {
    plug_notify_channels_.erase(*channel);
  }
  stream_channels_.erase(*channel);  // Must be last since we may destruct *channel.
}

void SimpleAudioStream::DeactivateRingBufferChannel(const Channel* channel) {
  if (rb_channel_.get() == channel) {
    if (rb_started_) {
      Stop();
      rb_started_ = false;
    }
    rb_fetched_ = false;
    expected_notifications_per_ring_.store(0);
    rb_channel_ = nullptr;
  }
}

zx_status_t SimpleAudioStream::OnGetStreamFormats(StreamChannel* channel,
                                                  const audio_proto::StreamGetFmtsReq& req) const {
  ZX_DEBUG_ASSERT(channel != nullptr);
  uint16_t formats_sent = 0;
  audio_proto::StreamGetFmtsResp resp = {};

  if (supported_formats_.size() > std::numeric_limits<uint16_t>::max()) {
    zxlogf(ERROR, "Too many formats (%zu) to send during AUDIO_STREAM_CMD_GET_FORMATS request!\n",
           supported_formats_.size());
    return ZX_ERR_INTERNAL;
  }

  resp.hdr = req.hdr;
  resp.format_range_count = static_cast<uint16_t>(supported_formats_.size());

  do {
    uint16_t todo, payload_sz;
    zx_status_t res;

    todo = fbl::min<uint16_t>(static_cast<uint16_t>(supported_formats_.size() - formats_sent),
                              AUDIO_STREAM_CMD_GET_FORMATS_MAX_RANGES_PER_RESPONSE);
    payload_sz = static_cast<uint16_t>(sizeof(resp.format_ranges[0]) * todo);

    resp.first_format_range_ndx = formats_sent;
    ::memcpy(resp.format_ranges, supported_formats_.data() + formats_sent, payload_sz);

    res = channel->Write(&resp, sizeof(resp));
    if (res != ZX_OK) {
      zxlogf(ERROR, "Failed to send get stream formats response (res %d)\n", res);
      return res;
    }

    formats_sent = (uint16_t)(formats_sent + todo);
  } while (formats_sent < supported_formats_.size());

  return ZX_OK;
}

zx_status_t SimpleAudioStream::OnSetStreamFormat(StreamChannel* channel,
                                                 const audio_proto::StreamSetFmtReq& req,
                                                 bool privileged) {
  ZX_DEBUG_ASSERT(channel != nullptr);
  zx::channel rb_channel_local;
  zx::channel rb_channel_remote;

  audio_proto::StreamSetFmtResp resp = {};
  bool found_one = false;
  resp.hdr = req.hdr;

  // Only the privileged stream channel is allowed to change the format.
  if (!privileged) {
    zxlogf(ERROR, "Unprivileged channel cannot SetStreamFormat\n");
    resp.result = ZX_ERR_ACCESS_DENIED;
    goto finished;
  }

  // Check the format for compatibility
  for (const auto& fmt : supported_formats_) {
    if (audio::utils::FormatIsCompatible(req.frames_per_second, req.channels, req.sample_format,
                                         fmt)) {
      found_one = true;
      break;
    }
  }

  if (!found_one) {
    zxlogf(ERROR, "Could not find a suitable format in %s\n", __PRETTY_FUNCTION__);
    resp.result = ZX_ERR_INVALID_ARGS;
    goto finished;
  }

  // Determine the frame size.
  frame_size_ = audio::utils::ComputeFrameSize(req.channels, req.sample_format);
  if (!frame_size_) {
    zxlogf(ERROR, "Failed to compute frame size (ch %hu fmt 0x%08x)\n", req.channels,
           req.sample_format);
    resp.result = ZX_ERR_INTERNAL;
    goto finished;
  }

  // Looks like we are going ahead with this format change.  Tear down any
  // exiting ring buffer interface before proceeding.
  {
    fbl::AutoLock channel_lock(&channel_lock_);
    if (rb_channel_ != nullptr) {
      DeactivateRingBufferChannel(rb_channel_.get());
      ZX_DEBUG_ASSERT(rb_channel_ == nullptr);
    }
  }

  // Actually attempt to change the format.
  resp.result = ChangeFormat(req);
  if (resp.result != ZX_OK) {
    zxlogf(ERROR, "Could not ChangeFormat in %s\n", __PRETTY_FUNCTION__);
    goto finished;
  }

  // Create a new ring buffer channel which can be used to move bulk data and
  // bind it to us.
  resp.result = zx::channel::create(0, &rb_channel_local, &rb_channel_remote);
  if (resp.result != ZX_OK) {
    zxlogf(ERROR, "Could not create channel in %s\n", __PRETTY_FUNCTION__);
    goto finished;
  }
  {
    fbl::AutoLock channel_lock(&channel_lock_);
    rb_channel_ = Channel::Create<Channel>(std::move(rb_channel_local));
    // We only use the rb_channel outside channel_lock_ when passing it into the channel handler
    // below, this handler processing is protected by the domain.
    rb_channel_->SetHandler([stream = fbl::RefPtr(this), channel = rb_channel_.get()](
                                async_dispatcher_t* dispatcher, async::WaitBase* wait,
                                zx_status_t status, const zx_packet_signal_t* signal) {
      ScopedToken t(stream->domain_token());
      stream->RingBufferSignalled(dispatcher, wait, status, signal, channel);
    });
    resp.result = rb_channel_->BeginWait(loop_.dispatcher());
    if (resp.result != ZX_OK) {
      zxlogf(ERROR, "Could not begin wait %s\n", __PRETTY_FUNCTION__);
      // We let rb_channel_remote go out of scope to trigger channel deactivation via closing.
    }
  }
finished:
  if (resp.result == ZX_OK) {
    resp.external_delay_nsec = external_delay_nsec_;
    return channel->Write(&resp, sizeof(resp), std::move(rb_channel_remote));
  } else {
    return channel->Write(&resp, sizeof(resp));
  }
}

void SimpleAudioStream::RingBufferSignalled(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                                            zx_status_t status, const zx_packet_signal_t* signal,
                                            Channel* channel) {
  if (status != ZX_OK) {
    if (status != ZX_ERR_CANCELED) {  // Cancel is expected.
      zxlogf(ERROR, "%s handler error %d\n", __PRETTY_FUNCTION__, status);
    }
    return;
  }
  bool readable_asserted = signal->observed & ZX_CHANNEL_READABLE;
  bool peer_closed_asserted = signal->observed & ZX_CHANNEL_PEER_CLOSED;
  if (readable_asserted) {
    {
      zx_status_t status = ProcessRingBufferChannel(channel);
      if (status != ZX_OK) {
        zxlogf(ERROR, "%s processing ring buffer channel error %d\n", __PRETTY_FUNCTION__, status);
        return;
      }
    }
    if (!peer_closed_asserted) {
      wait->Begin(dispatcher);
    }
  }
  if (peer_closed_asserted) {
    fbl::AutoLock channel_lock(&channel_lock_);
    DeactivateRingBufferChannel(rb_channel_.get());
    ZX_DEBUG_ASSERT(rb_channel_ == nullptr);
  }
}

zx_status_t SimpleAudioStream::OnGetGain(StreamChannel* channel,
                                         const audio_proto::GetGainReq& req) const {
  audio_proto::GetGainResp resp = cur_gain_state_;
  resp.hdr = req.hdr;
  return channel->Write(&resp, sizeof(resp));
}

zx_status_t SimpleAudioStream::OnSetGain(StreamChannel* channel,
                                         const audio_proto::SetGainReq& req) {
  audio_proto::SetGainResp resp;
  resp.hdr = req.hdr;

  // Sanity check the request before passing it along
  if ((req.flags & AUDIO_SGF_MUTE_VALID) && (req.flags & AUDIO_SGF_MUTE) &&
      !cur_gain_state_.can_mute) {
    resp.result = ZX_ERR_NOT_SUPPORTED;
    goto finished;
  }

  if ((req.flags & AUDIO_SGF_AGC_VALID) && (req.flags & AUDIO_SGF_AGC) &&
      !cur_gain_state_.can_agc) {
    resp.result = ZX_ERR_NOT_SUPPORTED;
    goto finished;
  }

  if ((req.flags & AUDIO_SGF_GAIN_VALID) &&
      ((req.gain < cur_gain_state_.min_gain) || (req.gain > cur_gain_state_.max_gain))) {
    resp.result = ZX_ERR_INVALID_ARGS;
    goto finished;
  }

  resp.result = SetGain(req);

finished:
  resp.cur_mute = cur_gain_state_.cur_mute;
  resp.cur_agc = cur_gain_state_.cur_agc;
  resp.cur_gain = cur_gain_state_.cur_gain;
  return (req.hdr.cmd & AUDIO_FLAG_NO_ACK) ? ZX_OK : channel->Write(&resp, sizeof(resp));
}

// Called when receiving a AUDIO_STREAM_CMD_PLUG_DETECT message from a client.
zx_status_t SimpleAudioStream::OnPlugDetect(StreamChannel* channel,
                                            const audio_proto::PlugDetectReq& req) {
  // It should never be the case that both bits are set -- but if so, DISABLE notifications.
  bool disable = ((req.flags & AUDIO_PDF_DISABLE_NOTIFICATIONS) != 0);
  bool enable = ((req.flags & AUDIO_PDF_ENABLE_NOTIFICATIONS) != 0) && !disable;

  {
    fbl::AutoLock channel_lock(&channel_lock_);
    if (enable) {
      if (plug_notify_channels_.is_empty()) {
        EnableAsyncNotification(true);
      }
      if (!channel->in_plug_notify_list()) {
        plug_notify_channels_.push_back(fbl::RefPtr(channel));
      }
    } else if (disable) {
      if (channel->in_plug_notify_list()) {
        plug_notify_channels_.erase(*channel);
      }
      if (plug_notify_channels_.is_empty()) {
        EnableAsyncNotification(false);
      }
    }
  }

  if (req.hdr.cmd & AUDIO_FLAG_NO_ACK) {
    return ZX_OK;
  }

  audio_proto::PlugDetectResp resp = {};
  resp.hdr = req.hdr;
  resp.flags = pd_flags_;
  resp.plug_state_time = plug_time_;

  return channel->Write(&resp, sizeof(resp));
}

zx_status_t SimpleAudioStream::OnGetUniqueId(StreamChannel* channel,
                                             const audio_proto::GetUniqueIdReq& req) const {
  audio_proto::GetUniqueIdResp resp;

  resp.hdr = req.hdr;
  resp.unique_id = unique_id_;

  return channel->Write(&resp, sizeof(resp));
}

zx_status_t SimpleAudioStream::OnGetString(StreamChannel* channel,
                                           const audio_proto::GetStringReq& req) const {
  audio_proto::GetStringResp resp;

  resp.hdr = req.hdr;
  resp.id = req.id;

  const char* str;
  switch (req.id) {
    case AUDIO_STREAM_STR_ID_MANUFACTURER:
      str = mfr_name_;
      break;
    case AUDIO_STREAM_STR_ID_PRODUCT:
      str = prod_name_;
      break;
    default:
      str = nullptr;
      break;
  }

  if (str == nullptr) {
    resp.result = ZX_ERR_NOT_FOUND;
    resp.strlen = 0;
  } else {
    int res = snprintf(reinterpret_cast<char*>(resp.str), sizeof(resp.str), "%s", str);
    ZX_DEBUG_ASSERT(res >= 0);
    resp.result = ZX_OK;
    resp.strlen = fbl::min<uint32_t>(res, sizeof(resp.str) - 1);
  }

  return channel->Write(&resp, sizeof(resp));
}

zx_status_t SimpleAudioStream::OnGetFifoDepth(Channel* channel,
                                              const audio_proto::RingBufGetFifoDepthReq& req) {
  audio_proto::RingBufGetFifoDepthResp resp = {};

  resp.hdr = req.hdr;
  resp.result = ZX_OK;
  resp.fifo_depth = fifo_depth_;

  return channel->Write(&resp, sizeof(resp));
}

zx_status_t SimpleAudioStream::OnGetBuffer(Channel* channel,
                                           const audio_proto::RingBufGetBufferReq& req) {
  audio_proto::RingBufGetBufferResp resp = {};
  resp.hdr = req.hdr;

  if (rb_started_) {
    resp.result = ZX_ERR_BAD_STATE;
  } else {
    zx::vmo buffer;
    resp.result = GetBuffer(req, &resp.num_ring_buffer_frames, &buffer);
    if (resp.result == ZX_OK) {
      zx_status_t res = channel->Write(&resp, sizeof(resp), std::move(buffer));
      if (res == ZX_OK) {
        expected_notifications_per_ring_.store(req.notifications_per_ring);
        rb_fetched_ = true;
      }
      return res;
    } else {
      expected_notifications_per_ring_.store(0);
    }
  }

  ZX_DEBUG_ASSERT(resp.result != ZX_OK);
  return channel->Write(&resp, sizeof(resp));
}

zx_status_t SimpleAudioStream::OnStart(Channel* channel, const audio_proto::RingBufStartReq& req) {
  audio_proto::RingBufStartResp resp = {};
  resp.hdr = req.hdr;

  if (rb_started_ || !rb_fetched_) {
    resp.result = ZX_ERR_BAD_STATE;
  } else {
    resp.result = Start(&resp.start_time);
    if (resp.result == ZX_OK) {
      rb_started_ = true;
    }
  }

  return channel->Write(&resp, sizeof(resp));
}

zx_status_t SimpleAudioStream::OnStop(Channel* channel, const audio_proto::RingBufStopReq& req) {
  audio_proto::RingBufStopResp resp = {};
  resp.hdr = req.hdr;

  if (!rb_started_) {
    resp.result = ZX_ERR_BAD_STATE;
  } else {
    resp.result = Stop();
    if (resp.result == ZX_OK) {
      rb_started_ = false;
    }
  }

  return channel->Write(&resp, sizeof(resp));
}

}  // namespace audio
