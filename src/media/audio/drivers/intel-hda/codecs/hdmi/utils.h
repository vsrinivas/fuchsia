// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_DRIVERS_INTEL_HDA_CODECS_HDMI_UTILS_H_
#define SRC_MEDIA_AUDIO_DRIVERS_INTEL_HDA_CODECS_HDMI_UTILS_H_

#include <intel-hda/utils/codec-commands.h>

namespace audio::intel_hda::codecs {

struct CommandListEntry {
  uint16_t nid;
  CodecVerb verb;
};

struct StreamProperties {
  // We use the monitor name from the HDA spec as product_name below, hence we need to have
  // enough memory for the maximum monitor name valid size. This maximum length does not include a
  // null termination as defined in the HDA spec, so we add 1 to the length for storage below to
  // be able to add a null termination.
  static constexpr size_t kMaxValidMonitorNameLength = 16;

  uint32_t stream_id;
  uint16_t afg_nid;   // NID of the audio function group this stream belongs to.
  uint16_t conv_nid;  // NID of the converter used by this stream.
  uint16_t pc_nid;    // NID of the pin converter used by this stream.
  float default_conv_gain;
  float default_pc_gain;

  const char* mfr_name;                               // No need for storage, we get it from a LUT.
  char product_name[kMaxValidMonitorNameLength + 1];  // + 1 for null termination.
};

}  // namespace audio::intel_hda::codecs

#endif  // SRC_MEDIA_AUDIO_DRIVERS_INTEL_HDA_CODECS_HDMI_UTILS_H_
