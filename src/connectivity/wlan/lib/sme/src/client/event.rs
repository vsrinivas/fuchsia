// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use eapol;
use fuchsia_zircon::{self as zx, prelude::DurationNum};

use crate::timer::TimeoutDuration;
use crate::MacAddr;

pub const ESTABLISHING_RSNA_TIMEOUT_SECONDS: i64 = 3;
pub const KEY_FRAME_EXCHANGE_TIMEOUT_MILLIS: i64 = 200;
pub const KEY_FRAME_EXCHANGE_MAX_ATTEMPTS: u32 = 3;

#[derive(Debug, Clone)]
pub enum Event {
    EstablishingRsnaTimeout,
    KeyFrameExchangeTimeout {
        bssid: MacAddr,
        sta_addr: MacAddr,
        frame: eapol::KeyFrame,
        attempt: u32,
    },
}

impl TimeoutDuration for Event {
    fn timeout_duration(&self) -> zx::Duration {
        match self {
            Event::EstablishingRsnaTimeout => ESTABLISHING_RSNA_TIMEOUT_SECONDS.seconds(),
            Event::KeyFrameExchangeTimeout { .. } => KEY_FRAME_EXCHANGE_TIMEOUT_MILLIS.millis(),
        }
    }
}
