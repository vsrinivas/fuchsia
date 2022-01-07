// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Utilities for Product Bundle Metadata (PBM).

use anyhow::Result;
use ffx_emulator_common::config::{FfxConfigWrapper, EMU_UPSCRIPT_FILE, SDK_ROOT};
use ffx_emulator_config::{
    convert_bundle_to_configs, AccelerationMode, ConsoleType, EmulatorConfiguration, LogLevel,
    NetworkingMode,
};
use ffx_emulator_start_args::StartCommand;
use fms;
use futures::executor::block_on;
use std::{collections::hash_map::DefaultHasher, hash::Hasher, path::PathBuf};

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
    emu_config = apply_command_line_options(emu_config, cmd, ffx_config)?;

    Ok(emu_config)
}

/// Given an EmulatorConfiguration and a StartCommand, write the values from the
/// StartCommand onto the EmulatorConfiguration, overriding any previous values.
fn apply_command_line_options(
    mut emu_config: EmulatorConfiguration,
    cmd: &StartCommand,
    ffx_config: &FfxConfigWrapper,
) -> Result<EmulatorConfiguration> {
    // HostConfig overrides
    emu_config.host.acceleration = cmd.accel.clone();
    if emu_config.host.acceleration == AccelerationMode::Auto {
        if std::env::consts::OS == "linux" {
            emu_config.host.acceleration = AccelerationMode::None;
            // Make sure we have access to KVM.
            let file = std::fs::File::open("/dev/kvm");
            if let Ok(kvm) = file {
                if let Ok(metadata) = kvm.metadata() {
                    if !metadata.permissions().readonly() {
                        emu_config.host.acceleration = AccelerationMode::Hyper;
                    }
                }
            }
        } else {
            // We generally assume Macs have HVF installed.
            emu_config.host.acceleration = AccelerationMode::Hyper;
        }
    }
    emu_config.host.gpu = cmd.gpu.clone();
    if let Some(log) = &cmd.log {
        emu_config.host.log = PathBuf::from(log);
    }
    emu_config.host.networking =
        if cmd.tuntap { NetworkingMode::Tap } else { NetworkingMode::None };
    emu_config.host.os = std::env::consts::OS.to_string();
    emu_config.host.architecture = std::env::consts::ARCH.to_string();

    // RuntimeConfig options
    if cmd.console {
        emu_config.runtime.console = ConsoleType::Console;
    } else if cmd.monitor {
        emu_config.runtime.console = ConsoleType::Monitor;
    } else {
        emu_config.runtime.console = ConsoleType::None;
    }
    emu_config.runtime.debugger = cmd.debugger;
    emu_config.runtime.dry_run = cmd.dry_run;
    emu_config.runtime.headless = cmd.headless;
    emu_config.runtime.hidpi_scaling = cmd.hidpi_scaling;
    emu_config.runtime.log_level = if cmd.verbose { LogLevel::Verbose } else { LogLevel::Info };
    emu_config.runtime.mac = generate_mac(&cmd.name);
    emu_config.runtime.name = cmd.name.clone();

    // If the user specified a path to a flag template on the command line, use that.
    if let Some(template_file) = &cmd.start_up_args_template {
        emu_config.runtime.template = template_file.clone();
    }

    emu_config.runtime.upscript = if let Ok(upscript) = block_on(ffx_config.file(EMU_UPSCRIPT_FILE))
    {
        Some(upscript)
    } else {
        None
    };

    Ok(emu_config)
}

/// Generate a unique MAC address based on the instance name. If using the default instance name
/// of fuchsia-emulator, this will be 52:54:47:5e:82:ef.
fn generate_mac(name: &str) -> String {
    let mut hasher = DefaultHasher::new();
    hasher.write(name.as_bytes());
    let hashed = hasher.finish();
    let bytes = hashed.to_be_bytes();
    format!("52:54:{:02x}:{:02x}:{:02x}:{:02x}", bytes[0], bytes[1], bytes[2], bytes[3])
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
        let mut ffx_config = FfxConfigWrapper::new();
        ffx_config.overrides.insert(EMU_UPSCRIPT_FILE, "/path/to/upscript".to_string());

        // Set up some test data to be applied.
        let mut cmd = StartCommand {
            accel: AccelerationMode::Hyper,
            console: true,
            debugger: true,
            dry_run: true,
            engine: EngineType::Qemu,
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
        let emu_config = EmulatorConfiguration::default();
        assert_eq!(emu_config.host.acceleration, AccelerationMode::None);
        assert_eq!(emu_config.host.gpu, GpuType::Auto);
        assert_eq!(emu_config.host.log, PathBuf::from(""));
        assert_eq!(emu_config.host.networking, NetworkingMode::None);
        assert_eq!(emu_config.runtime.console, ConsoleType::None);
        assert_eq!(emu_config.runtime.debugger, false);
        assert_eq!(emu_config.runtime.dry_run, false);
        assert_eq!(emu_config.runtime.headless, false);
        assert_eq!(emu_config.runtime.hidpi_scaling, false);
        assert_eq!(emu_config.runtime.log_level, LogLevel::Info);
        assert_eq!(emu_config.runtime.name, "");
        assert_eq!(emu_config.runtime.upscript, None);

        // Apply the test data, which should change everything in the config.
        let opts = apply_command_line_options(emu_config, &cmd, &ffx_config)?;
        assert_eq!(opts.host.acceleration, AccelerationMode::Hyper);
        assert_eq!(opts.host.gpu, GpuType::Host);
        assert_eq!(opts.host.log, PathBuf::from("/path/to/log"));
        assert_eq!(opts.host.networking, NetworkingMode::Tap);
        assert_eq!(opts.runtime.console, ConsoleType::Console);
        assert_eq!(opts.runtime.debugger, true);
        assert_eq!(opts.runtime.dry_run, true);
        assert_eq!(opts.runtime.headless, true);
        assert_eq!(opts.runtime.hidpi_scaling, true);
        assert_eq!(opts.runtime.log_level, LogLevel::Verbose);
        assert_eq!(opts.runtime.name, "SomeName");
        assert_eq!(opts.runtime.upscript, Some(PathBuf::from("/path/to/upscript")));

        // "console" and "monitor" are exclusive, so swap them and reapply.
        cmd.console = false;
        cmd.monitor = true;
        let opts = apply_command_line_options(opts, &cmd, &ffx_config)?;
        assert_eq!(opts.runtime.console, ConsoleType::Monitor);

        Ok(())
    }
}
