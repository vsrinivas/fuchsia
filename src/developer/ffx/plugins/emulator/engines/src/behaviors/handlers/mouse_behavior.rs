// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// A Behavior for setting up Kernel-based Virtualization (KVM) acceleration. KVM for Femu is only
/// supported for Linux hosts.
use crate::behaviors::FemuBehavior;
use anyhow::Result;
use ffx_emulator_config::{
    Behavior, BehaviorTrait, EmulatorConfiguration, FemuData, FilterResult, PointingDevice,
};

pub struct MouseBehavior<'a> {
    pub behavior: &'a Behavior,
}

impl BehaviorTrait for MouseBehavior<'_> {
    fn filter(&self, config: &EmulatorConfiguration) -> Result<FilterResult> {
        if config.device.pointing_device != PointingDevice::Mouse {
            return Ok(FilterResult::Reject(format!(
                "MouseBehavior rejected since pointing_device is set to {:?}.",
                config.device.pointing_device
            )));
        }
        Ok(FilterResult::Accept)
    }
}

impl FemuBehavior for MouseBehavior<'_> {
    fn data(&self) -> Option<&FemuData> {
        self.behavior.data.femu.as_ref()
    }
}
