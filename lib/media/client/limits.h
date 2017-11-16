// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

namespace media_client {

// Soft limits only - can be expanded if needed (except kMinNumChannels).
constexpr int kMinSampleRate = 8000;
constexpr int kMaxSampleRate = 48000;
constexpr int kMinNumChannels = 1;
constexpr int kMaxNumChannels = 2;

}  // namespace media_client
