// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.audio/cpp/wire.h>
#include <lib/ddk/debug.h>
#include <lib/simple-codec/simple-codec-client.h>
#include <zircon/threads.h>

#include <fbl/auto_lock.h>

namespace audio {

SimpleCodecClient::~SimpleCodecClient() { Unbind(); }

SimpleCodecClient::SimpleCodecClient(SimpleCodecClient&& other) noexcept
    : loop_(&kAsyncLoopConfigNeverAttachToThread),
      created_with_dispatcher_(other.created_with_dispatcher_),
      dispatcher_(created_with_dispatcher_ ? other.dispatcher_ : loop_.dispatcher()) {
  other.Unbind();
  if (other.proto_client_.is_valid()) {
    SetProtocol(other.proto_client_);
  }
}

zx_status_t SimpleCodecClient::SetProtocol(ddk::CodecProtocolClient proto_client) {
  Unbind();

  proto_client_ = proto_client;
  if (!proto_client_.is_valid()) {
    return ZX_ERR_NO_RESOURCES;
  }
  zx::channel channel_remote;
  fidl::ClientEnd<fuchsia_hardware_audio::Codec> channel_local;
  auto status = zx::channel::create(0, &channel_local.channel(), &channel_remote);
  if (status != ZX_OK) {
    return status;
  }
  status = proto_client_.Connect(std::move(channel_remote));
  if (status != ZX_OK) {
    return status;
  }

  std::promise<void> codec_torn_down_promise;
  codec_torn_down_ = codec_torn_down_promise.get_future();
  codec_ = fidl::WireSharedClient(
      std::move(channel_local), dispatcher_,
      fidl::ObserveTeardown(
          [teardown = std::move(codec_torn_down_promise)]() mutable { teardown.set_value(); }));

  if (!created_with_dispatcher_ && !thread_started_) {
    status = loop_.StartThread("SimpleCodecClient thread");
    if (status != ZX_OK) {
      return status;
    }
    thread_started_ = true;
  }

  auto endpoints =
      fidl::CreateEndpoints<fuchsia_hardware_audio_signalprocessing::SignalProcessing>();
  if (endpoints.status_value() != ZX_OK) {
    return ZX_OK;  // We allow servers not supporting signal processing.
  }
  signal_processing_ =
      fidl::WireSharedClient<fuchsia_hardware_audio_signalprocessing::SignalProcessing>(
          std::move(endpoints->client), dispatcher_, fidl::ObserveTeardown([]() mutable {}));
  auto result = codec_.sync()->SignalProcessingConnect(std::move(endpoints->server));
  if (!result.ok()) {
    return result.status();
  }
  auto pes = signal_processing_.sync()->GetElements();
  if (!pes.ok() || pes->is_error()) {
    return ZX_OK;  // We allow servers not supporting signal processing.
  }

  GainState gain_state{
      .gain = 0.0f,
      .muted = false,
      .agc_enabled = false,
  };
  GainFormat gain_format{
      .min_gain = 0.0f,
      .max_gain = 0.0f,
      .gain_step = 0.0f,
      .can_mute = false,
      .can_agc = false,
  };
  for (auto& pe : pes->value()->processing_elements) {
    if (!pe.has_id()) {
      return ZX_ERR_INVALID_ARGS;
    }
    switch (pe.type()) {
      case fuchsia_hardware_audio_signalprocessing::wire::ElementType::kGain:
        if (!gain_pe_id_.has_value()) {  // Use the first PE with gain support.
          gain_pe_id_.emplace(pe.id());

          if (pe.has_type_specific() && pe.type_specific().is_gain()) {
            if (pe.type_specific().gain().has_type() &&
                pe.type_specific().gain().type() !=
                    fuchsia_hardware_audio_signalprocessing::wire::GainType::kDecibels) {
              return ZX_ERR_NOT_SUPPORTED;
            }
            if (pe.type_specific().gain().has_min_gain()) {
              gain_format.min_gain = pe.type_specific().gain().min_gain();
            }
            if (pe.type_specific().gain().has_max_gain()) {
              gain_format.max_gain = pe.type_specific().gain().max_gain();
            }
            if (pe.type_specific().gain().has_min_gain_step()) {
              gain_format.gain_step = pe.type_specific().gain().min_gain_step();
            }
          }
          // The first call from this client shouldn't block since this is a hanging-get
          // and the first calls always has new information (no information has previously been
          // provided).
          const auto response = signal_processing_.sync()->WatchElementState(gain_pe_id_.value());
          if (!response.ok()) {
            return response.status();
          }

          if (response->state.has_enabled() && response->state.enabled() &&
              response->state.has_type_specific() && response->state.type_specific().is_gain() &&
              response->state.type_specific().gain().has_gain()) {
            gain_state.gain = response->state.type_specific().gain().gain();
          }
        }
        break;
      case fuchsia_hardware_audio_signalprocessing::wire::ElementType::kMute:
        if (!mute_pe_id_.has_value()) {  // Use the first PE with mute support.
          mute_pe_id_.emplace(pe.id());
          // The first call from this client shouldn't block since this is a hanging-get
          // and the first calls always has new information (no information has previously been
          // provided).
          const auto response = signal_processing_.sync()->WatchElementState(mute_pe_id_.value());
          if (!response.ok()) {
            return response.status();
          }
          gain_format.can_mute = true;
          gain_state.muted = response->state.has_enabled() && response->state.enabled();
        }
        break;
      case fuchsia_hardware_audio_signalprocessing::wire::ElementType::kAutomaticGainControl:
        if (!agc_pe_id_.has_value()) {  // Use the first PE with agc support.
          agc_pe_id_.emplace(pe.id());
          // The first call from this client shouldn't block since this is a hanging-get
          // and the first calls always has new information (no information has previously been
          // provided).
          const auto response = signal_processing_.sync()->WatchElementState(agc_pe_id_.value());
          if (!response.ok()) {
            return response.status();
          }
          gain_format.can_agc = true;
          gain_state.agc_enabled = response->state.has_enabled() && response->state.enabled();
        }
        break;
      default:
        // TODO(fxbug.dev/110245): Handle default case.
        break;
    }
  }
  gain_format_ = zx::ok(gain_format);
  // Update the stored gain state, and start hanging gets to receive further gain state changes.
  UpdateGainAndStartHangingGet(gain_state.gain);
  UpdateMuteAndStartHangingGet(gain_state.muted);
  UpdateAgcAndStartHangingGet(gain_state.agc_enabled);

  return ZX_OK;
}

zx_status_t SimpleCodecClient::Reset() { return codec_.sync()->Reset().status(); }

zx_status_t SimpleCodecClient::Stop() { return codec_.sync()->Stop().status(); }

zx_status_t SimpleCodecClient::Start() { return codec_.sync()->Start().status(); }

zx::result<Info> SimpleCodecClient::GetInfo() {
  const auto result = codec_.sync()->GetInfo();
  if (!result.ok()) {
    return zx::error(result.status());
  }

  const fuchsia_hardware_audio::wire::CodecInfo& llcpp_info = result.value().info;

  Info info;
  info.unique_id = std::string(llcpp_info.unique_id.data(), llcpp_info.unique_id.size());
  info.manufacturer = std::string(llcpp_info.manufacturer.data(), llcpp_info.manufacturer.size());
  info.product_name = std::string(llcpp_info.product_name.data(), llcpp_info.product_name.size());
  return zx::ok(std::move(info));
}

zx::result<bool> SimpleCodecClient::IsBridgeable() {
  const auto result = codec_.sync()->IsBridgeable();
  if (result.ok()) {
    return zx::ok(result.value().supports_bridged_mode);
  }
  return zx::error(result.status());
}

zx_status_t SimpleCodecClient::SetBridgedMode(bool bridged) {
  return codec_->SetBridgedMode(bridged).status();
}

zx::result<DaiSupportedFormats> SimpleCodecClient::GetDaiFormats() {
  auto result = codec_.sync()->GetDaiFormats();
  if (!result.ok()) {
    return zx::error(result.status());
  }
  if (result->is_error()) {
    return zx::error(result->error_value());
  }

  ZX_ASSERT(result->value()->formats.count() == 1);
  const auto& llcpp_formats = result->value()->formats[0];

  DaiSupportedFormats formats;
  formats.number_of_channels = std::vector(llcpp_formats.number_of_channels.cbegin(),
                                           llcpp_formats.number_of_channels.cend());
  for (auto& sample_format : llcpp_formats.sample_formats) {
    formats.sample_formats.push_back(static_cast<SampleFormat>(sample_format));
  }
  for (auto& frame_format : llcpp_formats.frame_formats) {
    formats.frame_formats.push_back(static_cast<FrameFormat>(frame_format.frame_format_standard()));
  }
  formats.frame_rates =
      std::vector(llcpp_formats.frame_rates.cbegin(), llcpp_formats.frame_rates.cend());
  formats.bits_per_slot =
      std::vector(llcpp_formats.bits_per_slot.cbegin(), llcpp_formats.bits_per_slot.cend());
  formats.bits_per_sample =
      std::vector(llcpp_formats.bits_per_sample.cbegin(), llcpp_formats.bits_per_sample.cend());

  return zx::ok(formats);
}

zx::result<CodecFormatInfo> SimpleCodecClient::SetDaiFormat(DaiFormat format) {
  fidl::Arena allocator;

  fuchsia_hardware_audio::wire::DaiFormat format2;
  format2.number_of_channels = format.number_of_channels;
  format2.channels_to_use_bitmask = format.channels_to_use_bitmask;
  format2.sample_format =
      static_cast<fuchsia_hardware_audio::wire::DaiSampleFormat>(format.sample_format);
  const auto standard =
      static_cast<fuchsia_hardware_audio::wire::DaiFrameFormatStandard>(format.frame_format);
  format2.frame_format =
      fuchsia_hardware_audio::wire::DaiFrameFormat::WithFrameFormatStandard(standard);
  format2.frame_rate = format.frame_rate;
  format2.bits_per_slot = format.bits_per_slot;
  format2.bits_per_sample = format.bits_per_sample;

  const auto ret = codec_.sync()->SetDaiFormat(format2);
  if (!ret.ok()) {
    return zx::error(ret.status());
  }
  if (ret->is_error()) {
    return zx::error(ret->error_value());
  }
  CodecFormatInfo format_info = {};
  if (ret->value()->state.has_external_delay()) {
    format_info.set_external_delay(ret->value()->state.external_delay());
  }
  if (ret->value()->state.has_turn_on_delay()) {
    format_info.set_turn_on_delay(ret->value()->state.turn_on_delay());
  }
  if (ret->value()->state.has_turn_off_delay()) {
    format_info.set_turn_off_delay(ret->value()->state.turn_off_delay());
  }
  return zx::ok(std::move(format_info));
}

zx::result<GainFormat> SimpleCodecClient::GetGainFormat() { return gain_format_; }

zx::result<GainState> SimpleCodecClient::GetGainState() {
  fbl::AutoLock lock(&gain_state_lock_);
  return gain_state_;
}

void SimpleCodecClient::SetGainState(GainState gain_state) {
  fidl::Arena allocator;
  if (gain_pe_id_.has_value()) {
    auto gain = fuchsia_hardware_audio_signalprocessing::wire::ElementState::Builder(allocator);
    gain.enabled(true);
    auto gain_parameters =
        fuchsia_hardware_audio_signalprocessing::wire::GainElementState::Builder(allocator);
    gain_parameters.gain(gain_state.gain);
    gain.type_specific(
        fuchsia_hardware_audio_signalprocessing::wire::TypeSpecificElementState::WithGain(
            allocator, gain_parameters.Build()));
    auto ret = signal_processing_.sync()->SetElementState(gain_pe_id_.value(), gain.Build());
    if (!ret.ok()) {
      return;
    }
  }

  if (mute_pe_id_.has_value()) {
    auto mute = fuchsia_hardware_audio_signalprocessing::wire::ElementState::Builder(allocator);
    mute.enabled(gain_state.muted);
    auto ret = signal_processing_.sync()->SetElementState(mute_pe_id_.value(), mute.Build());
    if (!ret.ok()) {
      return;
    }
  }

  if (agc_pe_id_.has_value()) {
    auto agc = fuchsia_hardware_audio_signalprocessing::wire::ElementState::Builder(allocator);
    agc.enabled(gain_state.agc_enabled);
    auto ret = signal_processing_.sync()->SetElementState(agc_pe_id_.value(), agc.Build());
    if (!ret.ok()) {
      return;
    }
  }
}

void SimpleCodecClient::UpdateGainAndStartHangingGet(float gain) {
  {
    fbl::AutoLock lock(&gain_state_lock_);
    if (!gain_state_.is_ok()) {
      gain_state_ = zx::ok(GainState{});
    }
    gain_state_->gain = gain;
  }

  if (gain_pe_id_.has_value()) {
    signal_processing_->WatchElementState(gain_pe_id_.value())
        .Then([this](fidl::WireUnownedResult<
                     fuchsia_hardware_audio_signalprocessing::SignalProcessing::WatchElementState>&
                         result) {
          if (!result.ok()) {
            return;
          }
          if (result->state.has_enabled() && result->state.enabled() &&
              result->state.has_type_specific() && result->state.type_specific().is_gain() &&
              result->state.type_specific().gain().has_gain()) {
            UpdateGainAndStartHangingGet(result->state.type_specific().gain().gain());
          }
        });
  }
}

void SimpleCodecClient::UpdateMuteAndStartHangingGet(bool mute) {
  {
    fbl::AutoLock lock(&gain_state_lock_);
    if (!gain_state_.is_ok()) {
      gain_state_ = zx::ok(GainState{});
    }
    gain_state_->muted = mute;
  }

  if (mute_pe_id_.has_value()) {
    signal_processing_->WatchElementState(mute_pe_id_.value())
        .Then([this](fidl::WireUnownedResult<
                     fuchsia_hardware_audio_signalprocessing::SignalProcessing::WatchElementState>&
                         result) {
          if (!result.ok()) {
            return;
          }
          UpdateMuteAndStartHangingGet(result->state.has_enabled() && result->state.enabled());
        });
  }
}

void SimpleCodecClient::UpdateAgcAndStartHangingGet(bool agc) {
  {
    fbl::AutoLock lock(&gain_state_lock_);
    if (!gain_state_.is_ok()) {
      gain_state_ = zx::ok(GainState{});
    }
    gain_state_->agc_enabled = agc;
  }

  if (agc_pe_id_.has_value()) {
    signal_processing_->WatchElementState(agc_pe_id_.value())
        .Then([this](fidl::WireUnownedResult<
                     fuchsia_hardware_audio_signalprocessing::SignalProcessing::WatchElementState>&
                         result) {
          if (!result.ok()) {
            return;
          }
          UpdateAgcAndStartHangingGet(result->state.has_enabled() && result->state.enabled());
        });
  }
}

void SimpleCodecClient::Unbind() {
  // Wait for any pending channel operations to complete. This ensures we don't get a WatchGainState
  // callback after the client has been freed.
  if (codec_.is_valid()) {
    codec_.AsyncTeardown();
    codec_torn_down_.wait();
  }
  {
    fbl::AutoLock lock(&gain_state_lock_);
    gain_state_ = zx::error(ZX_ERR_BAD_STATE);
  }
}

}  // namespace audio
