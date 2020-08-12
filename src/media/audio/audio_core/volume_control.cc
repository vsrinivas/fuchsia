// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/volume_control.h"

#include "src/media/audio/lib/logging/logging.h"

namespace media::audio {

namespace {

// This exists because impls in a BindingSet cannot access their own binding to send events in a
// safe way.
class VolumeControlImpl : public fuchsia::media::audio::VolumeControl {
 public:
  VolumeControlImpl(::media::audio::VolumeControl* volume_control)
      : volume_control_(volume_control) {}

 private:
  // |fuchsia::media::audio::VolumeControl|
  void SetVolume(float volume) override { volume_control_->SetVolume(volume); }
  void SetMute(bool mute) override { volume_control_->SetMute(mute); }

  ::media::audio::VolumeControl* volume_control_;
};

}  // namespace

VolumeControl::VolumeControl(VolumeSetting* volume_setting, async_dispatcher_t* dispatcher)
    : volume_setting_(volume_setting), dispatcher_(dispatcher) {}

void VolumeControl::AddBinding(
    fidl::InterfaceRequest<fuchsia::media::audio::VolumeControl> request) {
  bindings_.AddBinding(std::make_unique<VolumeControlImpl>(this), std::move(request), dispatcher_);
  bindings_.bindings().back()->events().OnVolumeMuteChanged(current_volume_, muted_);
}

void VolumeControl::SetVolume(float volume) {
  const auto volume_is_changed = volume != current_volume_;
  if (!volume_is_changed) {
    return;
  }

  // TODO(fxbug.dev/35581): Generate event async after update from callback.
  current_volume_ = volume;
  if (!muted_) {
    volume_setting_->SetVolume(current_volume_);
  }

  NotifyClientsOfState();
}

void VolumeControl::SetMute(bool mute) {
  bool mute_is_changed = mute != muted_;
  if (!mute_is_changed) {
    return;
  }
  muted_ = mute;

  volume_setting_->SetVolume(muted_ ? fuchsia::media::audio::MIN_VOLUME : current_volume_);
  NotifyClientsOfState();
}

void VolumeControl::NotifyClientsOfState() {
  for (auto& binding : bindings_.bindings()) {
    binding->events().OnVolumeMuteChanged(current_volume_, muted_);
  }
}

}  // namespace media::audio
