// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Utilities for Product Bundle Metadata (PBM).

use anyhow::{anyhow, Result};
use ffx_emulator_common::config::{FfxConfigWrapper, SDK_ROOT};
use ffx_emulator_config::{
    convert_bundle_to_configs, AccelerationMode, ConsoleType, EmulatorConfiguration, LogLevel,
    NetworkingMode,
};
use ffx_emulator_start_args::StartCommand;
use fms;
use std::path::PathBuf;

/// Create a RuntimeConfiguration based on the command line args.
pub(crate) async fn make_configs(
    cmd: &StartCommand,
    ffx_config: &FfxConfigWrapper,
) -> Result<EmulatorConfiguration> {
    let fms_entries = fms::Entries::from_config().await?;
    let product_bundle = fms::find_product_bundle(&fms_entries, &cmd.product_bundle)?;
    let virtual_device = fms::find_virtual_device(&fms_entries, &product_bundle.device_refs)?;
    if cmd.verbose {
        println!(
            "Found PBM {:?}, device_refs {:?}, virtual_device {:?}.",
            product_bundle.name, product_bundle.device_refs, virtual_device,
        );
    }

    // TODO(fxbug.dev/90948): Don't hard-code the image directory path, once it's in the SDK.
    let sdk_root = ffx_config.file(SDK_ROOT).await?;
    let fms_path = sdk_root.join("gen/build/images");

    // Apply the values from the manifest to an emulation configuration.
    let mut emu_config =
        convert_bundle_to_configs(product_bundle, virtual_device, &sdk_root, &fms_path)?;

    // Integrate the values from command line flags into the emulation configuration, and
    // return the result to the caller.
    emu_config = apply_command_line_options(emu_config, cmd)?;

    Ok(emu_config)
}

/// Given an EmulatorConfiguration and a StartCommand, write the values from the
/// StartCommand onto the EmulatorConfiguration, overriding any previous values.
fn apply_command_line_options(
    mut config: EmulatorConfiguration,
    cmd: &StartCommand,
) -> Result<EmulatorConfiguration> {
    // HostConfig overrides
    config.host.acceleration = cmd.accel.clone();
    if config.host.acceleration == AccelerationMode::Auto {
        if std::env::consts::OS == "linux" {
            config.host.acceleration = AccelerationMode::None;
            // Make sure we have access to KVM.
            let file = std::fs::File::open("/dev/kvm");
            if let Ok(kvm) = file {
                if let Ok(metadata) = kvm.metadata() {
                    if !metadata.permissions().readonly() {
                        config.host.acceleration = AccelerationMode::Hyper;
                    }
                }
            }
        } else {
            // We generally assume Macs have HVF installed.
            config.host.acceleration = AccelerationMode::Hyper;
        }
    }
    config.host.gpu = cmd.gpu.clone();
    if let Some(log) = &cmd.log {
        config.host.log = PathBuf::from(log);
    }
    config.host.networking =
        if cmd.tuntap { NetworkingMode::Bridged } else { NetworkingMode::None };
    config.host.os = std::env::consts::OS.to_string();
    config.host.architecture = std::env::consts::ARCH.to_string();

    // RuntimeConfig options
    if cmd.console {
        config.runtime.console = ConsoleType::Console;
    } else if cmd.monitor {
        config.runtime.console = ConsoleType::Monitor;
    } else {
        config.runtime.console = ConsoleType::None;
    }
    config.runtime.debugger = cmd.debugger;
    config.runtime.dry_run = cmd.dry_run;
    for env_var in &cmd.envs {
        if let Some((key, value)) = env_var.split_once("=") {
            config.runtime.environment.insert(key.to_string(), value.to_string());
        } else {
            return Err(anyhow!(
                "Problem parsing environment string: {} doesn't match the 'key=value' pattern",
                env_var
            ));
        }
    }
    config.runtime.headless = cmd.headless;
    config.runtime.hidpi_scaling = cmd.hidpi_scaling;
    config.runtime.log_level = if cmd.verbose { LogLevel::Verbose } else { LogLevel::Info };
    config.runtime.name = cmd.name.clone();

    // If the user specified a path to a flag template on the command line, use that.
    if let Some(template_file) = &cmd.start_up_args_template {
        config.runtime.template = template_file.clone();
    }
    Ok(config)
}

#[cfg(test)]
mod tests {
    use super::*;
    use ffx_emulator_config::{
        AccelerationMode, ConsoleType, EmulatorConfiguration, EngineType, GpuType, LogLevel,
        NetworkingMode,
    };

    #[test]
    fn test_apply_command_line_options() -> Result<()> {
        // Set up some test data to be applied.
        let mut cmd = StartCommand {
            accel: AccelerationMode::Hyper,
            console: true,
            debugger: true,
            dry_run: true,
            engine: EngineType::Qemu,
            envs: vec!["foo=1".to_string(), "bar=2".to_string(), "baz=3".to_string()],
            gpu: GpuType::Host,
            headless: true,
            hidpi_scaling: true,
            log: Some(PathBuf::from("/path/to/log")),
            monitor: false,
            name: "SomeName".to_string(),
            tuntap: true,
            verbose: true,
            ..Default::default()
        };

        // Get a default configuration, and verify we know what those values are.
        let config = EmulatorConfiguration::default();
        assert_eq!(config.host.acceleration, AccelerationMode::None);
        assert_eq!(config.host.gpu, GpuType::Auto);
        assert_eq!(config.host.log, PathBuf::from(""));
        assert_eq!(config.host.networking, NetworkingMode::None);
        assert_eq!(config.runtime.console, ConsoleType::None);
        assert_eq!(config.runtime.debugger, false);
        assert_eq!(config.runtime.dry_run, false);
        assert_eq!(config.runtime.environment.len(), 0);
        assert_eq!(config.runtime.headless, false);
        assert_eq!(config.runtime.hidpi_scaling, false);
        assert_eq!(config.runtime.log_level, LogLevel::Info);
        assert_eq!(config.runtime.name, "");

        // Apply the test data, which should change everything in the config.
        let opts = apply_command_line_options(config, &cmd)?;
        assert_eq!(opts.host.acceleration, AccelerationMode::Hyper);
        assert_eq!(opts.host.gpu, GpuType::Host);
        assert_eq!(opts.host.log, PathBuf::from("/path/to/log"));
        assert_eq!(opts.host.networking, NetworkingMode::Bridged);
        assert_eq!(opts.runtime.console, ConsoleType::Console);
        assert_eq!(opts.runtime.debugger, true);
        assert_eq!(opts.runtime.dry_run, true);
        assert_eq!(opts.runtime.environment.len(), 3);
        assert_eq!(opts.runtime.environment.get("foo"), Some(&"1".to_string()));
        assert_eq!(opts.runtime.environment.get("bar"), Some(&"2".to_string()));
        assert_eq!(opts.runtime.environment.get("baz"), Some(&"3".to_string()));
        assert_eq!(opts.runtime.headless, true);
        assert_eq!(opts.runtime.hidpi_scaling, true);
        assert_eq!(opts.runtime.log_level, LogLevel::Verbose);
        assert_eq!(opts.runtime.name, "SomeName");

        // "console" and "monitor" are exclusive, so swap them and reapply.
        cmd.console = false;
        cmd.monitor = true;
        let opts = apply_command_line_options(opts, &cmd)?;
        assert_eq!(opts.runtime.console, ConsoleType::Monitor);

        Ok(())
    }
}
