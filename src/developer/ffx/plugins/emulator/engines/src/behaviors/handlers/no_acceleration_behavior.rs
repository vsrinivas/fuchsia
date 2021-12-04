// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// A Behavior for running Femu without acceleration enabled. Not a recommended choice, as Femu
/// runs prohibitively slow without acceleration, but necessary for hosts that don't have KVM/HVF
/// available. Enabled by setting `--accel none` on the command line, or `--accel auto` on hosts
/// where acceleration software is not installed (see TODO comment below).
use crate::behaviors::FemuBehavior;
use anyhow::Result;
use ffx_emulator_config::{
    AccelerationMode, Behavior, BehaviorTrait, EmulatorConfiguration, FemuData, FilterResult,
};

pub struct NoAccelerationBehavior<'a> {
    pub behavior: &'a Behavior,
}

impl BehaviorTrait for NoAccelerationBehavior<'_> {
    fn filter(&self, config: &EmulatorConfiguration) -> Result<FilterResult> {
        if config.host.acceleration == AccelerationMode::Hyper {
            return Ok(FilterResult::Reject(
                "NoAccelerationBehavior rejected since acceleration is enabled by the --accel flag."
                    .to_string(),
            ));
        } else if config.host.acceleration == AccelerationMode::Auto {
            // TODO(fxbug.dev/50963): Need to handle this case when KVM is not installed on the host
            return Ok(FilterResult::Reject(
                "NoAccelerationBehavior rejected since acceleration is enabled by the --accel flag."
                    .to_string(),
            ));
        }
        Ok(FilterResult::Accept)
    }
}

impl FemuBehavior for NoAccelerationBehavior<'_> {
    fn data(&self) -> Option<&FemuData> {
        self.behavior.data.femu.as_ref()
    }
}
