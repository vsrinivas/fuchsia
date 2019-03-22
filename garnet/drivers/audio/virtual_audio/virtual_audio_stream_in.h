// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_AUDIO_VIRTUAL_AUDIO_VIRTUAL_AUDIO_STREAM_IN_H_
#define GARNET_DRIVERS_AUDIO_VIRTUAL_AUDIO_VIRTUAL_AUDIO_STREAM_IN_H_

#include <fbl/ref_ptr.h>

#include "garnet/drivers/audio/virtual_audio/virtual_audio_stream.h"

namespace virtual_audio {

class VirtualAudioDeviceImpl;

class VirtualAudioStreamIn
    : public VirtualAudioStream,
      public fbl::DoublyLinkedListable<fbl::RefPtr<VirtualAudioStreamIn>> {
 private:
  friend class SimpleAudioStream;
  friend class fbl::RefPtr<VirtualAudioStreamIn>;

  VirtualAudioStreamIn(VirtualAudioDeviceImpl* parent, zx_device_t* dev_node)
      : VirtualAudioStream(parent, dev_node, true /* is input */) {}
};

}  // namespace virtual_audio

#endif  // GARNET_DRIVERS_AUDIO_VIRTUAL_AUDIO_VIRTUAL_AUDIO_STREAM_IN_H_
