// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef AUDIO_UTILS_DURATION_H_
#define AUDIO_UTILS_DURATION_H_

#include <functional>
#include <variant>

namespace audio {
namespace utils {

using LoopingDoneCallback = std::function<bool()>;
using Duration = std::variant<float, LoopingDoneCallback>;
}  // namespace utils
}  // namespace audio

#endif  // AUDIO_UTILS_DURATION_H_
