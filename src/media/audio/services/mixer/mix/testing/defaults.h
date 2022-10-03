// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_SERVICES_MIXER_MIX_TESTING_DEFAULTS_H_
#define SRC_MEDIA_AUDIO_SERVICES_MIXER_MIX_TESTING_DEFAULTS_H_

#include <memory>

#include "src/media/audio/lib/clock/clock.h"
#include "src/media/audio/lib/clock/clock_snapshot.h"
#include "src/media/audio/lib/clock/clock_synchronizer.h"
#include "src/media/audio/lib/clock/unreadable_clock.h"
#include "src/media/audio/lib/format2/format.h"
#include "src/media/audio/services/mixer/common/basic_types.h"
#include "src/media/audio/services/mixer/mix/mix_job_context.h"
#include "src/media/audio/services/mixer/mix/simple_packet_queue_producer_stage.h"

namespace media_audio {

// Can be used when any MixJobContext will do.
MixJobContext& DefaultCtx();

// A set of clock snapshots that can be used when any will do.
const ClockSnapshots& DefaultClockSnapshots();

// A reference clock to use when any clock will do.
// This clock is guaranteed to exist in `MixJobContext.clocks()` and `DefaultClockSnapshots()`.
std::shared_ptr<Clock> DefaultClock();
UnreadableClock DefaultUnreadableClock();

// A noop clock synchronizer to use when any will do.
std::shared_ptr<ClockSynchronizer> DefaultClockSync();

// A TimelineFunction that defines t=0 to be the presentation time for frame 0.
TimelineFunction DefaultPresentationTimeToFracFrame(const Format& format);

// Constructs a SimplePacketQueueProducerStage with the given `format`, DefaultClock(), and optional
// `name`. The returned queue can be mutated via its `push` and `clear` methods.
std::shared_ptr<SimplePacketQueueProducerStage> MakeDefaultPacketQueue(
    const Format& format, std::string_view name = "DefaultPacketQueue");

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_SERVICES_MIXER_MIX_TESTING_DEFAULTS_H_
