// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The aemu module encapsulates the interactions with the emulator instance
//! started via the Android emulator, aemu.

use anyhow::Result;
use async_trait::async_trait;
use ffx_emulator_common::config::FfxConfigWrapper;
use ffx_emulator_config::{EmulatorConfiguration, EmulatorEngine};

pub struct FemuEngine {}

#[async_trait]
impl EmulatorEngine for FemuEngine {
    fn new() -> Self {
        FemuEngine {}
    }
    async fn initialize(
        &mut self,
        _config: &FfxConfigWrapper,
        _emulator_configuration: EmulatorConfiguration,
    ) -> Result<()> {
        todo!()
    }
    async fn start(&mut self) -> Result<i32> {
        todo!()
    }
    fn show(&mut self) -> Result<()> {
        todo!()
    }
    fn shutdown(&mut self) -> Result<()> {
        todo!()
    }
}
