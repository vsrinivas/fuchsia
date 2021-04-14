// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::core::TryFromU64;

/// Loop options for linear animations.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Loop {
    /// Play until the duration or end of work area of the animation.
    OneShot,
    /// Play until the duration or end of work area of the animation and
    /// then go back to the start (0 seconds).
    Loop,
    /// Play to the end of the duration/work area and then play back.
    PingPong,
}

impl Default for Loop {
    fn default() -> Self {
        Self::OneShot
    }
}

impl TryFromU64 for Loop {
    fn try_from(value: u64) -> Option<Self> {
        match value {
            0 => Some(Self::OneShot),
            1 => Some(Self::Loop),
            2 => Some(Self::PingPong),
            _ => None,
        }
    }
}
