// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/cpp/wire/server.h>
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
#include <intel-hda/codec-utils/dai-base.h>
#include <intel-hda/utils/intel-hda-proto.h>

#include "debug-logging.h"

namespace audio::intel_hda::codecs {

zx_protocol_device_t IntelHDADaiBase::DAI_DEVICE_THUNKS = []() {
  zx_protocol_device_t sdt = {};
  sdt.version = DEVICE_OPS_VERSION;
  sdt.message = [](void* ctx, fidl_incoming_msg_t* msg, fidl_txn_t* txn) {
    IntelHDADaiBase* thiz = static_cast<IntelHDADaiBase*>(ctx);
    DdkTransaction transaction(txn);
    fidl::WireDispatch<fuchsia_hardware_audio::DaiConnector>(
        thiz, fidl::IncomingHeaderAndMessage::FromEncodedCMessage(msg), &transaction);
    return transaction.Status();
  };
  return sdt;
}();

IntelHDADaiBase::IntelHDADaiBase(uint32_t id, bool is_input)
    : IntelHDAStreamBase(id, is_input), loop_(&kAsyncLoopConfigNeverAttachToThread) {
  loop_.StartThread("intel-hda-dai-loop");
}

void IntelHDADaiBase::Connect(ConnectRequestView request, ConnectCompleter::Sync& completer) {
  fbl::AutoLock lock(obj_lock());

  // Do not allow any new connections if we are in the process of shutting down
  if (!is_active()) {
    completer.Close(ZX_ERR_BAD_STATE);
    return;
  }

  // For now, block new connections if we currently have no privileged
  // connection, but there is a SetFormat request in flight to the codec
  // driver.  We are trying to avoid the following sequence...
  //
  // 1) A privileged connection starts a set format.
  // 2) After we ask the controller to set the format, our privileged channel
  //    is closed.
  // 3) A new user connects.
  // 4) The response to the first client's request arrives and gets sent
  //    to the second client.
  // 5) Confusion ensues.
  //
  // Denying new connections while the old request is in flight avoids this,
  // but is generally a terrible solution.  What we should really do is tag
  // the requests to the codec driver with a unique ID which we can use to
  // filter responses.  One option might be to split the transaction ID so
  // that a portion of the TID is used for stream routing, while another
  // portion is used for requests like this.
  bool privileged = (dai_channel_ == nullptr);
  if (privileged && IsFormatChangeInProgress()) {
    completer.Close(ZX_ERR_SHOULD_WAIT);
    return;
  }

  // Attempt to allocate a new driver channel and bind it to us.  If we don't
  // already have a dai_channel_, flag this channel is the privileged
  // connection (The connection which is allowed to do things like change
  // formats).
  fbl::RefPtr<DaiChannel> dai_channel = DaiChannel::Create(this);
  if (dai_channel == nullptr) {
    completer.Close(ZX_ERR_NO_MEMORY);
    return;
  }
  dai_channels_.push_back(dai_channel);

  fidl::OnUnboundFn<fidl::WireServer<fuchsia_hardware_audio::Dai>> on_unbound =
      [this, dai_channel](fidl::WireServer<fuchsia_hardware_audio::Dai>*, fidl::UnbindInfo,
                          fidl::ServerEnd<fuchsia_hardware_audio::Dai>) {
        fbl::AutoLock channel_lock(this->obj_lock());
        this->ProcessClientDeactivateLocked(dai_channel.get());
      };

  fidl::BindServer<fidl::WireServer<fuchsia_hardware_audio::Dai>>(
      loop_.dispatcher(), std::move(request->dai_protocol), dai_channel.get(),
      std::move(on_unbound));

  if (privileged) {
    dai_channel_ = dai_channel;
  }
}

void IntelHDADaiBase::GetDaiFormats(DaiChannel::GetDaiFormatsCompleter::Sync& completer) {
  fidl::Arena arena;
  fidl::VectorView<fuchsia_hardware_audio::wire::DaiSupportedFormats> all_formats(arena, 1);
  fuchsia_hardware_audio::wire::DaiSupportedFormats formats;
  uint32_t number_of_channels[] = {dai_format_.number_of_channels};
  fuchsia_hardware_audio::wire::DaiSampleFormat sample_formats[] = {dai_format_.sample_format};
  fuchsia_hardware_audio::wire::DaiFrameFormat frame_formats[] = {dai_format_.frame_format};
  uint32_t frame_rates[] = {dai_format_.frame_rate};
  uint8_t bits_per_slot[] = {dai_format_.bits_per_slot};
  uint8_t bits_per_sample[] = {dai_format_.bits_per_sample};
  formats.number_of_channels =
      fidl::VectorView<uint32_t>::FromExternal(number_of_channels, std::size(number_of_channels));
  formats.sample_formats =
      fidl::VectorView<fuchsia_hardware_audio::wire::DaiSampleFormat>::FromExternal(
          sample_formats, std::size(sample_formats));
  formats.frame_formats =
      fidl::VectorView<fuchsia_hardware_audio::wire::DaiFrameFormat>::FromExternal(
          frame_formats, std::size(frame_formats));
  formats.frame_rates =
      fidl::VectorView<uint32_t>::FromExternal(frame_rates, std::size(frame_rates));
  formats.bits_per_slot =
      fidl::VectorView<uint8_t>::FromExternal(bits_per_slot, std::size(bits_per_slot));
  formats.bits_per_sample =
      fidl::VectorView<uint8_t>::FromExternal(bits_per_sample, std::size(bits_per_sample));

  all_formats[0] = formats;

  fuchsia_hardware_audio::wire::DaiGetDaiFormatsResponse response;
  response.dai_formats = std::move(all_formats);
  completer.Reply(::fit::ok(&response));
}

void IntelHDADaiBase::Reset(DaiChannel::ResetCompleter::Sync& completer) {
  OnResetLocked();
  completer.Reply();
}

void IntelHDADaiBase::GetRingBufferFormats(
    DaiChannel::GetRingBufferFormatsCompleter::Sync& completer) {
  if (supported_formats_.size() > std::numeric_limits<uint16_t>::max()) {
    LOG("Too many formats (%zu) to send during GetRingBufferFormats request!\n",
        supported_formats_.size());
    return;
  }

  // Build formats compatible with FIDL from a vector of audio_stream_format_range_t.
  // Needs to be alive until the reply is sent.
  struct FidlCompatibleFormats {
    fbl::Vector<uint8_t> number_of_channels;
    fbl::Vector<fuchsia_hardware_audio::wire::SampleFormat> sample_formats;
    fbl::Vector<uint32_t> frame_rates;
    fbl::Vector<uint8_t> valid_bits_per_sample;
    fbl::Vector<uint8_t> bytes_per_sample;
  };
  fbl::Vector<FidlCompatibleFormats> fidl_compatible_formats;
  for (auto& i : supported_formats_) {
    auto formats = audio::utils::GetAllFormats(i.sample_formats);
    ZX_ASSERT(formats.size() >= 1);
    for (auto& j : formats) {
      fbl::Vector<uint32_t> rates;
      // Ignore flags if min and max are equal.
      if (i.min_frames_per_second == i.max_frames_per_second) {
        rates.push_back(i.min_frames_per_second);
      } else {
        ZX_DEBUG_ASSERT(!(i.flags & ASF_RANGE_FLAG_FPS_CONTINUOUS));
        audio::utils::FrameRateEnumerator enumerator(i);
        for (uint32_t rate : enumerator) {
          rates.push_back(rate);
        }
      }

      fbl::Vector<uint8_t> number_of_channels;
      for (uint8_t j = i.min_channels; j <= i.max_channels; ++j) {
        number_of_channels.push_back(j);
      }

      fidl_compatible_formats.push_back({
          std::move(number_of_channels),
          {j.format},
          std::move(rates),
          {j.valid_bits_per_sample},
          {j.bytes_per_sample},
      });
    }
  }

  fidl::Arena allocator;
  fidl::VectorView<fuchsia_hardware_audio::wire::SupportedFormats> fidl_formats(
      allocator, fidl_compatible_formats.size());
  for (size_t i = 0; i < fidl_compatible_formats.size(); ++i) {
    // Get FIDL PcmSupportedFormats from FIDL compatible vectors.
    // Needs to be alive until the reply is sent.
    FidlCompatibleFormats& src = fidl_compatible_formats[i];
    fuchsia_hardware_audio::wire::PcmSupportedFormats formats;

    fidl::VectorView<fuchsia_hardware_audio::wire::ChannelSet> channel_sets(
        allocator, src.number_of_channels.size());

    for (uint8_t j = 0; j < src.number_of_channels.size(); ++j) {
      fidl::VectorView<fuchsia_hardware_audio::wire::ChannelAttributes> all_attributes(
          allocator, src.number_of_channels[j]);
      channel_sets[j].Allocate(allocator);
      channel_sets[j].set_attributes(allocator, std::move(all_attributes));
    }
    formats.Allocate(allocator);
    formats.set_channel_sets(allocator, std::move(channel_sets));
    formats.set_sample_formats(
        allocator, ::fidl::VectorView<fuchsia_hardware_audio::wire::SampleFormat>::FromExternal(
                       src.sample_formats.data(), src.sample_formats.size()));
    formats.set_frame_rates(allocator, ::fidl::VectorView<uint32_t>::FromExternal(
                                           src.frame_rates.data(), src.frame_rates.size()));
    formats.set_bytes_per_sample(
        allocator, ::fidl::VectorView<uint8_t>::FromExternal(src.bytes_per_sample.data(),
                                                             src.bytes_per_sample.size()));
    formats.set_valid_bits_per_sample(
        allocator, ::fidl::VectorView<uint8_t>::FromExternal(src.valid_bits_per_sample.data(),
                                                             src.valid_bits_per_sample.size()));
    fidl_formats[i].Allocate(allocator);
    fidl_formats[i].set_pcm_supported_formats(allocator, std::move(formats));
  }

  fuchsia_hardware_audio::wire::DaiGetRingBufferFormatsResponse response;
  response.ring_buffer_formats = std::move(fidl_formats);
  completer.Reply(::fit::ok(&response));
}

void IntelHDADaiBase::CreateRingBuffer(
    DaiChannel* channel, fuchsia_hardware_audio::wire::DaiFormat dai_format,
    fuchsia_hardware_audio::wire::Format ring_buffer_format,
    ::fidl::ServerEnd<fuchsia_hardware_audio::RingBuffer> ring_buffer,
    DaiChannel::CreateRingBufferCompleter::Sync& completer) {
  ZX_DEBUG_ASSERT(channel != nullptr);
  zx_status_t res;

  // Only the privileged DAI channel is allowed to change the format.
  if (channel != dai_channel_.get()) {
    LOG("Unprivileged channel cannot set the format");
    completer.Close(ZX_ERR_INVALID_ARGS);
    return;
  }

  auto format_pcm = ring_buffer_format.pcm_format();
  audio_sample_format_t sample_format = audio::utils::GetSampleFormat(
      format_pcm.valid_bits_per_sample, 8 * format_pcm.bytes_per_sample);

  bool found_one = false;
  // Check the format for compatibility
  for (const auto& fmt : supported_formats_) {
    if (audio::utils::FormatIsCompatible(format_pcm.frame_rate,
                                         static_cast<uint16_t>(format_pcm.number_of_channels),
                                         sample_format, fmt)) {
      found_one = true;
      break;
    }
  }

  if (!found_one) {
    LOG("Could not find a suitable format in %s", __PRETTY_FUNCTION__);
    completer.Close(ZX_ERR_INVALID_ARGS);
    return;
  }

  res = IntelHDAStreamBase::CreateRingBufferLocked(std::move(ring_buffer_format),
                                                   std::move(ring_buffer));
  if (res != ZX_OK) {
    completer.Close(res);
  }
}

void IntelHDADaiBase::GetProperties(
    DaiChannel* channel,
    fidl::WireServer<fuchsia_hardware_audio::Dai>::GetPropertiesCompleter::Sync& completer) {
  fidl::Arena allocator;
  fuchsia_hardware_audio::wire::DaiProperties response(allocator);

  response.set_is_input(is_input());

  audio_proto::GetStringResp resp_product = {};
  audio_proto::GetStringReq req = {};
  req.id = AUDIO_STREAM_STR_ID_PRODUCT;
  OnGetStringLocked(req, &resp_product);
  auto product = fidl::StringView::FromExternal(reinterpret_cast<char*>(resp_product.str),
                                                resp_product.strlen);
  response.set_product_name(fidl::ObjectView<fidl::StringView>::FromExternal(&product));

  req.id = AUDIO_STREAM_STR_ID_MANUFACTURER;
  audio_proto::GetStringResp resp_manufacturer = {};
  OnGetStringLocked(req, &resp_manufacturer);
  auto manufacturer = fidl::StringView::FromExternal(reinterpret_cast<char*>(resp_manufacturer.str),
                                                     resp_manufacturer.strlen);
  response.set_manufacturer(fidl::ObjectView<fidl::StringView>::FromExternal(&manufacturer));

  completer.Reply(std::move(response));
}

void IntelHDADaiBase::OnGetStringLocked(const audio_proto::GetStringReq& req,
                                        audio_proto::GetStringResp* out_resp) {
  ZX_DEBUG_ASSERT(out_resp);

  switch (req.id) {
    case AUDIO_STREAM_STR_ID_MANUFACTURER:
    case AUDIO_STREAM_STR_ID_PRODUCT: {
      int res =
          snprintf(reinterpret_cast<char*>(out_resp->str), sizeof(out_resp->str), "<unknown>");
      ZX_DEBUG_ASSERT(res >= 0);  // there should be no way for snprintf to fail here.
      out_resp->strlen = std::min<uint32_t>(res, sizeof(out_resp->str) - 1);
      out_resp->result = ZX_OK;
      break;
    }

    default:
      out_resp->strlen = 0;
      out_resp->result = ZX_ERR_NOT_FOUND;
      break;
  }
}

void IntelHDADaiBase::ProcessClientDeactivateLocked(DaiChannel* channel) {
  ZX_DEBUG_ASSERT(channel != nullptr);

  // Let our subclass know that this channel is going away.
  OnChannelDeactivateLocked(*channel);

  // Is this the privileged DAI channel?
  if (dai_channel_.get() == channel) {
    dai_channel_.reset();
  }

  dai_channels_.erase(*channel);
}

void IntelHDADaiBase::OnChannelDeactivateLocked(const DaiChannel& channel) {}

void IntelHDADaiBase::OnDeactivate() { loop_.Shutdown(); }

void IntelHDADaiBase::RemoveDeviceLocked() { device_async_remove(dai_device_); }

zx_status_t IntelHDADaiBase::ProcessSetStreamFmtLocked(
    const ihda_proto::SetStreamFmtResp& codec_resp) {
  zx_status_t res = ZX_OK;

  // Are we shutting down?
  if (!is_active())
    return ZX_ERR_BAD_STATE;

  // If we don't have a set format operation in flight, or the DAI channel
  // has been closed, this set format operation has been canceled.  Do not
  // return an error up the stack; we don't want to close the connection to
  // our codec device.
  if ((!IsFormatChangeInProgress()) || (dai_channel_ == nullptr)) {
    goto finished;
  }

  // Let the implementation send the commands required to finish changing the
  // stream format.
  res = FinishChangeStreamFormatLocked(encoded_fmt());
  if (res != ZX_OK) {
    DEBUG_LOG("Failed to finish set format (enc fmt 0x%04hx res %d)\n", encoded_fmt(), res);
    goto finished;
  }

finished:
  // Something went fatally wrong when trying to send the result back to the
  // caller.  Close the DAI channel.
  if (dai_channel_ != nullptr) {
    OnChannelDeactivateLocked(*dai_channel_);
    dai_channel_ = nullptr;
  }

  // Set format operation is finished. There is no reply sent in CreateRingBuffer.
  SetFormatChangeInProgress(false);

  return ZX_OK;
}

zx_status_t IntelHDADaiBase::PublishDeviceLocked() {
  if (!is_active())
    return ZX_ERR_BAD_STATE;

  // Initialize our device and fill out the protocol hooks
  device_add_args_t args = {};
  args.version = DEVICE_ADD_ARGS_VERSION;
  args.name = dev_name();
  args.ctx = this;
  args.ops = &DAI_DEVICE_THUNKS;
  args.proto_id = ZX_PROTOCOL_DAI;

  // Publish the device.
  zx_status_t res = device_add(parent_codec()->codec_device(), &args, &dai_device_);
  if (res != ZX_OK) {
    LOG("Failed to add DAI device for \"%s\" (res %d)\n", dev_name(), res);
    return res;
  }

  return IntelHDAStreamBase::PublishDeviceLocked();
}

}  // namespace audio::intel_hda::codecs
