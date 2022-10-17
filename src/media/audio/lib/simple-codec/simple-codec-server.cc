// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/binding.h>
#include <lib/ddk/platform-defs.h>
#include <lib/simple-codec/simple-codec-server.h>

#include <algorithm>
#include <memory>

#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>

namespace audio {

namespace audio_fidl = ::fuchsia::hardware::audio;
namespace signal_fidl = ::fuchsia::hardware::audio::signalprocessing;

// SimpleCodecServer methods.
zx_status_t SimpleCodecServer::CreateAndAddToDdkInternal() {
  simple_codec_ = inspect_.GetRoot().CreateChild("simple_codec");
  state_ = simple_codec_.CreateString("state", "created");
  start_time_ = simple_codec_.CreateInt("start_time", 0);

  number_of_channels_ = simple_codec_.CreateUint("number_of_channels", 0);
  channels_to_use_bitmask_ = simple_codec_.CreateUint("channels_to_use_bitmask", 0);
  frame_rate_ = simple_codec_.CreateUint("frame_rate", 0);
  bits_per_slot_ = simple_codec_.CreateUint("bits_per_slot", 0);
  bits_per_sample_ = simple_codec_.CreateUint("bits_per_sample", 0);
  sample_format_ = simple_codec_.CreateString("sample_format", "not_set");
  frame_format_ = simple_codec_.CreateString("frame_format", "not_set");

  auto res = Initialize();
  if (res.is_error()) {
    return res.error_value();
  }
  loop_->StartThread();

  driver_ids_ = res.value();
  Info info = GetInfo();
  simple_codec_.CreateString("manufacturer", info.manufacturer, &inspect_);
  simple_codec_.CreateString("product", info.product_name, &inspect_);
  if (!info.unique_id.empty()) {
    simple_codec_.CreateString("unique_id", info.unique_id, &inspect_);
  }
  if (driver_ids_.instance_count != 0) {
    if (info.unique_id.empty()) {
      simple_codec_.CreateString("unique_id", std::to_string(driver_ids_.instance_count),
                                 &inspect_);
    }
    zx_device_prop_t props[] = {
        {BIND_PLATFORM_DEV_VID, 0, driver_ids_.vendor_id},
        {BIND_PLATFORM_DEV_DID, 0, driver_ids_.device_id},
        {BIND_CODEC_INSTANCE, 0, driver_ids_.instance_count},
    };
    return DdkAdd(ddk::DeviceAddArgs(info.product_name.c_str())
                      .set_props(props)
                      .set_inspect_vmo(inspect_.DuplicateVmo())
                      .set_flags(DEVICE_ADD_ALLOW_MULTI_COMPOSITE));
  }
  zx_device_prop_t props[] = {
      {BIND_PLATFORM_DEV_VID, 0, driver_ids_.vendor_id},
      {BIND_PLATFORM_DEV_DID, 0, driver_ids_.device_id},
  };
  return DdkAdd(ddk::DeviceAddArgs(info.product_name.c_str())
                    .set_props(props)
                    .set_inspect_vmo(inspect_.DuplicateVmo())
                    .set_flags(DEVICE_ADD_ALLOW_MULTI_COMPOSITE));
}

zx_status_t SimpleCodecServer::CodecConnect(zx::channel channel) {
  return BindClient(std::move(channel), loop_->dispatcher());
}

void SimpleCodecServer::SignalProcessingConnect(
    fidl::InterfaceRequest<signal_fidl::SignalProcessing> signal_processing) {
  signal_processing.Close(ZX_ERR_NOT_SUPPORTED);
}

// SimpleCodecServerInternal methods.
template <class T>
SimpleCodecServerInternal<T>::SimpleCodecServerInternal() {
  plug_time_ = zx::clock::get_monotonic().get();
}

template <class T>
zx_status_t SimpleCodecServerInternal<T>::BindClient(zx::channel channel,
                                                     async_dispatcher_t* dispatcher) {
  auto instance = std::make_unique<SimpleCodecServerInstance<SimpleCodecServer>>(std::move(channel),
                                                                                 dispatcher, this);
  fbl::AutoLock lock(&instances_lock_);
  instances_.push_back(std::move(instance));
  return ZX_OK;
}

template <class T>
void SimpleCodecServerInternal<T>::OnUnbound(SimpleCodecServerInstance<T>* instance) {
  fbl::AutoLock lock(&instances_lock_);
  instances_.erase(*instance);
}

template <class T>
void SimpleCodecServerInternal<T>::Reset(Codec::ResetCallback callback,
                                         SimpleCodecServerInstance<T>* instance) {
  auto status = static_cast<T*>(this)->Reset();
  if (status != ZX_OK) {
    instance->binding_.Unbind();
    fbl::AutoLock lock(&instances_lock_);
    instances_.erase(*instance);
  }
  callback();
}

template <class T>
void SimpleCodecServerInternal<T>::Stop(Codec::StopCallback callback,
                                        SimpleCodecServerInstance<T>* instance) {
  auto status = static_cast<T*>(this)->Stop();
  if (status != ZX_OK) {
    static_cast<T*>(this)->state_.Set("stopped error");
    instance->binding_.Unbind();
    fbl::AutoLock lock(&instances_lock_);
    instances_.erase(*instance);
  } else {
    static_cast<T*>(this)->state_.Set("stopped");
  }
  int64_t stop_time = zx::clock::get_monotonic().get();
  callback(stop_time);
}

template <class T>
void SimpleCodecServerInternal<T>::Start(Codec::StartCallback callback,
                                         SimpleCodecServerInstance<T>* instance) {
  auto status = static_cast<T*>(this)->Start();
  if (status != ZX_OK) {
    static_cast<T*>(this)->state_.Set("start error");
    instance->binding_.Unbind();
    fbl::AutoLock lock(&instances_lock_);
    instances_.erase(*instance);
  } else {
    static_cast<T*>(this)->state_.Set("started");
  }
  int64_t start_time = zx::clock::get_monotonic().get();
  static_cast<T*>(this)->start_time_.Set(start_time);
  callback(start_time);
}

template <class T>
void SimpleCodecServerInternal<T>::GetInfo(Codec::GetInfoCallback callback) {
  callback(static_cast<T*>(this)->GetInfo());
}

template <class T>
void SimpleCodecServerInternal<T>::SignalProcessingConnect(
    fidl::InterfaceRequest<signal_fidl::SignalProcessing> signal_processing) {
  static_cast<T*>(this)->SignalProcessingConnect(std::move(signal_processing));
}

template <class T>
void SimpleCodecServerInternal<T>::GetElements(
    signal_fidl::SignalProcessing::GetElementsCallback callback) {
  std::vector<signal_fidl::Element> pes;
  auto format = static_cast<T*>(this)->GetGainFormat();
  {
    signal_fidl::Element pe;
    pe.set_id(kGainPeId);
    pe.set_type(signal_fidl::ElementType::GAIN);
    signal_fidl::Gain gain;
    gain.set_type(signal_fidl::GainType::DECIBELS);  // Only decibels in simple codec.
    gain.set_min_gain(format.min_gain);
    gain.set_max_gain(format.max_gain);
    gain.set_min_gain_step(format.gain_step);
    pe.set_type_specific(signal_fidl::TypeSpecificElement::WithGain(std::move(gain)));
    pes.emplace_back(std::move(pe));
  }
  if (format.can_mute) {
    signal_fidl::Element pe;
    pe.set_id(kMutePeId);
    pe.set_type(signal_fidl::ElementType::MUTE);
    pes.emplace_back(std::move(pe));
  }
  if (format.can_agc) {
    signal_fidl::Element pe;
    pe.set_id(kAgcPeId);
    pe.set_type(signal_fidl::ElementType::AUTOMATIC_GAIN_CONTROL);
    pes.emplace_back(std::move(pe));
  }

  signal_fidl::Reader_GetElements_Response response(std::move(pes));
  signal_fidl::Reader_GetElements_Result result;
  result.set_response(std::move(response));
  callback(std::move(result));
}

template <class T>
void SimpleCodecServerInternal<T>::SetElementState(
    uint64_t processing_element_id, signal_fidl::ElementState state,
    signal_fidl::SignalProcessing::SetElementStateCallback callback,
    SimpleCodecServerInstance<T>* unused_instance) {
  // The unused_instance parameter indicates which instance triggered this call,
  // but we must update all appropriate instances callbacks with the potentially new state.
  switch (processing_element_id) {
    case kGainPeId:
      gain_state_.gain = state.type_specific().gain().gain();

      {
        fbl::AutoLock lock(&instances_lock_);
        for (auto& instance : instances_) {
          if (instance.gain_callback_) {
            signal_fidl::GainElementState gain_state;
            gain_state.set_gain(gain_state_.gain);
            signal_fidl::ElementState state = {};
            state.set_type_specific(
                signal_fidl::TypeSpecificElementState::WithGain(std::move(gain_state)));
            state.set_enabled(true);
            (*instance.gain_callback_)(std::move(state));
            instance.gain_callback_.reset();
            instance.gain_updated_ = false;
          } else {
            // Setting true here triggers WatchElementState to immediately reply with the latest
            // gain state next the time it is called.
            instance.gain_updated_ = true;
          }
        }
      }
      callback(signal_fidl::SignalProcessing_SetElementState_Result::WithResponse(
          signal_fidl::SignalProcessing_SetElementState_Response()));
      break;

    case kMutePeId:
      gain_state_.muted = state.enabled();

      {
        fbl::AutoLock lock(&instances_lock_);
        for (auto& instance : instances_) {
          if (instance.mute_callback_) {
            signal_fidl::ElementState state = {};
            state.set_enabled(gain_state_.muted);
            (*instance.mute_callback_)(std::move(state));
            instance.mute_callback_.reset();
            instance.mute_updated_ = false;
          } else {
            // Setting true here triggers WatchElementState to immediately reply with the latest
            // mute state next the time it is called.
            instance.mute_updated_ = true;
          }
        }
      }
      callback(signal_fidl::SignalProcessing_SetElementState_Result::WithResponse(
          signal_fidl::SignalProcessing_SetElementState_Response()));
      break;

    case kAgcPeId:
      gain_state_.agc_enabled = state.enabled();
      {
        fbl::AutoLock lock(&instances_lock_);
        for (auto& instance : instances_) {
          if (instance.agc_callback_) {
            signal_fidl::ElementState state = {};
            state.set_enabled(gain_state_.agc_enabled);
            (*instance.agc_callback_)(std::move(state));
            instance.agc_callback_.reset();
            instance.agc_updated_ = false;
          } else {
            // Setting true here triggers WatchElementState to immediately reply with the latest
            // AGC state next the time it is called.
            instance.agc_updated_ = true;
          }
        }
      }
      callback(signal_fidl::SignalProcessing_SetElementState_Result::WithResponse(
          signal_fidl::SignalProcessing_SetElementState_Response()));
      break;

    default:
      callback(signal_fidl::SignalProcessing_SetElementState_Result::WithErr(ZX_ERR_INVALID_ARGS));
      break;
  }
  if (!last_gain_state_.has_value() || last_gain_state_->gain != gain_state_.gain ||
      last_gain_state_->muted != gain_state_.muted ||
      last_gain_state_->agc_enabled != gain_state_.agc_enabled) {
    static_cast<T*>(this)->SetGainState(gain_state_);
    last_gain_state_.emplace(gain_state_);
  }
}

template <class T>
void SimpleCodecServerInternal<T>::WatchElementState(
    uint64_t processing_element_id,
    signal_fidl::SignalProcessing::WatchElementStateCallback callback,
    SimpleCodecServerInstance<T>* instance) {
  if (processing_element_id != kGainPeId && processing_element_id != kMutePeId &&
      processing_element_id != kAgcPeId) {
    return;
  }

  if (load_gain_state_first_time_) {
    gain_state_ = static_cast<T*>(this)->GetGainState();
    last_gain_state_.emplace(gain_state_);
    load_gain_state_first_time_ = false;
  }

  // Reply immediately if the either the gain, mute or AGC has been updated since the last call.
  // Otherwise store the callback and reply the next time the gain state is updated. Only one
  // hanging get may beoutstanding at a time.
  switch (processing_element_id) {
    case kGainPeId:
      if (instance->gain_updated_) {
        instance->gain_updated_ = false;

        signal_fidl::GainElementState gain_state;
        gain_state.set_gain(gain_state_.gain);
        signal_fidl::ElementState state = {};
        state.set_type_specific(
            signal_fidl::TypeSpecificElementState::WithGain(std::move(gain_state)));
        state.set_enabled(true);
        callback(std::move(state));
      } else if (!instance->gain_callback_) {
        instance->gain_callback_.emplace(std::move(callback));
      } else {
        // The client called WatchElementState when another hanging get was pending.
        // This is an error condition and hence we unbind and remove the instance.
        instance->binding_.Unbind();
        fbl::AutoLock lock(&instances_lock_);
        instances_.erase(*instance);
      }
      break;

    case kMutePeId:
      if (instance->mute_updated_) {
        instance->mute_updated_ = false;

        signal_fidl::ElementState state = {};
        state.set_enabled(gain_state_.muted);
        callback(std::move(state));
      } else if (!instance->mute_callback_) {
        instance->mute_callback_.emplace(std::move(callback));
      } else {
        // The client called WatchElementState when another hanging get was pending.
        // This is an error condition and hence we unbind and remove the instance.
        instance->binding_.Unbind();
        fbl::AutoLock lock(&instances_lock_);
        instances_.erase(*instance);
      }
      break;

    case kAgcPeId:
      if (instance->agc_updated_) {
        instance->agc_updated_ = false;

        signal_fidl::ElementState state = {};
        state.set_enabled(gain_state_.agc_enabled);
        callback(std::move(state));
      } else if (!instance->agc_callback_) {
        instance->agc_callback_.emplace(std::move(callback));
      } else {
        // The client called WatchElementState when another hanging get was pending.
        // This is an error condition and hence we unbind and remove the instance.
        instance->binding_.Unbind();
        fbl::AutoLock lock(&instances_lock_);
        instances_.erase(*instance);
      }
      break;
  }
}

template <class T>
void SimpleCodecServerInternal<T>::GetTopologies(
    signal_fidl::SignalProcessing::GetTopologiesCallback callback) {
  std::vector<signal_fidl::EdgePair> edges;
  {
    signal_fidl::EdgePair edge;
    edge.processing_element_id_from = kGainPeId;
    edge.processing_element_id_to = kMutePeId;
    edges.emplace_back(edge);
  }
  {
    signal_fidl::EdgePair edge;
    edge.processing_element_id_from = kMutePeId;
    edge.processing_element_id_to = kAgcPeId;
    edges.emplace_back(edge);
  }

  signal_fidl::Topology topology;
  topology.set_id(kTopologyId);
  topology.set_processing_elements_edge_pairs(edges);

  std::vector<signal_fidl::Topology> topologies;
  topologies.emplace_back(std::move(topology));

  signal_fidl::Reader_GetTopologies_Response response(std::move(topologies));
  signal_fidl::Reader_GetTopologies_Result result;
  result.set_response(std::move(response));
  callback(std::move(result));
}

template <class T>
void SimpleCodecServerInternal<T>::SetTopology(
    uint64_t topology_id, signal_fidl::SignalProcessing::SetTopologyCallback callback) {
  // We only support one topology, return error if any mismatch.
  if (topology_id != kTopologyId) {
    callback(signal_fidl::SignalProcessing_SetTopology_Result::WithErr(ZX_ERR_INVALID_ARGS));
    return;
  }
  callback(signal_fidl::SignalProcessing_SetTopology_Result::WithResponse(
      signal_fidl::SignalProcessing_SetTopology_Response()));
}

template <class T>
void SimpleCodecServerInternal<T>::GetPlugDetectCapabilities(
    Codec::GetPlugDetectCapabilitiesCallback callback) {
  // Only hardwired in simple codec.
  callback(audio_fidl::PlugDetectCapabilities::HARDWIRED);
}

template <class T>
void SimpleCodecServerInternal<T>::WatchPlugState(Codec::WatchPlugStateCallback callback,
                                                  SimpleCodecServerInstance<T>* instance) {
  // Since the library only advertsises a hardwired codec, it returns that the codec is always
  // plugged and only replies to this hanging-get with the plugged state once per instance.
  // Hence for the first WatchPlugState call reply immediately, otherwise do not reply.
  if (instance->plug_state_updated_) {
    instance->plug_state_updated_ = false;
    fuchsia::hardware::audio::PlugState plug_state;
    plug_state.set_plugged(true);
    plug_state.set_plug_state_time(plug_time_);
    callback(std::move(plug_state));
  }
}

template <class T>
void SimpleCodecServerInternal<T>::IsBridgeable(audio_fidl::Codec::IsBridgeableCallback callback) {
  callback(static_cast<T*>(this)->IsBridgeable());
}

template <class T>
void SimpleCodecServerInternal<T>::SetBridgedMode(bool enable_bridged_mode) {
  static_cast<T*>(this)->SetBridgedMode(enable_bridged_mode);
}

template <class T>
void SimpleCodecServerInternal<T>::GetDaiFormats(Codec::GetDaiFormatsCallback callback) {
  auto formats = static_cast<T*>(this)->GetDaiFormats();
  std::vector<audio_fidl::DaiFrameFormat> frame_formats;
  for (FrameFormat i : formats.frame_formats) {
    audio_fidl::DaiFrameFormat frame_format;
    frame_format.set_frame_format_standard(i);
    frame_formats.emplace_back(std::move(frame_format));
  }
  audio_fidl::Codec_GetDaiFormats_Response response;
  response.formats.emplace_back(audio_fidl::DaiSupportedFormats{
      .number_of_channels = std::move(formats.number_of_channels),
      .sample_formats = std::move(formats.sample_formats),
      .frame_formats = std::move(frame_formats),
      .frame_rates = std::move(formats.frame_rates),
      .bits_per_slot = std::move(formats.bits_per_slot),
      .bits_per_sample = std::move(formats.bits_per_sample),
  });
  audio_fidl::Codec_GetDaiFormats_Result result;
  result.set_response(std::move(response));
  callback(std::move(result));
}

template <class T>
void SimpleCodecServerInternal<T>::SetDaiFormat(audio_fidl::DaiFormat format,
                                                Codec::SetDaiFormatCallback callback) {
  DaiFormat format2 = {};
  format2.number_of_channels = format.number_of_channels;
  format2.channels_to_use_bitmask = format.channels_to_use_bitmask;
  format2.sample_format = format.sample_format;
  format2.frame_format = format.frame_format.frame_format_standard();
  format2.frame_rate = format.frame_rate;
  format2.bits_per_slot = format.bits_per_slot;
  format2.bits_per_sample = format.bits_per_sample;
  auto* thiz = static_cast<T*>(this);
  thiz->number_of_channels_.Set(format2.number_of_channels);
  thiz->channels_to_use_bitmask_.Set(format2.channels_to_use_bitmask);
  thiz->frame_rate_.Set(format2.frame_rate);
  thiz->bits_per_slot_.Set(format2.bits_per_slot);
  thiz->bits_per_sample_.Set(format2.bits_per_sample);
  using FidlSampleFormat = audio_fidl::DaiSampleFormat;
  // clang-format off
  switch (format2.sample_format) {
    case FidlSampleFormat::PDM:          thiz->sample_format_.Set("PDM");          break;
    case FidlSampleFormat::PCM_SIGNED:   thiz->sample_format_.Set("PCM_signed");   break;
    case FidlSampleFormat::PCM_UNSIGNED: thiz->sample_format_.Set("PCM_unsigned"); break;
    case FidlSampleFormat::PCM_FLOAT:    thiz->sample_format_.Set("PCM_float");    break;
  }
  // clang-format on
  using FidlFrameFormat = audio_fidl::DaiFrameFormatStandard;
  // clang-format off
  switch (format2.frame_format) {
    case FidlFrameFormat::NONE:         thiz->frame_format_.Set("NONE");         break;
    case FidlFrameFormat::I2S:          thiz->frame_format_.Set("I2S");          break;
    case FidlFrameFormat::STEREO_LEFT:  thiz->frame_format_.Set("Stereo_left");  break;
    case FidlFrameFormat::STEREO_RIGHT: thiz->frame_format_.Set("Stereo_right"); break;
    case FidlFrameFormat::TDM1:         thiz->frame_format_.Set("TDM1");         break;
    case FidlFrameFormat::TDM2:         thiz->frame_format_.Set("TDM2");         break;
    case FidlFrameFormat::TDM3:         thiz->frame_format_.Set("TDM3");         break;
  }
  // clang-format on
  audio_fidl::Codec_SetDaiFormat_Result result = {};
  zx::result<CodecFormatInfo> format_info = thiz->SetDaiFormat(std::move(format2));
  if (!format_info.is_ok()) {
    thiz->state_.Set(std::string("Set DAI format error: ") + format_info.status_string());
    result.set_err(format_info.status_value());
    callback(std::move(result));
    return;
  }
  audio_fidl::Codec_SetDaiFormat_Response response = {};
  audio_fidl::CodecFormatInfo codec_state = {};
  if (format_info->has_external_delay()) {
    codec_state.set_external_delay(format_info->external_delay());
  }
  if (format_info->has_turn_on_delay()) {
    codec_state.set_turn_on_delay(format_info->turn_on_delay());
  }
  if (format_info->has_turn_off_delay()) {
    codec_state.set_turn_off_delay(format_info->turn_off_delay());
  }
  response.state = std::move(codec_state);
  result.set_response(std::move(response));
  callback(std::move(result));
}

// SimpleCodecServerInstance methods.

template <class T>
void SimpleCodecServerInstance<T>::SignalProcessingConnect(
    fidl::InterfaceRequest<signal_fidl::SignalProcessing> signal_processing) {
  if (parent_->SupportsSignalProcessing()) {
    parent_->SignalProcessingConnect(std::move(signal_processing));
  } else {
    if (signal_processing_binding_.has_value()) {
      signal_processing.Close(ZX_ERR_ALREADY_BOUND);
      return;
    }
    signal_processing_binding_.emplace(this, std::move(signal_processing),
                                       parent_->loop_->dispatcher());
  }
}

template class SimpleCodecServerInternal<SimpleCodecServer>;

}  // namespace audio
