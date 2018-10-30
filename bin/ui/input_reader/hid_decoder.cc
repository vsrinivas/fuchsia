// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/input_reader/hid_decoder.h"

namespace mozart {

HidDecoder::HidDecoder() = default;
HidDecoder::~HidDecoder() = default;

bool HidDecoder::use_legacy_mode() const {
  Protocol p = protocol();
  return p != Protocol::Gamepad && p != Protocol::Buttons &&
         p != Protocol::LightSensor;
}

}  // namespace mozart