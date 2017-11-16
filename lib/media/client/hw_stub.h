// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/types.h>

// Currently this file is a stub implementation of the interface in audio.h.
// TODO(mpuryear): Convert the stub implementation. Enumerate audio playback
// devices, connect to the selected devices, and play audio to said devices.
namespace media_client {

constexpr int kStubNumDevices = 1;

extern const char* const kStubDevId;
extern const char* const kStubDevName;

constexpr int kStubPreferredRate = 48000;
constexpr int kStubPreferredNumChans = 2;
constexpr int kStubPreferredBufferSize = 480;
}  // namespace media_client
