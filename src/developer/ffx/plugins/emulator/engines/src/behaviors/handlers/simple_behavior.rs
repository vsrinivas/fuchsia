// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::behaviors::FemuBehavior;
use ffx_emulator_config::{Behavior, BehaviorTrait, FemuData};

pub struct SimpleBehavior<'a> {
    pub behavior: &'a Behavior,
}

impl BehaviorTrait for SimpleBehavior<'_> {}

impl FemuBehavior for SimpleBehavior<'_> {
    fn data(&self) -> Option<&FemuData> {
        self.behavior.data.femu.as_ref()
    }
}
