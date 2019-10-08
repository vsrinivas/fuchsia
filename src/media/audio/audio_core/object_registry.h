// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_OBJECT_REGISTRY_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_OBJECT_REGISTRY_H_

#include <lib/zx/time.h>

#include <fbl/ref_ptr.h>

namespace media::audio {

class AudioCapturerImpl;
class AudioRendererImpl;
class AudioDevice;

class ObjectRegistry {
 public:
  virtual ~ObjectRegistry() = default;

  virtual void AddAudioRenderer(fbl::RefPtr<AudioRendererImpl> audio_renderer) = 0;
  virtual void RemoveAudioRenderer(AudioRendererImpl* audio_renderer) = 0;
  virtual void AddAudioCapturer(const fbl::RefPtr<AudioCapturerImpl>& audio_capturer) = 0;
  virtual void RemoveAudioCapturer(AudioCapturerImpl* audio_capturer) = 0;

  // Begin initializing a device and add it to the set of devices waiting to be initialized.
  //
  // Called from the plug detector when a new stream device first shows up.
  virtual void AddDevice(const fbl::RefPtr<AudioDevice>& device) = 0;

  // Move device from pending-init list to active-devices list. Notify users and re-evaluate policy.
  virtual void ActivateDevice(const fbl::RefPtr<AudioDevice>& device) = 0;

  // Shutdown this device; remove it from the appropriate set of active devices.
  virtual void RemoveDevice(const fbl::RefPtr<AudioDevice>& device) = 0;

  // Handles a plugged/unplugged state change for the supplied audio device.
  virtual void OnPlugStateChanged(const fbl::RefPtr<AudioDevice>& device, bool plugged,
                                  zx::time plug_time) = 0;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_OBJECT_REGISTRY_H_
