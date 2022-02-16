// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The qemu module encapsulates the interactions with the emulator instance
//! started via the QEMU emulator.
//! Some of the functions related to QEMU are pub(crate) to allow reuse by
//! femu module since femu is a wrapper around an older version of QEMU.

use crate::serialization::SerializingEngine;
use anyhow::{bail, Result};
use async_trait::async_trait;
use ffx_emulator_common::{
    config::{FfxConfigWrapper, QEMU_TOOL},
    process,
};
use ffx_emulator_config::{EmulatorConfiguration, EmulatorEngine, EngineType, PointingDevice};
use fidl_fuchsia_developer_bridge as bridge;
use serde::{Deserialize, Serialize};
use std::{env, path::PathBuf, process::Command};

pub(crate) mod qemu_base;
pub(crate) use qemu_base::QemuBasedEngine;

#[derive(Clone, Debug, Default, Deserialize, PartialEq, Serialize)]
pub struct QemuEngine {
    #[serde(skip)]
    pub(crate) ffx_config: FfxConfigWrapper,

    pub(crate) emulator_configuration: EmulatorConfiguration,
    pub(crate) pid: u32,
    pub(crate) engine_type: EngineType,
}

#[async_trait]
impl EmulatorEngine for QemuEngine {
    async fn start(&mut self, proxy: &bridge::TargetCollectionProxy) -> Result<i32> {
        self.emulator_configuration.guest = self
            .stage_image_files(
                &self.emulator_configuration.runtime.name,
                &self.emulator_configuration.guest,
                &self.emulator_configuration.device,
                &self.ffx_config,
            )
            .await
            .expect("could not stage image files");

        // TODO(fxbug.dev/86737): Find the emulator executable using ffx_config::get_host_tool().
        // This is a workaround until the ffx_config::get_host_tool works.
        let sdk = ffx_config::get_sdk().await?;
        let sdk_root = sdk.get_path_prefix();
        let backup_qemu = match env::consts::OS {
            "linux" => {
                sdk_root.join("../../prebuilt/third_party/qemu/linux-x64/bin/qemu-system-x86_64")
            }
            "macos" => {
                sdk_root.join("../../prebuilt/third_party/qemu/mac-x64/bin/qemu-system-x86_64")
            }
            _ => panic!("Sorry. {} is not supported.", env::consts::OS),
        }
        .canonicalize()?;

        let qemu = match self.ffx_config.get_host_tool(QEMU_TOOL).await {
            Ok(qemu_path) => qemu_path,
            Err(e) => {
                println!("Need to fix {:?}", e);
                backup_qemu
            }
        };

        return self.run(&qemu, proxy).await;
    }
    fn show(&self) {
        println!("{:#?}", self.emulator_configuration);
    }
    async fn stop(&self, proxy: &bridge::TargetCollectionProxy) -> Result<()> {
        // Extract values from the self here, since there are sharing issues with trying to call
        // shutdown_emulator from another thread.
        let target_id = &self.emulator_configuration.runtime.name;
        Self::stop_emulator(self.is_running(), self.get_pid(), target_id, proxy).await
    }
    fn validate(&self) -> Result<()> {
        if self.emulator_configuration.device.pointing_device == PointingDevice::Touch {
            bail!("Touchscreen as a pointing device is not available on Qemu.");
        }
        self.validate_network_flags(&self.emulator_configuration)
            .and_then(|()| self.check_required_files(&self.emulator_configuration.guest))
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

impl QemuBasedEngine for QemuEngine {
    fn set_pid(&mut self, pid: u32) {
        self.pid = pid;
    }

    fn get_pid(&self) -> u32 {
        self.pid
    }

    fn emu_config(&self) -> &EmulatorConfiguration {
        return &self.emulator_configuration;
    }

    fn emu_config_mut(&mut self) -> &mut EmulatorConfiguration {
        return &mut self.emulator_configuration;
    }

    /// Build the Command to launch Qemu emulator running Fuchsia.
    fn build_emulator_cmd(&self, emu_binary: &PathBuf) -> Result<Command> {
        let mut cmd = Command::new(&emu_binary);
        cmd.args(&self.emulator_configuration.flags.args);
        let extra_args = self
            .emulator_configuration
            .flags
            .kernel_args
            .iter()
            .map(|x| x.to_string())
            .collect::<Vec<_>>()
            .join(" ");
        if extra_args.len() > 0 {
            cmd.args(["-append", &extra_args]);
        }
        if self.emulator_configuration.flags.envs.len() > 0 {
            cmd.envs(&self.emulator_configuration.flags.envs);
        }
        Ok(cmd)
    }
}
