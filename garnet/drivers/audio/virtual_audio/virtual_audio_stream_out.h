// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_AUDIO_VIRTUAL_AUDIO_VIRTUAL_AUDIO_STREAM_OUT_H_
#define GARNET_DRIVERS_AUDIO_VIRTUAL_AUDIO_VIRTUAL_AUDIO_STREAM_OUT_H_

#include <fbl/ref_ptr.h>

#include "garnet/drivers/audio/virtual_audio/virtual_audio_stream.h"

namespace virtual_audio {

class VirtualAudioDeviceImpl;

class VirtualAudioStreamOut
    : public VirtualAudioStream,
      public fbl::DoublyLinkedListable<fbl::RefPtr<VirtualAudioStreamOut>> {
 private:
  friend class SimpleAudioStream;
  friend class fbl::RefPtr<VirtualAudioStreamOut>;

  VirtualAudioStreamOut(VirtualAudioDeviceImpl* parent, zx_device_t* dev_node)
      : VirtualAudioStream(parent, dev_node, false /* is input */) {}
};

}  // namespace virtual_audio

#endif  // GARNET_DRIVERS_AUDIO_VIRTUAL_AUDIO_VIRTUAL_AUDIO_STREAM_OUT_H_
