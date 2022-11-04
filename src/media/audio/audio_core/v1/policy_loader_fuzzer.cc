// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/v1/policy_loader.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  std::string json(reinterpret_cast<const char*>(data), size);
  ::media::audio::PolicyLoader::ParseConfig(json.c_str());
  return 0;
}
