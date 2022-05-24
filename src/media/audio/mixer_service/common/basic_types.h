// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_MIXER_SERVICE_COMMON_BASIC_TYPES_H_
#define SRC_MEDIA_AUDIO_MIXER_SERVICE_COMMON_BASIC_TYPES_H_

#include <fidl/fuchsia.audio.mixer/cpp/natural_types.h>

#include <limits>

#include "src/media/audio/lib/clock/audio_clock.h"
#include "src/media/audio/lib/format/constants.h"
#include "src/media/audio/lib/format2/format.h"
#include "src/media/audio/lib/timeline/timeline_function.h"
#include "src/media/audio/lib/timeline/timeline_rate.h"

namespace media_audio_mixer_service {

// FIDL IDs.
using NodeId = uint64_t;
using ThreadId = uint64_t;
using GainControlId = uint64_t;

// This ID shall not be used.
constexpr uint64_t kInvalidId = fuchsia_audio_mixer::kInvalidId;

// This ID is used by the GlobalTaskQueue to mean "any thread allowed".
constexpr uint64_t kAnyThreadId = std::numeric_limits<uint64_t>::max();
static_assert(kAnyThreadId != kInvalidId);

// Alias common types into this namespace.
using AudioClock = ::media::audio::AudioClock;
using Format = ::media_audio::Format;
using Fixed = ::media::audio::Fixed;
using TimelineFunction = ::media::TimelineFunction;
using TimelineRate = ::media::TimelineRate;

}  // namespace media_audio_mixer_service

#endif  // SRC_MEDIA_AUDIO_MIXER_SERVICE_COMMON_BASIC_TYPES_H_
