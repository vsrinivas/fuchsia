// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The femu module encapsulates the interactions with the emulator instance
//! started via the Fuchsia emulator, Femu.

use crate::{qemu_based::QemuBasedEngine, serialization::SerializingEngine};
use anyhow::{bail, Context, Result};
use async_trait::async_trait;
use ffx_emulator_common::{config, process};
use ffx_emulator_config::{
    EmulatorConfiguration, EmulatorEngine, EngineConsoleType, EngineType, ShowDetail,
};
use fidl_fuchsia_developer_ffx as ffx;
use serde::{Deserialize, Serialize};
use std::{path::PathBuf, process::Command};

#[derive(Clone, Debug, Default, Deserialize, PartialEq, Serialize)]
pub struct FemuEngine {
    #[serde(default)]
    pub(crate) emulator_binary: PathBuf,
    pub(crate) emulator_configuration: EmulatorConfiguration,
    pub(crate) pid: u32,
    pub(crate) engine_type: EngineType,
}

#[async_trait]
impl EmulatorEngine for FemuEngine {
    async fn stage(&mut self) -> Result<()> {
        let sdk = ffx_config::get_sdk().await?;
        self.emulator_binary = match sdk.get_host_tool(config::FEMU_TOOL) {
            Ok(aemu_path) => aemu_path.canonicalize().context(format!(
                "Failed to canonicalize the path to the emulator binary: {:?}",
                aemu_path
            ))?,
            Err(e) => {
                bail!("Cannot find {} in the SDK: {:?}", config::FEMU_TOOL, e);
            }
        };

        if !self.emulator_binary.exists() || !self.emulator_binary.is_file() {
            bail!("Giving up finding emulator binary. Tried {:?}", self.emulator_binary)
        }

        <Self as QemuBasedEngine>::stage(&mut self.emulator_configuration).await
    }

    async fn start(
        &mut self,
        emulator_cmd: Command,
        proxy: &ffx::TargetCollectionProxy,
    ) -> Result<i32> {
        self.run(emulator_cmd, proxy).await
    }

    fn show(&self, details: Vec<ShowDetail>) {
        <Self as QemuBasedEngine>::show(self, details)
    }

    async fn stop(&self, proxy: &ffx::TargetCollectionProxy) -> Result<()> {
        // Extract values from the self here, since there are sharing issues with trying to call
        // shutdown_emulator from another thread.
        let target_id = &self.emulator_configuration.runtime.name;
        Self::stop_emulator(self.is_running(), self.get_pid(), target_id, proxy).await
    }

    fn validate(&self) -> Result<()> {
        if !self.emulator_configuration.runtime.headless && std::env::var("DISPLAY").is_err() {
            eprintln!(
                "No DISPLAY set in the local environment, try running with --headless if you \
                encounter failures related to display or Qt.",
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

    fn attach(&self, console: EngineConsoleType) -> Result<()> {
        self.attach_to(&self.emulator_configuration.runtime.instance_directory, console)
    }

    /// Build the Command to launch Android emulator running Fuchsia.
    fn build_emulator_cmd(&self) -> Command {
        let mut cmd = Command::new(&self.emulator_binary);
        let feature_arg = self
            .emulator_configuration
            .flags
            .features
            .iter()
            .map(|x| x.to_string())
            .collect::<Vec<_>>()
            .join(",");
        if feature_arg.len() > 0 {
            cmd.arg("-feature").arg(feature_arg);
        }
        cmd.args(&self.emulator_configuration.flags.options)
            .arg("-fuchsia")
            .args(&self.emulator_configuration.flags.args);
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
        cmd
    }

    fn emu_config(&self) -> &EmulatorConfiguration {
        return &self.emulator_configuration;
    }

    fn emu_config_mut(&mut self) -> &mut EmulatorConfiguration {
        return &mut self.emulator_configuration;
    }
}

impl SerializingEngine for FemuEngine {}

impl QemuBasedEngine for FemuEngine {
    fn set_pid(&mut self, pid: u32) {
        self.pid = pid;
    }

    fn get_pid(&self) -> u32 {
        self.pid
    }
}
