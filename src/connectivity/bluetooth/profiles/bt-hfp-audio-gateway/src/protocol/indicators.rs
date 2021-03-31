// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::procedure::AgUpdate;
use at_commands as at;

#[derive(Debug, Default, Clone, Copy)]
pub struct Indicators {
    pub service: bool,
    pub call: bool,
    pub callsetup: (),
    pub callheld: (),
    pub signal: u8,
    pub roam: bool,
    pub battchg: u8,
}

/// The supported phone status update indicators.
#[derive(Clone, Copy, Debug, PartialEq)]
pub enum Indicator {
    Service(u8),
    Call(u8),
    CallSetup(u8),
    CallHeld(u8),
    Signal(u8),
    Roam(u8),
    BatteryLevel(u8),
}

impl From<Indicator> for at::Response {
    fn from(src: Indicator) -> at::Response {
        match src {
            Indicator::Service(v) => {
                at::Response::Success(at::Success::Ciev { ind: 1, value: v as i64 })
            }
            Indicator::Call(v) => {
                at::Response::Success(at::Success::Ciev { ind: 2, value: v as i64 })
            }
            Indicator::CallSetup(v) => {
                at::Response::Success(at::Success::Ciev { ind: 3, value: v as i64 })
            }
            Indicator::CallHeld(v) => {
                at::Response::Success(at::Success::Ciev { ind: 4, value: v as i64 })
            }
            Indicator::Signal(v) => {
                at::Response::Success(at::Success::Ciev { ind: 5, value: v as i64 })
            }
            Indicator::Roam(v) => {
                at::Response::Success(at::Success::Ciev { ind: 6, value: v as i64 })
            }
            Indicator::BatteryLevel(v) => {
                at::Response::Success(at::Success::Ciev { ind: 7, value: v as i64 })
            }
        }
    }
}

impl From<Indicator> for AgUpdate {
    fn from(src: Indicator) -> Self {
        Self::PhoneStatusIndicator(src)
    }
}
