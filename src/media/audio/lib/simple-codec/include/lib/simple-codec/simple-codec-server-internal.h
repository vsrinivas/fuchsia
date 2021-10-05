// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_LIB_SIMPLE_CODEC_INCLUDE_LIB_SIMPLE_CODEC_SIMPLE_CODEC_SERVER_INTERNAL_H_
#define SRC_MEDIA_AUDIO_LIB_SIMPLE_CODEC_INCLUDE_LIB_SIMPLE_CODEC_SIMPLE_CODEC_SERVER_INTERNAL_H_

#include <fidl/fuchsia.hardware.audio/cpp/wire.h>
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

 private:
  using Codec = ::fuchsia::hardware::audio::Codec;

  friend class SimpleCodecServer;
  friend class SimpleCodecServerInstance<T>;

  zx_status_t BindClient(zx::channel channel, async_dispatcher_t* dispatcher = nullptr);
  void OnUnbound(SimpleCodecServerInstance<T>* instance);

  void Reset(Codec::ResetCallback callback, SimpleCodecServerInstance<T>* instance);
  void Stop(Codec::StopCallback callback, SimpleCodecServerInstance<T>* instance);
  void Start(Codec::StartCallback callback, SimpleCodecServerInstance<T>* instance);
  void GetInfo(Codec::GetInfoCallback callback);
  void IsBridgeable(Codec::IsBridgeableCallback callback);
  virtual void SetBridgedMode(bool enable_bridged_mode) = 0;
  void GetDaiFormats(Codec::GetDaiFormatsCallback callback);
  void SetDaiFormat(::fuchsia::hardware::audio::DaiFormat format,
                    Codec::SetDaiFormatCallback callback);
  void GetGainFormat(Codec::GetGainFormatCallback callback);
  void WatchGainState(Codec::WatchGainStateCallback callback,
                      SimpleCodecServerInstance<T>* instance);
  void SetGainState(::fuchsia::hardware::audio::GainState state);
  void GetPlugDetectCapabilities(Codec::GetPlugDetectCapabilitiesCallback callback);
  void WatchPlugState(Codec::WatchPlugStateCallback callback);

  zx_time_t plug_time_ = 0;

  fbl::Mutex instances_lock_;
  fbl::DoublyLinkedList<std::unique_ptr<SimpleCodecServerInstance<SimpleCodecServer>>> instances_
      TA_GUARDED(instances_lock_);
  bool load_gain_state_first_time_ = true;
  GainState gain_state_ = {};
};

template <class T>
class SimpleCodecServerInstance
    : public ::fuchsia::hardware::audio::Codec,
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
  void GainStateUpdated(::fuchsia::hardware::audio::GainState gain_state);

  void Reset(ResetCallback callback) override { parent_->Reset(std::move(callback), this); }
  void Stop(StopCallback callback) override { parent_->Stop(std::move(callback), this); }
  void Start(StartCallback callback) override { parent_->Start(std::move(callback), this); }
  void GetInfo(GetInfoCallback callback) override { parent_->GetInfo(std::move(callback)); }
  void IsBridgeable(IsBridgeableCallback callback) override {
    parent_->IsBridgeable(std::move(callback));
  }
  void SetBridgedMode(bool enable_bridged_mode) override {
    parent_->SetBridgedMode(enable_bridged_mode);
  }
  void GetDaiFormats(GetDaiFormatsCallback callback) override {
    parent_->GetDaiFormats(std::move(callback));
  }
  void SetDaiFormat(::fuchsia::hardware::audio::DaiFormat format,
                    SetDaiFormatCallback callback) override {
    parent_->SetDaiFormat(std::move(format), std::move(callback));
  }
  void GetGainFormat(GetGainFormatCallback callback) override {
    parent_->GetGainFormat(std::move(callback));
  }
  void WatchGainState(WatchGainStateCallback callback) override {
    parent_->WatchGainState(std::move(callback), this);
  }
  void SetGainState(::fuchsia::hardware::audio::GainState state) override {
    parent_->SetGainState(std::move(state));
  }
  void GetPlugDetectCapabilities(GetPlugDetectCapabilitiesCallback callback) override {
    parent_->GetPlugDetectCapabilities(std::move(callback));
  }
  void WatchPlugState(WatchPlugStateCallback callback) override;

  SimpleCodecServerInternal<T>* parent_;
  fidl::Binding<::fuchsia::hardware::audio::Codec> binding_;
  bool watch_plug_state_first_time_ = true;
  bool gain_state_updated_ = true;  // Return the current gain state on the first call.
  std::optional<WatchGainStateCallback> gain_state_callback_;
};

}  // namespace audio

#endif  // SRC_MEDIA_AUDIO_LIB_SIMPLE_CODEC_INCLUDE_LIB_SIMPLE_CODEC_SIMPLE_CODEC_SERVER_INTERNAL_H_
