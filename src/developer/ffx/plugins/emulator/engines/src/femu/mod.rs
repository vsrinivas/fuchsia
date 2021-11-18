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

use crate::qemu::qemu_base::QemuBasedEngine;

#[derive(Default, Deserialize, Serialize)]
pub struct FemuEngine {
    #[serde(skip)]
    pub(crate) ffx_config: FfxConfigWrapper,

    pub(crate) emulator_configuration: EmulatorConfiguration,
    pub(crate) _pid: i32,
}

#[async_trait]
impl EmulatorEngine for FemuEngine {
    async fn start(&mut self) -> Result<i32> {
        self.emulator_configuration.guest = self
            .stage_image_files(
                &self.emulator_configuration.runtime.name,
                &self.emulator_configuration.guest,
                &self.emulator_configuration.device,
                &self.ffx_config,
            )
            .await
            .expect("could not stage image files");
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
        self.check_required_files(&self.emulator_configuration.guest)?;
        Ok(())
    }
}

#[async_trait]
impl SerializingEngine for FemuEngine {}
impl QemuBasedEngine for FemuEngine {}
