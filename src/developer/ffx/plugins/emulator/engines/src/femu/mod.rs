// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The aemu module encapsulates the interactions with the emulator instance
//! started via the Android emulator, aemu.

use crate::serialization::SerializingEngine;
use anyhow::Result;
use async_trait::async_trait;
use ffx_emulator_common::config::FfxConfigWrapper;
use ffx_emulator_config::{EmulatorConfiguration, EmulatorEngine};
use serde::{Deserialize, Serialize};

#[derive(Default, Deserialize, Serialize)]
pub struct FemuEngine {
    #[serde(skip)]
    pub(crate) _ffx_config: FfxConfigWrapper,

    pub(crate) emulator_configuration: EmulatorConfiguration,
    pub(crate) _pid: i32,
}

#[async_trait]
impl EmulatorEngine for FemuEngine {
    async fn start(&mut self) -> Result<i32> {
        self.write_to_disk(
            &self.emulator_configuration.runtime.instance_directory,
            &self.emulator_configuration.runtime.log_level,
        )
        .await?;
        todo!()
    }
    fn show(&mut self) -> Result<()> {
        todo!()
    }
    fn shutdown(&mut self) -> Result<()> {
        todo!()
    }
    fn validate(&self) -> Result<()> {
        Ok(())
    }
}

#[async_trait]
impl SerializingEngine for FemuEngine {}
