// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_LIB_CLOCK_CLONE_MONO_H_
#define SRC_MEDIA_AUDIO_LIB_CLOCK_CLONE_MONO_H_

#include <lib/zx/clock.h>

namespace media::audio::clock {

void CloneMonotonicInto(zx::clock* clock_out, bool adjustable = false);

zx::clock AdjustableCloneOfMonotonic();
zx::clock CloneOfMonotonic();

}  // namespace media::audio::clock

#endif  // SRC_MEDIA_AUDIO_LIB_CLOCK_CLONE_MONO_H_
