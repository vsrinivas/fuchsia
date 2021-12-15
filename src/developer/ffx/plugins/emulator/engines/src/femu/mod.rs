// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The femu module encapsulates the interactions with the emulator instance
//! started via the Fuchsia emulator, Femu.

use crate::{qemu::qemu_base::QemuBasedEngine, serialization::SerializingEngine};
use anyhow::{bail, Result};
use async_trait::async_trait;
use ffx_emulator_common::{config, config::FfxConfigWrapper, process};
use ffx_emulator_config::{
    ConsoleType, EmulatorConfiguration, EmulatorEngine, EngineType, LogLevel,
};
use serde::{Deserialize, Serialize};
use shared_child::SharedChild;
use std::{env, path::PathBuf, process::Command, sync::Arc};

#[derive(Clone, Debug, Default, Deserialize, PartialEq, Serialize)]
pub struct FemuEngine {
    #[serde(skip)]
    pub(crate) ffx_config: FfxConfigWrapper,

    pub(crate) emulator_configuration: EmulatorConfiguration,
    pub(crate) pid: u32,
    pub(crate) engine_type: EngineType,

    pub(crate) args: Vec<String>,
    pub(crate) features: Vec<String>,
    pub(crate) kernel_args: Vec<String>,
    pub(crate) options: Vec<String>,
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

        let instance_directory = self.emulator_configuration.runtime.instance_directory.clone();
        self.write_to_disk(&instance_directory).await?;

        // TODO(fxbug.dev/86737): Find the emulator executable using ffx_config::get_host_tool().
        // This is a workaround until the ffx_config::get_host_tool works.
        let sdk_root = &self.ffx_config.file(config::SDK_ROOT).await?;
        let backup_aemu = match env::consts::OS {
            "linux" => sdk_root.join("../../prebuilt/third_party/aemu/linux-x64/emulator"),
            "macos" => sdk_root.join("../../prebuilt/third_party/aemu/mac-x64/emulator"),
            _ => panic!("Sorry. {} is not supported.", env::consts::OS),
        };

        let aemu = match self.ffx_config.get_host_tool(config::FEMU_TOOL).await {
            Ok(aemu_path) => aemu_path,
            Err(e) => {
                print!("Need to fix {:?}", e);
                backup_aemu
            }
        };

        if !aemu.exists() || !aemu.is_file() {
            bail!("Giving up finding aemu binary. Tried {:?}", aemu)
        }

        let mut emulator_cmd = self.build_emulator_cmd(&aemu)?;

        if self.emulator_configuration.runtime.log_level == LogLevel::Verbose
            || self.emulator_configuration.runtime.dry_run
        {
            println!("[aemu emulator] Running emulator cmd: {:?}\n", emulator_cmd);
            if self.emulator_configuration.runtime.dry_run {
                return Ok(0);
            }
        }

        let shared_process = SharedChild::spawn(&mut emulator_cmd)?;
        let child_arc = Arc::new(shared_process);

        self.pid = child_arc.id();

        self.write_to_disk(&self.emulator_configuration.runtime.instance_directory).await?;

        if self.emulator_configuration.runtime.console == ConsoleType::Monitor
            || self.emulator_configuration.runtime.console == ConsoleType::Console
        {
            // When running with '--monitor' or '--console' mode, the user is directly interacting
            // with the emulator console, or the guest console. Therefore wait until the
            // execution of QEMU or AEMU terminates.
            match fuchsia_async::unblock(move || process::monitored_child_process(&child_arc)).await
            {
                Ok(_) => {
                    return Ok(0);
                }
                Err(e) => {
                    if let Some(shutdown_error) = self.shutdown().err() {
                        log::debug!(
                            "Error encountered in shutdown when handling failed launch: {:?}",
                            shutdown_error
                        );
                    }
                    bail!("Emulator launcher did not terminate properly, error: {}", e)
                }
            }
        } else if self.emulator_configuration.runtime.debugger {
            let status = child_arc.wait()?;
            if !status.success() {
                let exit_code = status.code().unwrap_or_default();
                if exit_code != 0 {
                    bail!("Cannot start Fuchsia Emulator.")
                }
            }
        }
        Ok(0)
    }
    fn show(&self) {
        println!("{:#?}", self.emulator_configuration);
    }
    fn shutdown(&mut self) -> Result<()> {
        if self.is_running() {
            print!("Terminating running instance {:?}", self.pid);
            if let Some(terminate_error) = process::terminate(self.pid).err() {
                log::debug!("Error encountered terminating process: {:?}", terminate_error);
            }
        }
        Ok(())
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
impl SerializingEngine for FemuEngine {}

impl QemuBasedEngine for FemuEngine {}

impl FemuEngine {
    /// Build the Command to launch Android emulator running Fuchsia.
    fn build_emulator_cmd(&mut self, aemu: &PathBuf) -> Result<Command> {
        let mut cmd = Command::new(&aemu);
        let feature_arg = self.features.iter().map(|x| x.to_string()).collect::<Vec<_>>().join(",");
        cmd.arg("-feature").arg(feature_arg).args(&self.options).arg("-fuchsia").args(&self.args);
        let extra_args =
            self.kernel_args.iter().map(|x| x.to_string()).collect::<Vec<_>>().join(" ");
        cmd.args(["-append", &extra_args]);

        // Environment key-value pairs are expected to be command line friendly.
        // (no leading/trailing whitespace, etc.)
        for (k, v) in &self.emulator_configuration.runtime.environment {
            cmd.arg("--env").arg(format!("{}={}", k, v));
        }
        Ok(cmd)
    }
}
