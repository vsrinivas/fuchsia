// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_VOLUME_CONTROL_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_VOLUME_CONTROL_H_

#include <fuchsia/media/audio/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>

#include <memory>

namespace media::audio {

// An interface for a volume settings. Calls are made on the FIDL thread.
class VolumeSetting {
 public:
  // TODO(fxbug.dev/35581): Add a callback here to support devices with low volume setting
  // granularity.
  virtual void SetVolume(float volume) = 0;
};

// Serves fuchsia.media.audio.VolumeControl for a single `VolumeSetting` to many clients. It
// assumes it is the sole control point of the `VolumeSetting`. This is assumed to run on the
// FIDL thread.
class VolumeControl {
 public:
  // Clients will be disconnected after receiving this many events without sending an ACK.
  static constexpr size_t kMaxEventsSentWithoutAck = 30;

  VolumeControl(VolumeControl&) = delete;
  VolumeControl(VolumeControl&&) = delete;
  VolumeControl& operator=(VolumeControl) = delete;
  VolumeControl& operator=(VolumeControl&&) = delete;

  VolumeControl(VolumeSetting* volume_setting, async_dispatcher_t* dispatcher);

  void AddBinding(fidl::InterfaceRequest<fuchsia::media::audio::VolumeControl> request);

  // Sets the volume, notifies all clients, and persists the volume internally so it survives mutes.
  void SetVolume(float volume);

  void SetMute(bool mute);

 private:
  // Notifies FIDL clients of the volume setting's state.
  void NotifyClientsOfState();

  fidl::BindingSet<fuchsia::media::audio::VolumeControl,
                   std::unique_ptr<fuchsia::media::audio::VolumeControl>>
      bindings_;

  float current_volume_ = 1.0;
  bool muted_ = false;
  VolumeSetting* volume_setting_;
  async_dispatcher_t* dispatcher_;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_VOLUME_CONTROL_H_
