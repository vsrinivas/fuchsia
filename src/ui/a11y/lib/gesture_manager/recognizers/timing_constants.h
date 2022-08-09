// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_GESTURE_MANAGER_RECOGNIZERS_TIMING_CONSTANTS_H_
#define SRC_UI_A11Y_LIB_GESTURE_MANAGER_RECOGNIZERS_TIMING_CONSTANTS_H_

#include "lib/zx/time.h"

namespace a11y {

// Max time between finger-down and finger-up events to be considered a tap.
inline constexpr zx::duration kMaxTapDuration = zx::msec(300);

// Max time between the individual taps of a multi-tap gesture.
//
// See also `kMaxTimeBetweenMultifingerTaps`.
//
// TODO(fxbug.dev/105911): consolidate this and kMaxTimeBetweenMultifingerTaps.
inline constexpr zx::duration kMaxTimeBetweenTaps = zx::msec(300);

// Max time between the individual taps of a *multi-finger* multi-tap gesture.
//
// See also `kMaxTimeBetweenTaps`.
inline constexpr zx::duration kMaxTimeBetweenMultifingerTaps = zx::msec(250);

// Max time between the first and second fingers' `DOWN` events to be considered
// a two-finger drag.
inline constexpr zx::duration kMaxSecondFingerDownDelay = zx::msec(300);

// Max duration to be considered a swipe.
//
// The same value is used for one-finger swipes and multi-finger swipes.
inline constexpr zx::duration kMaxSwipeDuration = zx::msec(500);

// Min duration to be considered a drag.
//
// This delay is intended to ensure behavioral consistency with other screen readers.
inline constexpr zx::duration kMinDragDuration = zx::msec(500);

}  // namespace a11y

#endif  // SRC_UI_A11Y_LIB_GESTURE_MANAGER_RECOGNIZERS_TIMING_CONSTANTS_H_
