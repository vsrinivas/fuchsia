// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// A Behavior for setting up Kernel-based Virtualization (KVM) acceleration. KVM for Femu is only
/// supported for Linux hosts.
use crate::behaviors::FemuBehavior;
use anyhow::Result;
use ffx_emulator_config::{
    AccelerationMode, Behavior, BehaviorTrait, EmulatorConfiguration, FemuData, FilterResult,
};

pub struct KvmBehavior<'a> {
    pub behavior: &'a Behavior,
}

impl BehaviorTrait for KvmBehavior<'_> {
    fn filter(&self, config: &EmulatorConfiguration) -> Result<FilterResult> {
        if config.host.acceleration == AccelerationMode::None {
            return Ok(FilterResult::Reject(
                "KvmBehavior rejected since acceleration is disabled by the --accel flag."
                    .to_string(),
            ));
        } else if config.host.acceleration == AccelerationMode::Auto {
            // TODO(fxbug.dev/50963): Need to reject the Auto case when KVM is not on the host.
            // For now it accepts for Auto.
        }
        if std::env::consts::OS != "linux" {
            return Ok(FilterResult::Reject(
                "KvmBehavior rejected since OS is not Linux.".to_string(),
            ));
        }
        Ok(FilterResult::Accept)
    }
}

impl FemuBehavior for KvmBehavior<'_> {
    fn data(&self) -> Option<&FemuData> {
        self.behavior.data.femu.as_ref()
    }
}
