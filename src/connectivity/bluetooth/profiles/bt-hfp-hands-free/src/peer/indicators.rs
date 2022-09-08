// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use at_commands as at;
use core::fmt::Debug;

// A single indicator status + value.
#[derive(Clone, Copy, Debug)]
pub struct Indicator<T: Clone + Copy + Debug> {
    /// Whether this indicator is enabled or not.
    pub enabled: bool,
    /// The value of the indicator.
    pub value: Option<T>,
}

impl<T: Clone + Copy + Debug> Default for Indicator<T> {
    fn default() -> Self {
        Self { enabled: true, value: None }
    }
}

impl<T: Clone + Copy + Debug> Indicator<T> {
    pub fn set_if_enabled(&mut self, val: T) {
        if self.enabled {
            self.value = Some(val);
        }
    }
}

// A collection of indicators supported by the AG.
#[derive(Default, Clone)]
pub struct AgIndicators {
    pub service: Indicator<bool>,
    pub call: Indicator<bool>,
    pub callsetup: Indicator<u8>,
    pub callheld: Indicator<u8>,
    pub signal: Indicator<u8>,
    pub roam: Indicator<bool>,
    pub battchg: Indicator<u8>,
}

impl AgIndicators {
    /// Update the Indicator values from the Cind response received from the AG.
    pub fn update_indicator_values(&mut self, response: &at::Success) {
        match response {
            at::Success::Cind { service, call, callsetup, callheld, signal, roam, battchg } => {
                self.service.set_if_enabled(*service);
                self.call.set_if_enabled(*call);
                self.callsetup.set_if_enabled(*callsetup as u8);
                self.callheld.set_if_enabled(*callheld as u8);
                self.signal.set_if_enabled(*signal as u8);
                self.roam.set_if_enabled(*roam);
                self.battchg.set_if_enabled(*battchg as u8);
            }
            _ => {}
        }
    }

    #[cfg(test)]
    /// Add default values to indicators.
    pub fn set_default_values(&mut self) {
        self.service.value = Some(false);
        self.call.value = Some(false);
        self.callsetup.value = Some(0);
        self.callheld.value = Some(0);
        self.signal.value = Some(0);
        self.roam.value = Some(false);
        self.battchg.value = Some(0);
    }
}
