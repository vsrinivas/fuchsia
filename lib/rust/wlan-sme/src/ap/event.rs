// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_zircon::{self as zx, prelude::DurationNum};

use crate::MacAddr;

pub const KEY_EXCHANGE_TIMEOUT_SECONDS: i64 = 2;
pub const KEY_EXCHANGE_MAX_ATTEMPTS: u32 = 4;

#[derive(Debug, Clone)]
pub struct Event {
    pub addr: MacAddr,
    pub event: ClientEvent,
}

impl Event {
    pub fn timeout_duration(&self) -> zx::Duration {
        self.event.timeout_duration()
    }
}

#[derive(Debug, Clone)]
pub enum ClientEvent {
    KeyExchangeTimeout { attempt: u32 },
}

impl ClientEvent {
    pub fn timeout_duration(&self) -> zx::Duration {
        match self {
            ClientEvent::KeyExchangeTimeout { .. } => KEY_EXCHANGE_TIMEOUT_SECONDS.seconds(),
        }
    }
}