// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "drivers/audio/intel-hda/utils/codec-commands.h"

namespace audio {
namespace intel_hda {
namespace codecs {

struct CommandListEntry {
    uint16_t nid;
    CodecVerb verb;
};

struct StreamProperties {
    uint32_t stream_id;
    uint16_t conv_nid;
    uint16_t pc_nid;
    bool     is_input;
    bool     headphone_out;
    uint8_t  conv_unity_gain_lvl;
    uint8_t  pc_unity_gain_lvl;
};

}  // namespace codecs
}  // namespace audio
}  // namespace intel_hda
