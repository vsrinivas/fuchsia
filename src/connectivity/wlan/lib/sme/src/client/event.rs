// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_zircon::{self as zx, prelude::DurationNum};

use ieee80211::{Bssid, MacAddr};
use wlan_common::timer::TimeoutDuration;

/// Amount of time in milliseconds an entire RSNA establishment is allowed to take.
/// Exceeding this time will result in a failure to establish the RSNA.
pub const RSNA_COMPLETION_TIMEOUT_MILLIS: i64 = 8700;

/// Amount of time in milliseconds the supplicant or authenticator has to respond
/// to a frame used to establish an RSNA, e.g., an EAPOL key frame.
/// A delayed response exceeding this time will result in a failure to establish
/// the RSNA.
pub const RSNA_RESPONSE_TIMEOUT_MILLIS: i64 = 4000;

/// Amount of time in milliseconds the supplicant or authenticator will wait for a
/// response before retransmitting the last transmitted frame for establishing
/// the RSNA, e.g., the last transmitted EAPOL key frame. The implementation of
/// the RSNA decides how many retries are allowed. This timeout never triggers
/// a failure to establish the RSNA.
pub const RSNA_RETRANSMISSION_TIMEOUT_MILLIS: i64 = 200;

/// Amount of time in milliseconds a participant in the SAE handshake will wait for
/// a response before restransmitting the last transmitted SAE message.
pub const SAE_RETRANSMISSION_TIMEOUT_MILLIS: i64 = 1000;

pub const INSPECT_PULSE_CHECK_MINUTES: i64 = 1;
pub const INSPECT_PULSE_PERSIST_MINUTES: i64 = 5;

#[derive(Debug, Clone)]
pub enum Event {
    RsnaCompletionTimeout(RsnaCompletionTimeout),
    RsnaResponseTimeout(RsnaResponseTimeout),
    RsnaRetransmissionTimeout(RsnaRetransmissionTimeout),
    InspectPulseCheck(InspectPulseCheck),
    /// From startup, periodically schedule an event to persist the Inspect pulse data
    InspectPulsePersist(InspectPulsePersist),
    SaeTimeout(SaeTimeout),
}
impl From<RsnaCompletionTimeout> for Event {
    fn from(timeout: RsnaCompletionTimeout) -> Self {
        Event::RsnaCompletionTimeout(timeout)
    }
}
impl From<RsnaResponseTimeout> for Event {
    fn from(timeout: RsnaResponseTimeout) -> Self {
        Event::RsnaResponseTimeout(timeout)
    }
}
impl From<RsnaRetransmissionTimeout> for Event {
    fn from(timeout: RsnaRetransmissionTimeout) -> Self {
        Event::RsnaRetransmissionTimeout(timeout)
    }
}
impl From<InspectPulseCheck> for Event {
    fn from(this: InspectPulseCheck) -> Self {
        Event::InspectPulseCheck(this)
    }
}
impl From<InspectPulsePersist> for Event {
    fn from(this: InspectPulsePersist) -> Self {
        Event::InspectPulsePersist(this)
    }
}
impl From<SaeTimeout> for Event {
    fn from(this: SaeTimeout) -> Self {
        Event::SaeTimeout(this)
    }
}

#[derive(Debug, Clone)]
pub struct RsnaCompletionTimeout;
impl TimeoutDuration for RsnaCompletionTimeout {
    fn timeout_duration(&self) -> zx::Duration {
        RSNA_COMPLETION_TIMEOUT_MILLIS.millis()
    }
}

#[derive(Debug, Clone)]
pub struct RsnaResponseTimeout;
impl TimeoutDuration for RsnaResponseTimeout {
    fn timeout_duration(&self) -> zx::Duration {
        RSNA_RESPONSE_TIMEOUT_MILLIS.millis()
    }
}

#[derive(Debug, Clone)]
pub struct RsnaRetransmissionTimeout {
    pub bssid: Bssid,
    pub sta_addr: MacAddr,
}
impl TimeoutDuration for RsnaRetransmissionTimeout {
    fn timeout_duration(&self) -> zx::Duration {
        RSNA_RETRANSMISSION_TIMEOUT_MILLIS.millis()
    }
}

#[derive(Debug, Clone)]
pub struct InspectPulseCheck;
impl TimeoutDuration for InspectPulseCheck {
    fn timeout_duration(&self) -> zx::Duration {
        INSPECT_PULSE_CHECK_MINUTES.minutes()
    }
}

#[derive(Debug, Clone)]
pub struct InspectPulsePersist;
impl TimeoutDuration for InspectPulsePersist {
    fn timeout_duration(&self) -> zx::Duration {
        INSPECT_PULSE_PERSIST_MINUTES.minutes()
    }
}

#[derive(Debug, Clone)]
pub struct SaeTimeout(pub u64);
impl TimeoutDuration for SaeTimeout {
    fn timeout_duration(&self) -> zx::Duration {
        SAE_RETRANSMISSION_TIMEOUT_MILLIS.millis()
    }
}
