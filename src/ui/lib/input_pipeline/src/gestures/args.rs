// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_zircon as zx;

pub(super) const SPURIOUS_TO_INTENTIONAL_MOTION_THRESHOLD_MM: f32 = 16.0 / 12.0;
pub(super) const MAX_SPURIOUS_TO_INTENTIONAL_SCROLL_THRESHOLD_MM: f32 = 5.0 * 16.0 / 12.0;
pub(super) const MAX_TAP_MOVEMENT_IN_MM: f32 = 2.0;
pub(super) const TAP_TIMEOUT: zx::Duration = zx::Duration::from_millis(1200);
pub(super) const MAX_SCROLL_DIRECTION_SKEW_DEGREES: f32 = 40.0;
