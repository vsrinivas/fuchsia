// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fit/defer.h>
#include <lib/zx/clock.h>
#include <string.h>

#include <algorithm>
#include <limits>
#include <utility>

#include <audio-proto-utils/format-utils.h>
#include <audio-proto/audio-proto.h>
#include <fbl/algorithm.h>
#include <intel-hda/codec-utils/codec-driver-base.h>
#include <intel-hda/codec-utils/stream-base.h>
#include <intel-hda/utils/intel-hda-proto.h>

#include "debug-logging.h"

namespace audio {
namespace intel_hda {
namespace codecs {

IntelHDAStreamBase::IntelHDAStreamBase(uint32_t id, bool is_input) : id_(id), is_input_(is_input) {
  snprintf(dev_name_, sizeof(dev_name_), "%s-stream-%03u", is_input_ ? "input" : "output", id_);
}

IntelHDAStreamBase::~IntelHDAStreamBase() {}

void IntelHDAStreamBase::PrintDebugPrefix() const { printf("[%s] ", dev_name_); }

void IntelHDAStreamBase::SetPersistentUniqueId(const audio_stream_unique_id_t& id) {
  fbl::AutoLock obj_lock(&obj_lock_);
  SetPersistentUniqueIdLocked(id);
}

void IntelHDAStreamBase::SetPersistentUniqueIdLocked(const audio_stream_unique_id_t& id) {
  persistent_unique_id_ = id;
}

zx_status_t IntelHDAStreamBase::Activate(fbl::RefPtr<IntelHDACodecDriverBase>&& parent_codec,
                                         const fbl::RefPtr<Channel>& codec_channel) {
  ZX_DEBUG_ASSERT(codec_channel != nullptr);

  fbl::AutoLock obj_lock(&obj_lock_);
  if (is_active() || (codec_channel_ != nullptr))
    return ZX_ERR_BAD_STATE;

  ZX_DEBUG_ASSERT(parent_codec_ == nullptr);

  // Remember our parent codec and our codec channel.  If something goes wrong
  // during activation, make sure we let go of these references.
  //
  // Note; the cleanup lambda needs to have thread analysis turned off because
  // the compiler is not quite smart enough to figure out that the obj_lock
  // AutoLock will destruct (and release the lock) after the AutoCall runs,
  // and that the AutoCall will never leave this scope.
  auto cleanup = fit::defer([this]() __TA_NO_THREAD_SAFETY_ANALYSIS {
    parent_codec_.reset();
    codec_channel_.reset();
  });
  parent_codec_ = std::move(parent_codec);
  codec_channel_ = codec_channel;

  // Allow our implementation to send its initial stream setup commands to the
  // codec.
  zx_status_t res = OnActivateLocked();
  if (res != ZX_OK)
    return res;

  // Request a DMA context
  ihda_proto::RequestStreamReq req;

  req.hdr.transaction_id = id();
  req.hdr.cmd = IHDA_CODEC_REQUEST_STREAM;
  req.input = is_input();

  res = codec_channel_->Write(&req, sizeof(req));
  if (res != ZX_OK)
    return res;

  cleanup.cancel();
  return ZX_OK;
}

void IntelHDAStreamBase::Deactivate() {
  {
    fbl::AutoLock obj_lock(&obj_lock_);
    DEBUG_LOG("Deactivating stream\n");

    // Let go of any unsolicited stream tags we may be holding.
    if (unsol_tag_count_) {
      ZX_DEBUG_ASSERT(parent_codec_ != nullptr);
      parent_codec_->ReleaseAllUnsolTags(*this);
      unsol_tag_count_ = 0;
    }

    // Clear out our parent_codec_ pointer.  This will mark us as being
    // inactive and prevent any new connections from being made.
    parent_codec_.reset();

    // We should already have been removed from our codec's active stream list
    // at this point.
    ZX_DEBUG_ASSERT(!this->InContainer());
  }

  OnDeactivate();

  {
    fbl::AutoLock obj_lock(&obj_lock_);

    // Allow our implementation to send the commands needed to tear down the
    // widgets which make up this stream.
    OnDeactivateLocked();

    // If we have been given a DMA stream by the IHDA controller, attempt to
    // return it now.
    if ((dma_stream_id_ != IHDA_INVALID_STREAM_ID) && (codec_channel_ != nullptr)) {
      ihda_proto::ReleaseStreamReq req;

      req.hdr.transaction_id = id();
      req.hdr.cmd = IHDA_CODEC_RELEASE_STREAM_NOACK, req.stream_id = dma_stream_id_;

      codec_channel_->Write(&req, sizeof(req));

      dma_stream_id_ = IHDA_INVALID_STREAM_ID;
      dma_stream_tag_ = IHDA_INVALID_STREAM_TAG;
    }

    // Let go of our reference to the codec device channel.
    codec_channel_ = nullptr;

    // If we had published a device node, remove it now.
    if (parent_device_ != nullptr) {
      RemoveDeviceLocked();
      parent_device_ = nullptr;
    }
  }

  DEBUG_LOG("Deactivate complete\n");
}

zx_status_t IntelHDAStreamBase::ProcessResponse(const CodecResponse& resp) {
  fbl::AutoLock obj_lock(&obj_lock_);

  if (!is_active()) {
    DEBUG_LOG("Ignoring codec response (0x%08x, 0x%08x) for inactive stream id %u\n", resp.data,
              resp.data_ex, id());
    return ZX_OK;
  }

  return resp.unsolicited() ? OnUnsolicitedResponseLocked(resp) : OnSolicitedResponseLocked(resp);
}

zx_status_t IntelHDAStreamBase::ProcessRequestStream(const ihda_proto::RequestStreamResp& resp) {
  fbl::AutoLock obj_lock(&obj_lock_);
  zx_status_t res;

  if (!is_active())
    return ZX_ERR_BAD_STATE;

  res = SetDMAStreamLocked(resp.stream_id, resp.stream_tag);
  if (res != ZX_OK) {
    // TODO(johngro) : If we failed to set the DMA info because this stream
    // is in the process of shutting down, we really should return the
    // stream to the controller.
    //
    // Right now, we are going to return an error which will cause the lower
    // level infrastructure to close the codec device channel.  This will
    // prevent a leak (the core controller driver will re-claim the stream),
    // but it will also ruin all of the other streams in this codec are
    // going to end up being destroyed.  For simple codec driver who never
    // change stream topology, this is probably fine, but for more
    // complicated ones it probably is not.
    return res;
  }

  return OnDMAAssignedLocked();
}

// TODO(johngro) : Refactor this; this sample_format of parameters is 95% the same
// between both the codec and stream base classes.
zx_status_t IntelHDAStreamBase::SendCodecCommandLocked(uint16_t nid, CodecVerb verb, Ack do_ack) {
  if (codec_channel_ == nullptr)
    return ZX_ERR_BAD_STATE;

  ihda_codec_send_corb_cmd_req_t cmd;

  cmd.hdr.cmd = (do_ack == Ack::NO) ? IHDA_CODEC_SEND_CORB_CMD_NOACK : IHDA_CODEC_SEND_CORB_CMD;
  cmd.hdr.transaction_id = id();
  cmd.nid = nid;
  cmd.verb = verb.val;

  return codec_channel_->Write(&cmd, sizeof(cmd));
}

zx_status_t IntelHDAStreamBase::SendSetStreamFmtLocked(uint16_t encoded_fmt,
                                                       zx::handle ring_buffer_channel) {
  if (codec_channel_ == nullptr)
    return ZX_ERR_BAD_STATE;

  // Set the format of DMA stream.  This will stop any stream in progress and
  // close any connection to its clients.  At this point, all of our checks
  // are done and we expect success.  If anything goes wrong, consider it to
  // be a fatal internal error and close the connection to our client by
  // returning an error.
  ihda_proto::SetStreamFmtReq req;
  req.hdr.cmd = IHDA_CODEC_SET_STREAM_FORMAT;
  req.hdr.transaction_id = id();
  req.stream_id = dma_stream_id_;
  req.format = encoded_fmt;
  zx_status_t res = codec_channel_->Write(&req, sizeof(req), std::move(ring_buffer_channel));
  if (res != ZX_OK) {
    return res;
  }
  encoded_fmt_ = encoded_fmt;
  return ZX_OK;
}

zx_status_t IntelHDAStreamBase::CreateRingBufferLocked(
    fuchsia_hardware_audio::wire::Format format,
    fidl::ServerEnd<fuchsia_hardware_audio::RingBuffer> ring_buffer) {
  uint16_t encoded_fmt;
  // If we don't have a DMA stream assigned to us, or there is already a set
  // format operation in flight, we cannot proceed.
  if (dma_stream_id_ == IHDA_INVALID_STREAM_ID || IsFormatChangeInProgress()) {
    return ZX_ERR_BAD_STATE;
  }

  auto format_pcm = format.pcm_format();
  audio_sample_format_t sample_format = audio::utils::GetSampleFormat(
      format_pcm.valid_bits_per_sample, 8 * format_pcm.bytes_per_sample);

  if (sample_format == 0) {
    LOG("Unsupported format: Invalid bits per sample (%u/%u)\n", format_pcm.valid_bits_per_sample,
        8 * format_pcm.bytes_per_sample);
    return ZX_ERR_INVALID_ARGS;
  }

  if (format_pcm.sample_format == fuchsia_hardware_audio::wire::SampleFormat::kPcmFloat) {
    sample_format = AUDIO_SAMPLE_FORMAT_32BIT_FLOAT;
    if (format_pcm.valid_bits_per_sample != 32 || format_pcm.bytes_per_sample != 4) {
      LOG("Unsupported format: Not 32 per sample/channel for float\n");
      return ZX_ERR_INVALID_ARGS;
    }
  }

  if (format_pcm.sample_format == fuchsia_hardware_audio::wire::SampleFormat::kPcmUnsigned) {
    sample_format |= AUDIO_SAMPLE_FORMAT_FLAG_UNSIGNED;
  }

  audio_proto::StreamSetFmtReq fmt = {};
  fmt.sample_format = sample_format;
  fmt.channels = format_pcm.number_of_channels;
  fmt.frames_per_second = format_pcm.frame_rate;

  // The upper level stream told us that they support this format, we had
  // better be able to encode it into an IHDA format specifier.
  zx_status_t res = EncodeStreamFormat(fmt, &encoded_fmt);
  if (res != ZX_OK) {
    DEBUG_LOG("Failed to encode stream format %u:%hu:%s (res %d)\n", fmt.frames_per_second,
              fmt.channels, audio_proto::SampleFormatToString(fmt.sample_format), res);
    return res;
  }

  // Let our implementation start the process of a format change.  This gives
  // it a chance to check the format for compatibility, and send commands to
  // quiesce the converters and amplifiers if it approves of the format.
  res = BeginChangeStreamFormatLocked(fmt);
  if (res != ZX_OK) {
    DEBUG_LOG("Stream impl rejected stream format %u:%hu:%s (res %d)\n", fmt.frames_per_second,
              fmt.channels, audio_proto::SampleFormatToString(fmt.sample_format), res);
    return res;
  }

  res = SendSetStreamFmtLocked(encoded_fmt, ring_buffer.TakeChannel());
  if (res != ZX_OK) {
    DEBUG_LOG("Failed to write set stream format %u:%hu:%s to codec channel (res %d)\n",
              fmt.frames_per_second, fmt.channels,
              audio_proto::SampleFormatToString(fmt.sample_format), res);
    return res;
  }

  // Success!  Record that the format change is in progress.
  SetFormatChangeInProgress(true);
  return ZX_OK;
}

zx_status_t IntelHDAStreamBase::SetDMAStreamLocked(uint16_t id, uint8_t tag) {
  if ((id == IHDA_INVALID_STREAM_ID) || (tag == IHDA_INVALID_STREAM_TAG))
    return ZX_ERR_INVALID_ARGS;

  ZX_DEBUG_ASSERT((dma_stream_id_ == IHDA_INVALID_STREAM_ID) ==
                  (dma_stream_tag_ == IHDA_INVALID_STREAM_TAG));

  if (dma_stream_id_ != IHDA_INVALID_STREAM_ID)
    return ZX_ERR_BAD_STATE;

  dma_stream_id_ = id;
  dma_stream_tag_ = tag;

  return ZX_OK;
}

zx_status_t IntelHDAStreamBase::AllocateUnsolTagLocked(uint8_t* out_tag) {
  if (!parent_codec_)
    return ZX_ERR_BAD_STATE;

  zx_status_t res = parent_codec_->AllocateUnsolTag(*this, out_tag);
  if (res == ZX_OK)
    unsol_tag_count_++;

  return res;
}

void IntelHDAStreamBase::ReleaseUnsolTagLocked(uint8_t tag) {
  ZX_DEBUG_ASSERT(unsol_tag_count_ > 0);
  ZX_DEBUG_ASSERT(parent_codec_ != nullptr);
  parent_codec_->ReleaseUnsolTag(*this, tag);
  unsol_tag_count_--;
}

// TODO(johngro) : Move this out to a utils library?
#define MAKE_RATE(_rate, _base, _mult, _div) \
  { .rate = _rate, .encoded = (_base << 14) | ((_mult - 1) << 11) | ((_div - 1) << 8) }
zx_status_t IntelHDAStreamBase::EncodeStreamFormat(const audio_proto::StreamSetFmtReq& fmt,
                                                   uint16_t* encoded_fmt_out) {
  ZX_DEBUG_ASSERT(encoded_fmt_out != nullptr);

  // See section 3.7.1
  // Start with the channel count.  Intel HDA DMA streams support between 1
  // and 16 channels.
  uint32_t channels = fmt.channels - 1;
  if ((fmt.channels < 1) || (fmt.channels > 16))
    return ZX_ERR_NOT_SUPPORTED;

  // Next determine the bit sample_format format
  uint32_t bits;
  switch (fmt.sample_format) {
    case AUDIO_SAMPLE_FORMAT_8BIT:
      bits = 0;
      break;
    case AUDIO_SAMPLE_FORMAT_16BIT:
      bits = 1;
      break;
    case AUDIO_SAMPLE_FORMAT_20BIT_IN32:
      bits = 2;
      break;
    case AUDIO_SAMPLE_FORMAT_24BIT_IN32:
      bits = 3;
      break;
    case AUDIO_SAMPLE_FORMAT_32BIT:
    case AUDIO_SAMPLE_FORMAT_32BIT_FLOAT:
      bits = 4;
      break;
    default:
      return ZX_ERR_NOT_SUPPORTED;
  }

  // Finally, determine the base frame rate, as well as the multiplier and
  // divisor.
  static const struct {
    uint32_t rate;
    uint32_t encoded;
  } RATE_ENCODINGS[] = {
      // 48 KHz family
      MAKE_RATE(6000, 0, 1, 8),
      MAKE_RATE(8000, 0, 1, 6),
      MAKE_RATE(9600, 0, 1, 5),
      MAKE_RATE(16000, 0, 1, 3),
      MAKE_RATE(24000, 0, 1, 2),
      MAKE_RATE(32000, 0, 2, 3),
      MAKE_RATE(48000, 0, 1, 1),
      MAKE_RATE(96000, 0, 2, 1),
      MAKE_RATE(144000, 0, 3, 1),
      MAKE_RATE(192000, 0, 4, 1),
      // 44.1 KHz family
      MAKE_RATE(11025, 1, 1, 4),
      MAKE_RATE(22050, 1, 1, 2),
      MAKE_RATE(44100, 1, 1, 1),
      MAKE_RATE(88200, 1, 2, 1),
      MAKE_RATE(176400, 1, 4, 1),
  };

  for (const auto& r : RATE_ENCODINGS) {
    if (r.rate == fmt.frames_per_second) {
      *encoded_fmt_out = static_cast<uint16_t>(r.encoded | channels | (bits << 4));
      return ZX_OK;
    }
  }

  return ZX_ERR_NOT_SUPPORTED;
}
#undef MAKE_RATE

zx_status_t IntelHDAStreamBase::RecordPublishedDeviceLocked() {
  if (!is_active() || (parent_device_ != nullptr))
    return ZX_ERR_BAD_STATE;
  ZX_DEBUG_ASSERT(parent_codec_ != nullptr);

  // Record our parent.
  parent_device_ = parent_codec_->codec_device();

  return ZX_OK;
}

/////////////////////////////////////////////////////////////////////
//
// Default handlers
//
/////////////////////////////////////////////////////////////////////
zx_status_t IntelHDAStreamBase::OnActivateLocked() { return ZX_OK; }

void IntelHDAStreamBase::OnDeactivateLocked() {}

void IntelHDAStreamBase::OnDeactivate() {}

void IntelHDAStreamBase::RemoveDeviceLocked() {}

zx_status_t IntelHDAStreamBase::OnDMAAssignedLocked() { return PublishDeviceLocked(); }

zx_status_t IntelHDAStreamBase::OnSolicitedResponseLocked(const CodecResponse& resp) {
  return ZX_OK;
}

zx_status_t IntelHDAStreamBase::OnUnsolicitedResponseLocked(const CodecResponse& resp) {
  return ZX_OK;
}

zx_status_t IntelHDAStreamBase::BeginChangeStreamFormatLocked(
    const audio_proto::StreamSetFmtReq& fmt) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t IntelHDAStreamBase::FinishChangeStreamFormatLocked(uint16_t encoded_fmt) {
  return ZX_ERR_INTERNAL;
}
}  // namespace codecs
}  // namespace intel_hda
}  // namespace audio
