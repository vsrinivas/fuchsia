// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_LIB_SIMPLE_CODEC_INCLUDE_LIB_SIMPLE_CODEC_SIMPLE_CODEC_SERVER_INTERNAL_H_
#define SRC_MEDIA_AUDIO_LIB_SIMPLE_CODEC_INCLUDE_LIB_SIMPLE_CODEC_SIMPLE_CODEC_SERVER_INTERNAL_H_

#include <fidl/fuchsia.hardware.audio/cpp/wire.h>
#include <fuchsia/hardware/audio/signalprocessing/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/simple-codec/simple-codec-types.h>
#include <lib/zircon-internal/thread_annotations.h>

#include <memory>
#include <optional>

#include <fbl/intrusive_double_list.h>
#include <fbl/mutex.h>
#include <sdk/lib/fidl/cpp/binding.h>

namespace audio {

class SimpleCodecServer;

template <class T>
class SimpleCodecServerInstance;

template <class T>
class SimpleCodecServerInternal {
 public:
  explicit SimpleCodecServerInternal();

 protected:
  uint64_t GetTopologyId() { return kTopologyId; }
  uint64_t GetGainPeId() { return kGainPeId; }
  uint64_t GetMutePeId() { return kMutePeId; }
  uint64_t GetAgcPeId() { return kAgcPeId; }

 private:
  using Codec = ::fuchsia::hardware::audio::Codec;

  friend class SimpleCodecServer;
  friend class SimpleCodecServerInstance<T>;

  static constexpr uint64_t kTopologyId = 1;
  static constexpr uint64_t kGainPeId = 1;
  static constexpr uint64_t kMutePeId = 2;
  static constexpr uint64_t kAgcPeId = 3;

  zx_status_t BindClient(zx::channel channel, async_dispatcher_t* dispatcher = nullptr);
  void OnUnbound(SimpleCodecServerInstance<T>* instance);

  void Reset(Codec::ResetCallback callback, SimpleCodecServerInstance<T>* instance);
  void Stop(Codec::StopCallback callback, SimpleCodecServerInstance<T>* instance);
  void Start(Codec::StartCallback callback, SimpleCodecServerInstance<T>* instance);
  void GetInfo(Codec::GetInfoCallback callback);
  void GetHealthState(Codec::GetHealthStateCallback callback) { callback({}); }
  void IsBridgeable(Codec::IsBridgeableCallback callback);
  void SetBridgedMode(bool enable_bridged_mode);
  void GetDaiFormats(Codec::GetDaiFormatsCallback callback);
  void SetDaiFormat(fuchsia::hardware::audio::DaiFormat format,
                    Codec::SetDaiFormatCallback callback);
  void GetPlugDetectCapabilities(Codec::GetPlugDetectCapabilitiesCallback callback);
  void WatchPlugState(Codec::WatchPlugStateCallback callback,
                      SimpleCodecServerInstance<T>* instance);

  virtual bool SupportsSignalProcessing() = 0;
  virtual void SignalProcessingConnect(
      fidl::InterfaceRequest<fuchsia::hardware::audio::signalprocessing::SignalProcessing>
          signal_processing);

  void GetElements(
      fuchsia::hardware::audio::signalprocessing::SignalProcessing::GetElementsCallback callback);
  void SetElementState(
      uint64_t processing_element_id,
      fuchsia::hardware::audio::signalprocessing::ElementState state,
      fuchsia::hardware::audio::signalprocessing::SignalProcessing::SetElementStateCallback
          callback,
      SimpleCodecServerInstance<T>* instance);
  void WatchElementState(
      uint64_t processing_element_id,
      fuchsia::hardware::audio::signalprocessing::SignalProcessing::WatchElementStateCallback
          callback,
      SimpleCodecServerInstance<T>* instance);
  void GetTopologies(
      fuchsia::hardware::audio::signalprocessing::SignalProcessing::GetTopologiesCallback callback);
  void SetTopology(
      uint64_t topology_id,
      fuchsia::hardware::audio::signalprocessing::SignalProcessing::SetTopologyCallback callback);

  zx_time_t plug_time_ = 0;

  fbl::Mutex instances_lock_;
  fbl::DoublyLinkedList<std::unique_ptr<SimpleCodecServerInstance<SimpleCodecServer>>> instances_
      TA_GUARDED(instances_lock_);
  bool load_gain_state_first_time_ = true;
  std::optional<GainState> last_gain_state_;
  GainState gain_state_ = {};
  std::optional<async::Loop> loop_;
};

template <class T>
class SimpleCodecServerInstance
    : public fuchsia::hardware::audio::Codec,
      public fuchsia::hardware::audio::signalprocessing::SignalProcessing,
      public fbl::DoublyLinkedListable<std::unique_ptr<SimpleCodecServerInstance<T>>> {
 public:
  SimpleCodecServerInstance(zx::channel channel, async_dispatcher_t* dispatcher,
                            SimpleCodecServerInternal<T>* parent)
      : parent_(parent), binding_(this, std::move(channel), dispatcher) {
    binding_.set_error_handler([&](zx_status_t status) { OnUnbound(); });
  }

 private:
  friend class SimpleCodecServerInternal<T>;

  void OnUnbound() { parent_->OnUnbound(this); }
  void GainStateUpdated(fuchsia::hardware::audio::GainState gain_state);

  void Reset(ResetCallback callback) override { parent_->Reset(std::move(callback), this); }
  void Stop(StopCallback callback) override { parent_->Stop(std::move(callback), this); }
  void Start(StartCallback callback) override { parent_->Start(std::move(callback), this); }
  void GetInfo(GetInfoCallback callback) override { parent_->GetInfo(std::move(callback)); }
  void GetHealthState(GetHealthStateCallback callback) override {
    parent_->GetHealthState(std::move(callback));
  }
  void SignalProcessingConnect(
      fidl::InterfaceRequest<fuchsia::hardware::audio::signalprocessing::SignalProcessing>
          signal_processing) override;
  void GetElements(GetElementsCallback callback) override {
    parent_->GetElements(std::move(callback));
  }
  void SetElementState(uint64_t processing_element_id,
                       fuchsia::hardware::audio::signalprocessing::ElementState state,
                       SetElementStateCallback callback) override {
    parent_->SetElementState(processing_element_id, std::move(state), std::move(callback), this);
  }
  void WatchElementState(
      uint64_t processing_element_id,
      fuchsia::hardware::audio::signalprocessing::SignalProcessing::WatchElementStateCallback
          callback) override {
    parent_->WatchElementState(processing_element_id, std::move(callback), this);
  }
  void GetTopologies(
      fuchsia::hardware::audio::signalprocessing::SignalProcessing::GetTopologiesCallback callback)
      override {
    parent_->GetTopologies(std::move(callback));
  }
  void SetTopology(uint64_t topology_id,
                   fuchsia::hardware::audio::signalprocessing::SignalProcessing::SetTopologyCallback
                       callback) override {
    parent_->SetTopology(topology_id, std::move(callback));
  }
  void IsBridgeable(IsBridgeableCallback callback) override {
    parent_->IsBridgeable(std::move(callback));
  }
  void SetBridgedMode(bool enable_bridged_mode) override {
    parent_->SetBridgedMode(enable_bridged_mode);
  }
  void GetDaiFormats(GetDaiFormatsCallback callback) override {
    parent_->GetDaiFormats(std::move(callback));
  }
  void SetDaiFormat(fuchsia::hardware::audio::DaiFormat format,
                    SetDaiFormatCallback callback) override {
    parent_->SetDaiFormat(std::move(format), std::move(callback));
  }
  void GetPlugDetectCapabilities(GetPlugDetectCapabilitiesCallback callback) override {
    parent_->GetPlugDetectCapabilities(std::move(callback));
  }
  void WatchPlugState(WatchPlugStateCallback callback) override {
    parent_->WatchPlugState(std::move(callback), this);
  }

  SimpleCodecServerInternal<T>* parent_;
  fidl::Binding<fuchsia::hardware::audio::Codec> binding_;
  std::optional<fidl::Binding<fuchsia::hardware::audio::signalprocessing::SignalProcessing>>
      signal_processing_binding_;
  bool plug_state_updated_ = true;  // Return the current plug state on the first call.

  bool gain_updated_ = true;  // Return the current gain state on the first call.
  std::optional<
      fuchsia::hardware::audio::signalprocessing::SignalProcessing::WatchElementStateCallback>
      gain_callback_;

  bool mute_updated_ = true;  // Return the current mute state on the first call.
  std::optional<
      fuchsia::hardware::audio::signalprocessing::SignalProcessing::WatchElementStateCallback>
      mute_callback_;

  bool agc_updated_ = true;  // Return the current AGC state on the first call.
  std::optional<
      fuchsia::hardware::audio::signalprocessing::SignalProcessing::WatchElementStateCallback>
      agc_callback_;
};

}  // namespace audio

#endif  // SRC_MEDIA_AUDIO_LIB_SIMPLE_CODEC_INCLUDE_LIB_SIMPLE_CODEC_SIMPLE_CODEC_SERVER_INTERNAL_H_
