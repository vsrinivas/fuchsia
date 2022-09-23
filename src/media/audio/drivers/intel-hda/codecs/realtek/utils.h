// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_DRIVERS_INTEL_HDA_CODECS_REALTEK_UTILS_H_
#define SRC_MEDIA_AUDIO_DRIVERS_INTEL_HDA_CODECS_REALTEK_UTILS_H_

#include <zircon/device/audio.h>

#include <vector>

#include <intel-hda/utils/codec-commands.h>

namespace audio {
namespace intel_hda {
namespace codecs {

struct CommandListEntry {
  uint16_t nid;
  CodecVerb verb;
};

enum StreamFixup {
  FIXUP_RESERVED,
  FIXUP_DELL1_HEADSET,
};

struct StreamProperties {
  uint32_t stream_id;
  uint16_t afg_nid;   // NID of the audio function group this stream belongs to.
  uint16_t conv_nid;  // NID of the converter used by this stream.
  uint16_t pc_nid;    // NID of the pin converter used by this stream.
  bool is_input;
  float default_conv_gain;
  float default_pc_gain;

  audio_stream_unique_id_t uid;
  const char* mfr_name = nullptr;      // String literals only!
  const char* product_name = nullptr;  // String literals only!

  // Ordered list of implementation fixups needed for this stream.
  // May be empty if the stream works with no fixups.
  std::vector<StreamFixup> fixups;
};

}  // namespace codecs
}  // namespace intel_hda
}  // namespace audio

#endif  // SRC_MEDIA_AUDIO_DRIVERS_INTEL_HDA_CODECS_REALTEK_UTILS_H_
