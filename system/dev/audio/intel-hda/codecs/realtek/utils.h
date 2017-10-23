// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <intel-hda/utils/codec-commands.h>

namespace audio {
namespace intel_hda {
namespace codecs {

struct CommandListEntry {
    uint16_t nid;
    CodecVerb verb;
};

struct StreamProperties {
    uint32_t stream_id;
    uint16_t afg_nid;   // NID of the audio function group this stream belongs to.
    uint16_t conv_nid;  // NID of the converter used by this stream.
    uint16_t pc_nid;    // NID of the pin converter used by this stream.
    bool     is_input;
    float    default_conv_gain;
    float    default_pc_gain;
};

}  // namespace codecs
}  // namespace audio
}  // namespace intel_hda
