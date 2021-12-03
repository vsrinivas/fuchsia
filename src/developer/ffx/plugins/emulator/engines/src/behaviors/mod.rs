// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod femu_behavior;
mod handlers;

use serde::{Deserialize, Serialize};
use std::str::FromStr;

pub use femu_behavior::FemuBehavior;
pub use handlers::hvf_behavior::HvfBehavior;
pub use handlers::kvm_behavior::KvmBehavior;
pub use handlers::no_acceleration_behavior::NoAccelerationBehavior;
pub use handlers::simple_behavior::SimpleBehavior;

#[derive(Clone, Debug, Deserialize, PartialEq, Serialize)]
pub(crate) enum BehaviorHandler {
    SimpleBehavior,
    KvmBehavior,
    HvfBehavior,
    NoAccelerationBehavior,
}

impl FromStr for BehaviorHandler {
    type Err = std::string::String;
    fn from_str(text: &str) -> Result<Self, std::string::String> {
        let result = serde_json::from_str(&format!("\"{}\"", text));
        return match result {
            Err(e) => {
                return Err(format!(
                    "could not parse '{}' as a valid BehaviorHandler. \
                    Please check the help text for allowed values and try again: {:?}",
                    text, e
                ))
            }
            Ok(v) => Ok(v),
        };
    }
}
