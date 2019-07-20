// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_zircon::{self as zx, prelude::DurationNum};

use crate::client::info::ConnectionMilestoneInfo;
use crate::timer::TimeoutDuration;
use crate::MacAddr;

pub const ESTABLISHING_RSNA_TIMEOUT_SECONDS: i64 = 3;
pub const KEY_FRAME_EXCHANGE_TIMEOUT_MILLIS: i64 = 200;
pub const KEY_FRAME_EXCHANGE_MAX_ATTEMPTS: u32 = 3;

#[derive(Debug, Clone)]
pub enum Event {
    EstablishingRsnaTimeout(EstablishingRsnaTimeout),
    KeyFrameExchangeTimeout(KeyFrameExchangeTimeout),
    ConnectionMilestone(ConnectionMilestoneInfo),
}
impl From<EstablishingRsnaTimeout> for Event {
    fn from(timeout: EstablishingRsnaTimeout) -> Self {
        Event::EstablishingRsnaTimeout(timeout)
    }
}
impl From<KeyFrameExchangeTimeout> for Event {
    fn from(timeout: KeyFrameExchangeTimeout) -> Self {
        Event::KeyFrameExchangeTimeout(timeout)
    }
}

#[derive(Debug, Clone)]
pub struct EstablishingRsnaTimeout;
impl TimeoutDuration for EstablishingRsnaTimeout {
    fn timeout_duration(&self) -> zx::Duration {
        ESTABLISHING_RSNA_TIMEOUT_SECONDS.seconds()
    }
}

#[derive(Debug, Clone)]
pub struct KeyFrameExchangeTimeout {
    pub bssid: MacAddr,
    pub sta_addr: MacAddr,
    pub frame: eapol::KeyFrameBuf,
    pub attempt: u32,
}
impl TimeoutDuration for KeyFrameExchangeTimeout {
    fn timeout_duration(&self) -> zx::Duration {
        KEY_FRAME_EXCHANGE_TIMEOUT_MILLIS.millis()
    }
}
