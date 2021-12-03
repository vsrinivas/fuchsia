// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// A Behavior for setting up the MacOS Hypervisor Framework (HFV) feature. Femu on MacOS can't use
/// KVM for acceleration, so HVF is the MacOS alternative. 

use crate::behaviors::FemuBehavior;
use anyhow::Result;
use ffx_emulator_config::{
    AccelerationMode, Behavior, BehaviorTrait, EmulatorConfiguration, FemuData, FilterResult,
};

pub struct HvfBehavior<'a> {
    pub behavior: &'a Behavior,
}

impl BehaviorTrait for HvfBehavior<'_> {
    fn filter(&self, config: &EmulatorConfiguration) -> Result<FilterResult> {
        if config.host.acceleration == AccelerationMode::None {
            return Ok(FilterResult::Reject(
                "HvfBehavior rejected since acceleration is disabled by the --accel flag."
                    .to_string(),
            ));
        } else if config.host.acceleration == AccelerationMode::Auto {
            // TODO(fxbug.dev/50963): Need to reject the Auto case when HVF is not on the host.
            // For now it accepts for Auto.
        }
        if std::env::consts::OS != "macos" {
            return Ok(FilterResult::Reject("HvfBehavior rejected since OS is not MacOS.".to_string()));
        }
        Ok(FilterResult::Accept)
    }
}

impl FemuBehavior for HvfBehavior<'_> {
    fn data(&self) -> Option<&FemuData> {
        self.behavior.data.femu.as_ref()
    }
}
