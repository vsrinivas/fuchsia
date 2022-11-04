// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/v1/volume_control.h"

#include <lib/syslog/cpp/macros.h>

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
    : volume_setting_(volume_setting),
      dispatcher_(dispatcher),
      reporter_(Reporter::Singleton().CreateVolumeControl()) {}

void VolumeControl::AddBinding(fidl::InterfaceRequest<fuchsia::media::audio::VolumeControl> request,
                               std::string name) {
  name_ = name;
  bindings_.AddBinding(std::make_unique<VolumeControlImpl>(this), std::move(request), dispatcher_);
  bindings_.bindings().back()->events().OnVolumeMuteChanged(current_volume_, muted_);
  reporter_->AddBinding(name_);
}

void VolumeControl::SetVolume(float volume) {
  if (volume == current_volume_) {
    return;
  }
  FX_LOGS(INFO) << name_ << " VolumeControl::SetVolume(" << volume << ")";

  // TODO(fxbug.dev/35581): Generate event async after update from callback.
  current_volume_ = volume;
  if (!muted_) {
    volume_setting_->SetVolume(current_volume_);
  }
  reporter_->SetVolumeMute(current_volume_, muted_);
  NotifyClientsOfState();
}

void VolumeControl::SetMute(bool mute) {
  if (mute == muted_) {
    return;
  }
  FX_LOGS(INFO) << name_ << " VolumeControl::SetMute(" << mute << ")";
  muted_ = mute;

  auto vol = muted_ ? fuchsia::media::audio::MIN_VOLUME : current_volume_;
  volume_setting_->SetVolume(vol);
  reporter_->SetVolumeMute(vol, mute);
  NotifyClientsOfState();
}

void VolumeControl::NotifyClientsOfState() {
  for (auto& binding : bindings_.bindings()) {
    binding->events().OnVolumeMuteChanged(current_volume_, muted_);
  }
}

}  // namespace media::audio
