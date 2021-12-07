// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The femu module encapsulates the interactions with the emulator instance
//! started via the Fuchsia emulator, Femu.

use crate::{
    behaviors::FemuBehavior, qemu::qemu_base::QemuBasedEngine, serialization::SerializingEngine,
};
use anyhow::{bail, Result};
use async_trait::async_trait;
use ffx_emulator_common::{config, config::FfxConfigWrapper, process};
use ffx_emulator_config::{
    Behavior, ConsoleType, EmulatorConfiguration, EmulatorEngine, EngineType, LogLevel,
};
use serde::{Deserialize, Serialize};
use shared_child::SharedChild;
use std::{collections::HashMap, env, path::PathBuf, process::Command, sync::Arc};

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

    pub(crate) implemented_behaviors: HashMap<String, Behavior>,
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

        self.set_up_behaviors()?;

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
        <FemuEngine as QemuBasedEngine>::clean_up_behaviors(&self.implemented_behaviors)?;
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
    fn set_up_behaviors(&mut self) -> Result<()> {
        // QemuBasedEngine::set_up_behaviors runs the behavior filters, runs setup on the accepted
        // behaviors, and returns a HashMap of the ones that were accepted. Then we store the
        // accepted behaviors, and push the args, etc., into the engine's vectors. These will feed
        // directly into the Command to start the emulator program.
        let result =
            <FemuEngine as QemuBasedEngine>::set_up_behaviors(&self.emulator_configuration);

        match result {
            Ok(behaviors) => {
                for behavior in behaviors.values() {
                    let femu = behavior as &dyn FemuBehavior;
                    self.args.extend(femu.args().unwrap_or(vec![]));
                    self.features.extend(femu.features().unwrap_or(vec![]));
                    self.kernel_args.extend(femu.kernel_args().unwrap_or(vec![]));
                    self.options.extend(femu.options().unwrap_or(vec![]));
                }
                self.implemented_behaviors = behaviors;
                Ok(())
            }
            Err(e) => {
                if let Err(e1) =
                    <FemuEngine as QemuBasedEngine>::clean_up_behaviors(&self.implemented_behaviors)
                {
                    log::error!("{:?}", e1);
                }
                Err(e)
            }
        }
    }
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
#[cfg(test)]
mod tests {
    use super::*;
    use ffx_emulator_config::{AccelerationMode, BehaviorData, FemuData};

    fn new_behavior(data: BehaviorData) -> Behavior {
        Behavior {
            description: "Description".to_string(),
            handler: "SimpleBehavior".to_string(),
            data,
        }
    }

    #[test]
    fn test_set_up_behaviors() -> Result<()> {
        // These behaviors are SimpleFemuBehaviors. They should be included, and the result should
        // contain their args/features/options strings.
        let behavior_data_1 = BehaviorData {
            femu: Some(FemuData {
                args: vec!["--b1arg".to_string()],
                features: vec!["B1Feature".to_string()],
                options: vec!["b1-option1".to_string(), "b1-option2".to_string()],
                kernel_args: vec!["b1-kernel".to_string()],
            }),
        };

        let behavior_data_2 = BehaviorData {
            femu: Some(FemuData {
                args: vec!["--b2arg".to_string()],
                features: vec!["B2Feature".to_string()],
                options: vec!["b2-option1".to_string(), "b2-option2".to_string()],
                kernel_args: vec!["b2-kernel".to_string()],
            }),
        };

        let mut behaviors = HashMap::new();
        behaviors.insert("test_ok_1".to_string(), new_behavior(behavior_data_1));
        behaviors.insert("test_ok_2".to_string(), new_behavior(behavior_data_2));

        // The last one tries to add KVM, but we set the accel flag to None, which rejects KVM
        let behavior_data_3 = BehaviorData {
            femu: Some(FemuData {
                args: vec!["--b3arg".to_string()],
                features: vec!["B3Feature".to_string()],
                options: vec!["b3-option1".to_string(), "b3-option2".to_string()],
                kernel_args: vec!["b3-kernel".to_string()],
            }),
        };
        let reject = Behavior {
            description: "Description".to_string(),
            handler: "KvmBehavior".to_string(),
            data: behavior_data_3,
        };
        behaviors.insert("test_bad_3".to_string(), reject);

        let mut engine = FemuEngine::default();
        engine.emulator_configuration.behaviors = behaviors;
        engine.emulator_configuration.host.acceleration = AccelerationMode::None;

        assert!(engine.set_up_behaviors().is_ok());
        assert_eq!(engine.implemented_behaviors.len(), 2);

        assert!(engine.implemented_behaviors.keys().any(|v| v == "test_ok_1"));
        assert!(engine.args.iter().any(|v| v == "--b1arg"));
        assert!(engine.features.iter().any(|v| v == "B1Feature"));
        assert!(engine.options.iter().any(|v| v == "b1-option1"));
        assert!(engine.options.iter().any(|v| v == "b1-option2"));
        assert!(engine.kernel_args.iter().any(|v| v == "b1-kernel"));

        assert!(engine.implemented_behaviors.keys().any(|v| v == "test_ok_2"));
        assert!(engine.args.iter().any(|v| v == "--b2arg"));
        assert!(engine.features.iter().any(|v| v == "B2Feature"));
        assert!(engine.options.iter().any(|v| v == "b2-option1"));
        assert!(engine.options.iter().any(|v| v == "b2-option2"));
        assert!(engine.kernel_args.iter().any(|v| v == "b2-kernel"));

        assert!(!engine.implemented_behaviors.keys().any(|v| v == "test_bad_3"));
        assert!(!engine.args.iter().any(|v| v == "--b3arg"));
        assert!(!engine.features.iter().any(|v| v == "B3Feature"));
        assert!(!engine.options.iter().any(|v| v == "b3-option1"));
        assert!(!engine.options.iter().any(|v| v == "b3-option2"));
        assert!(!engine.kernel_args.iter().any(|v| v == "b3-kernel"));

        Ok(())
    }
}
