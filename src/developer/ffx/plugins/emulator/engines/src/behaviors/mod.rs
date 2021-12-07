// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod femu_behavior;
mod handlers;

use anyhow::{anyhow, Result};
use ffx_emulator_config::{Behavior, BehaviorTrait};
use handlers::{
    hvf_behavior::HvfBehavior, kvm_behavior::KvmBehavior, mouse_behavior::MouseBehavior,
    no_acceleration_behavior::NoAccelerationBehavior, simple_behavior::SimpleBehavior,
    touch_behavior::TouchBehavior,
};
use serde::{Deserialize, Serialize};
use std::str::FromStr;

pub use femu_behavior::FemuBehavior;

#[derive(Clone, Debug, Deserialize, PartialEq, Serialize)]
pub(crate) enum BehaviorHandler {
    SimpleBehavior,
    KvmBehavior,
    HvfBehavior,
    MouseBehavior,
    NoAccelerationBehavior,
    TouchBehavior,
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

pub fn get_handler_for_behavior(behavior: &Behavior) -> Result<Box<dyn BehaviorTrait + '_>> {
    let result = BehaviorHandler::from_str(&behavior.handler);
    let behavior_handler;
    match result {
        Ok(handler) => {
            behavior_handler = handler;
        }
        Err(error) => return Err(anyhow!(error)),
    }
    let b = match behavior_handler {
        BehaviorHandler::HvfBehavior => {
            Box::new(HvfBehavior { behavior: &behavior }) as Box<dyn BehaviorTrait>
        }
        BehaviorHandler::MouseBehavior => {
            Box::new(MouseBehavior { behavior: &behavior }) as Box<dyn BehaviorTrait>
        }
        BehaviorHandler::TouchBehavior => {
            Box::new(TouchBehavior { behavior: &behavior }) as Box<dyn BehaviorTrait>
        }
        BehaviorHandler::KvmBehavior => {
            Box::new(KvmBehavior { behavior: &behavior }) as Box<dyn BehaviorTrait>
        }
        BehaviorHandler::NoAccelerationBehavior => {
            Box::new(NoAccelerationBehavior { behavior: &behavior }) as Box<dyn BehaviorTrait>
        }
        BehaviorHandler::SimpleBehavior => {
            Box::new(SimpleBehavior { behavior: &behavior }) as Box<dyn BehaviorTrait>
        }
    };
    return Ok(b);
}
