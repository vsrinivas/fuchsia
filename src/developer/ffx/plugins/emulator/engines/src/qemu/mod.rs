// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The qemu module encapsulates the interactions with the emulator instance
//! started via the QEMU emulator.
//! Some of the functions related to QEMU are pub(crate) to allow reuse by
//! femu module since femu is a wrapper around an older version of QEMU.

use crate::serialization::SerializingEngine;
use anyhow::Result;
use async_trait::async_trait;
use ffx_emulator_common::{config::FfxConfigWrapper, process};
use ffx_emulator_config::{EmulatorConfiguration, EmulatorEngine, EngineType};
use serde::{Deserialize, Serialize};

pub(crate) mod qemu_base;
pub(crate) use qemu_base::QemuBasedEngine;

#[derive(Clone, Debug, Default, Deserialize, PartialEq, Serialize)]
pub struct QemuEngine {
    #[serde(skip)]
    pub(crate) ffx_config: FfxConfigWrapper,

    pub(crate) emulator_configuration: EmulatorConfiguration,
    pub(crate) pid: u32,
    pub(crate) engine_type: EngineType,

    pub(crate) args: Vec<String>,
    pub(crate) kernel_args: Vec<String>,
}

#[async_trait]
impl EmulatorEngine for QemuEngine {
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

        let instance_directory = self.emulator_configuration.runtime.instance_directory.clone();
        self.write_to_disk(&instance_directory).await?;
        todo!()
    }
    fn show(&self) {
        println!("{:#?}", self.emulator_configuration);
    }
    fn shutdown(&mut self) -> Result<()> {
        todo!()
    }
    fn validate(&self) -> Result<()> {
        self.check_required_files(&self.emulator_configuration.guest)
    }
    fn engine_type(&self) -> EngineType {
        self.engine_type
    }

    fn is_running(&self) -> bool {
        process::is_running(self.pid)
    }
}

#[async_trait]
impl SerializingEngine for QemuEngine {}

impl QemuBasedEngine for QemuEngine {}
