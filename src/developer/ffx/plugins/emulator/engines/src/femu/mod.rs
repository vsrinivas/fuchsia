// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The femu module encapsulates the interactions with the emulator instance
//! started via the Fuchsia emulator, Femu.

use crate::{qemu::qemu_base::QemuBasedEngine, serialization::SerializingEngine};
use anyhow::Result;
use async_trait::async_trait;
use ffx_emulator_common::{config, config::FfxConfigWrapper, process};
use ffx_emulator_config::{EmulatorConfiguration, EmulatorEngine, EngineType};
use serde::{Deserialize, Serialize};
use std::{env, path::PathBuf, process::Command};

#[derive(Clone, Debug, Default, Deserialize, PartialEq, Serialize)]
pub struct FemuEngine {
    #[serde(skip)]
    pub(crate) ffx_config: FfxConfigWrapper,

    pub(crate) emulator_configuration: EmulatorConfiguration,
    pub(crate) pid: u32,
    pub(crate) engine_type: EngineType,
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

        // TODO(fxbug.dev/86737): Find the emulator executable using ffx_config::get_host_tool().
        // This is a workaround until the ffx_config::get_host_tool works.
        let sdk_root = &self.ffx_config.file(config::SDK_ROOT).await?;
        let backup_aemu =
            match env::consts::OS {
                "linux" => sdk_root
                    .join("../../prebuilt/third_party/android/aemu/release/linux-x64/emulator"),
                "macos" => sdk_root
                    .join("../../prebuilt/third_party/android/aemu/release/mac-x64/emulator"),
                _ => panic!("Sorry. {} is not supported.", env::consts::OS),
            }
            .canonicalize()?;

        let aemu = match self.ffx_config.get_host_tool(config::FEMU_TOOL).await {
            Ok(aemu_path) => aemu_path,
            Err(e) => {
                println!("Need to fix {:?}", e);
                backup_aemu
            }
        };

        return self.run(&aemu).await;
    }
    fn show(&self) {
        println!("{:#?}", self.emulator_configuration);
    }
    fn shutdown(&self) -> Result<()> {
        self.shutdown_emulator()
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
}

#[async_trait]
impl SerializingEngine for FemuEngine {}

impl QemuBasedEngine for FemuEngine {
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

    /// Build the Command to launch Android emulator running Fuchsia.
    fn build_emulator_cmd(&self, emu_binary: &PathBuf) -> Result<Command> {
        let mut cmd = Command::new(&emu_binary);
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
        Ok(cmd)
    }
}
