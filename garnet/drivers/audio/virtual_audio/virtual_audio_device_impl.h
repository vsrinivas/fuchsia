// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_AUDIO_VIRTUAL_AUDIO_VIRTUAL_AUDIO_DEVICE_IMPL_H_
#define GARNET_DRIVERS_AUDIO_VIRTUAL_AUDIO_VIRTUAL_AUDIO_DEVICE_IMPL_H_

#include <ddk/debug.h>
#include <fbl/ref_ptr.h>

#include "garnet/drivers/audio/virtual_audio/virtual_audio_config_impl.h"

namespace virtual_audio {

class VirtualAudioControlImpl;
class VirtualAudioStream;

class VirtualAudioDeviceImpl : public VirtualAudioConfigImpl {
 public:
  virtual bool CreateStream(zx_device_t* devnode) = 0;
  void RemoveStream();
  void ClearStream();

  //
  // virtualaudio.Device interface
  //
  void Add();

  void Remove();

  void ChangePlugState(zx_time_t plug_change_time, bool plugged);

 protected:
  VirtualAudioDeviceImpl(VirtualAudioControlImpl* owner);
  ~VirtualAudioDeviceImpl();

  VirtualAudioControlImpl const* owner_;
  fbl::RefPtr<VirtualAudioStream> stream_;
};

}  // namespace virtual_audio

#endif  // GARNET_DRIVERS_AUDIO_VIRTUAL_AUDIO_VIRTUAL_AUDIO_DEVICE_IMPL_H_
