// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/edid/edid.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>

// fuzz_target.cc
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  edid::Edid edid;
  if (size > UINT16_MAX) {
    return 0;
  }

  const char* err_msg;
  if (!edid.Init(data, static_cast<uint16_t>(size), &err_msg)) {
    return 0;
  }

  // Use a static variable to introduce optimization-preventing side-effects.
  static size_t count = 0;
  count += edid.is_hdmi() ? 0 : 1;
  for (auto it = edid::timing_iterator(&edid); it.is_valid(); ++it) {
    count++;
  }
  for (auto it = edid::audio_data_block_iterator(&edid); it.is_valid(); ++it) {
    count--;
  }
  edid.Print([](const char* str) {});

  return 0;
}
