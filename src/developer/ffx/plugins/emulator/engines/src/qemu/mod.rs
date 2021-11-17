// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The qemu module encapsulates the interactions with the emulator instance
//! started via the QEMU emulator.
//! Some of the functions related to QEMU are pub(crate) to allow reuse by
//! aemu module since aemu is a wrapper around an older version of QEMU.

use anyhow::Result;
use async_trait::async_trait;
use ffx_emulator_common::config::FfxConfigWrapper;
use ffx_emulator_config::{EmulatorConfiguration, EmulatorEngine};
use serde::{Deserialize, Serialize};

#[derive(Default, Deserialize, Serialize)]
pub struct QemuEngine {
    #[serde(skip)]
    pub(crate) _ffx_config: FfxConfigWrapper,

    pub(crate) _emulator_configuration: EmulatorConfiguration,
    pub(crate) _pid: i32,
}

#[async_trait]
impl EmulatorEngine for QemuEngine {
    async fn start(&mut self) -> Result<i32> {
        todo!()
    }
    fn show(&mut self) -> Result<()> {
        todo!()
    }
    fn shutdown(&mut self) -> Result<()> {
        todo!()
    }
    fn validate(&self) -> Result<()> {
        todo!()
    }
}
