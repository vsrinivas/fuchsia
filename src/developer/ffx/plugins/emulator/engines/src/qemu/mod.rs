// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The qemu module encapsulates the interactions with the emulator instance
//! started via the QEMU emulator.
//! Some of the functions related to QEMU are pub(crate) to allow reuse by
//! femu module since femu is a wrapper around an older version of QEMU.

use crate::serialization::SerializingEngine;
use anyhow::{bail, Context, Result};
use async_trait::async_trait;
use ffx_emulator_common::{
    config::{FfxConfigWrapper, QEMU_TOOL},
    process,
};
use ffx_emulator_config::{
    CpuArchitecture, EmulatorConfiguration, EmulatorEngine, EngineType, PointingDevice,
};
use fidl_fuchsia_developer_ffx as ffx;
use serde::{Deserialize, Serialize};
use std::{
    path::{Path, PathBuf},
    process::Command,
};

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

impl QemuEngine {
    /// returns the path to the qemu binary to execute. This is based on the guest OS architecture.
    ///
    /// Currently this is done by getting the default CLI which is for x64 images, and then
    /// replace it if the guest OK is arm64.
    /// TODO(http://fxdev.bug/98862): Improve the SDK metadata to have multiple binaries per tool.
    async fn get_qemu_path(&self) -> Result<PathBuf> {
        let cli_name = match self.emulator_configuration.device.cpu.architecture {
            CpuArchitecture::Arm64 => Some("qemu-system-aarch64"),
            CpuArchitecture::X64 => None,
            CpuArchitecture::Unsupported => None,
        };

        // Realistically, the file is always in a directory, so the empty path is a reasonable
        // fallback since it will "never" happen
        let qemu_x64_path = match self.ffx_config.get_host_tool(QEMU_TOOL).await {
            Ok(qemu_path) => qemu_path.canonicalize().context(format!(
                "Failed to canonicalize the path to the emulator binary: {:?}",
                qemu_path
            ))?,
            Err(e) => bail!("Cannot find {} in the SDK: {:?}", QEMU_TOOL, e),
        };

        // If we need to, replace the executable name.
        if let Some(exe_name) = cli_name {
            let mut p = PathBuf::from(qemu_x64_path.parent().unwrap_or(Path::new("")));
            p.push(exe_name);
            Ok(p)
        } else {
            Ok(qemu_x64_path)
        }
    }
}

#[async_trait]
impl EmulatorEngine for QemuEngine {
    async fn start(&mut self, proxy: &ffx::TargetCollectionProxy) -> Result<i32> {
        self.emulator_configuration.guest = self
            .stage_image_files(
                &self.emulator_configuration.runtime.name,
                &self.emulator_configuration.guest,
                &self.emulator_configuration.device,
                self.emulator_configuration.runtime.reuse,
                &self.ffx_config,
            )
            .await
            .context("could not stage image files")?;

        let qemu = self.get_qemu_path().await.context("could not determine qemu cli path")?;

        return self.run(&qemu, proxy).await;
    }

    fn show(&self) {
        println!("{:#?}", self.emulator_configuration);
    }
    async fn stop(&self, proxy: &ffx::TargetCollectionProxy) -> Result<()> {
        // Extract values from the self here, since there are sharing issues with trying to call
        // shutdown_emulator from another thread.
        let target_id = &self.emulator_configuration.runtime.name;
        Self::stop_emulator(self.is_running(), self.get_pid(), target_id, proxy).await
    }
    fn validate(&self) -> Result<()> {
        if self.emulator_configuration.device.pointing_device == PointingDevice::Touch {
            eprintln!("Touchscreen as a pointing device is not available on Qemu.");
            eprintln!(
                "If you encounter errors, try changing the pointing device to 'mouse' in the \
                Virtual Device specification."
            );
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
