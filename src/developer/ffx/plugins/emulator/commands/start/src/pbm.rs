// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Utilities for Product Bundle Metadata (PBM).

use anyhow::{bail, Context, Result};
use ffx_emulator_common::{
    config::{EMU_UPSCRIPT_FILE, KVM_PATH},
    instances::get_instance_dir,
    split_once,
    tuntap::tap_available,
};
use ffx_emulator_config::{
    convert_bundle_to_configs, AccelerationMode, ConsoleType, EmulatorConfiguration, GpuType,
    LogLevel, NetworkingMode, OperatingSystem,
};
use ffx_emulator_start_args::StartCommand;
use pbms::{load_product_bundle, ListingMode};
use sdk_metadata::ProductBundle;
use std::str::FromStr;
use std::{collections::hash_map::DefaultHasher, env, hash::Hasher, path::PathBuf, time::Duration};

/// Lists the virtual device spec names in the specified product.
pub(crate) async fn list_virtual_devices(cmd: &StartCommand) -> Result<Vec<String>> {
    let bundle = load_product_bundle(&cmd.product_bundle, ListingMode::ReadyBundlesOnly).await?;
    match bundle {
        ProductBundle::V1(product_bundle) => Ok(product_bundle.device_refs.clone()),
        ProductBundle::V2(_) => {
            bail!("V2 Product Bundles do not yet contain multiple virtual devices")
        }
    }
}

/// Create a RuntimeConfiguration based on the command line args.
pub(crate) async fn make_configs(cmd: &StartCommand) -> Result<EmulatorConfiguration> {
    // Apply the values from the manifest to an emulation configuration.
    let mut emu_config =
        convert_bundle_to_configs(cmd.product_bundle.clone(), cmd.device().await?, cmd.verbose)
            .await
            .context("problem with convert_bundle_to_configs")?;

    // HostConfig values that come from the OS environment.
    emu_config.host.os = std::env::consts::OS.to_string().into();
    emu_config.host.architecture = std::env::consts::ARCH.to_string().into();

    // Integrate the values from command line flags into the emulation configuration, and
    // return the result to the caller.
    emu_config = apply_command_line_options(emu_config, cmd)
        .await
        .context("problem with apply command lines")?;

    Ok(emu_config)
}

/// Given an EmulatorConfiguration and a StartCommand, write the values from the
/// StartCommand onto the EmulatorConfiguration, overriding any previous values.
async fn apply_command_line_options(
    mut emu_config: EmulatorConfiguration,
    cmd: &StartCommand,
) -> Result<EmulatorConfiguration> {
    // Clone any fields that can simply copy over.
    emu_config.host.acceleration = cmd.accel.clone();
    emu_config.host.networking = cmd.net.clone();

    // Process any values that are Options, have Auto values, or need any transformation.
    emu_config.host.gpu = GpuType::from_str(&cmd.gpu().await?)?;

    if let Some(log) = &cmd.log {
        // It'd be nice to canonicalize this path, to clean up relative bits like "..", but the
        // canonicalize method also checks for existence and symlinks, and we don't generally
        // expect the log file to exist ahead of time.
        emu_config.host.log = PathBuf::from(env::current_dir()?).join(log);
    } else {
        let instance = get_instance_dir(&cmd.name, false).await?;
        emu_config.host.log = instance.join("emulator.log");
    }

    if emu_config.host.acceleration == AccelerationMode::Auto {
        let check_kvm = emu_config.device.cpu.architecture == emu_config.host.architecture;

        match emu_config.host.os {
            OperatingSystem::Linux => {
                emu_config.host.acceleration = AccelerationMode::None;
                if check_kvm {
                    let path: String = ffx_config::get(KVM_PATH)
                        .await
                        .context("getting KVM path from ffx config")?;
                    if let Ok(kvm) = std::fs::File::open(path) {
                        if let Ok(metadata) = kvm.metadata() {
                            if !metadata.permissions().readonly() {
                                emu_config.host.acceleration = AccelerationMode::Hyper
                            }
                        }
                    }
                }
            }
            OperatingSystem::MacOS => {
                // We assume Macs always have HVF installed.
                emu_config.host.acceleration =
                    if check_kvm { AccelerationMode::Hyper } else { AccelerationMode::None };
            }
            _ => {
                // For anything else, acceleration is unsupported.
                emu_config.host.acceleration = AccelerationMode::None;
            }
        }
    }

    if emu_config.host.networking == NetworkingMode::Auto {
        let available = tap_available();
        if available.is_ok() {
            emu_config.host.networking = NetworkingMode::Tap;
        } else {
            tracing::debug!(
                "Falling back on user-mode networking: {}",
                available.as_ref().unwrap_err()
            );
            emu_config.host.networking = NetworkingMode::User;
        }
    }

    // RuntimeConfig options, starting with simple copies.
    emu_config.runtime.debugger = cmd.debugger;
    emu_config.runtime.headless = cmd.headless;
    emu_config.runtime.startup_timeout = Duration::from_secs(cmd.startup_timeout().await?);
    emu_config.runtime.hidpi_scaling = cmd.hidpi_scaling;
    emu_config.runtime.addl_kernel_args = cmd.kernel_args.clone();
    emu_config.runtime.name = cmd.name.clone();
    emu_config.runtime.reuse = cmd.reuse;

    // Collapsing multiple binary options into related fields.
    if cmd.console {
        emu_config.runtime.console = ConsoleType::Console;
    } else if cmd.monitor {
        emu_config.runtime.console = ConsoleType::Monitor;
    } else {
        emu_config.runtime.console = ConsoleType::None;
    }
    emu_config.runtime.log_level = if cmd.verbose { LogLevel::Verbose } else { LogLevel::Info };

    // If the user specified a path to a flag config file on the command line, use that.
    if let Some(template_file) = &cmd.config {
        emu_config.runtime.template = PathBuf::from(env::current_dir()?).join(template_file);
    }

    if emu_config.host.networking == NetworkingMode::User {
        // Reconcile the guest ports from device_spec with the host ports from the command line.
        if let Err(e) = parse_host_port_maps(&cmd.port_map, &mut emu_config) {
            bail!(
                "Problem parsing the port-map values from the command line. \
                Please check your spelling and syntax. {:?}",
                e
            );
        }
    } else {
        // If we're not running in user mode, we don't need a port map, so clear it.
        emu_config.host.port_map.clear();
    }

    // Any generated values or values from ffx_config.
    emu_config.runtime.mac_address = generate_mac_address(&cmd.name);
    let upscript: String = ffx_config::get(EMU_UPSCRIPT_FILE)
        .await
        .context("Getting upscript path from ffx config")?;
    if !upscript.is_empty() {
        emu_config.runtime.upscript = Some(PathBuf::from(upscript));
    }

    Ok(emu_config)
}

/// Reconciles the host ports specified on the command line with the guest ports defined in the
/// device specification, mapping them together. If a host port is ill-formed or specified for a
/// guest port that was not defined, this returns an error and stops processing, so the state of
/// the port_map is undefined at that time. Duplicate ports are allowed but not advised, and a
/// warning will be logged for each occurrence.
fn parse_host_port_maps(
    flag_contents: &Vec<String>,
    emu_config: &mut EmulatorConfiguration,
) -> Result<()> {
    // At call time, the device_spec should already be parsed into the map, so this function
    // should, for each value in the Vector, check the name exists in the map and populate the host
    // value for the corresponding structure.
    for port_text in flag_contents {
        if let Ok((name, port)) = split_once(port_text, ":") {
            let mapping = emu_config.host.port_map.get_mut(&name);
            if mapping.is_none() {
                bail!(
                    "Command attempts to set port '{}', which is not defined by the device \
                    specification. Only ports with names defined by the device specification \
                    can be set. Terminating emulation.",
                    name
                );
            }
            let mut mapping = mapping.unwrap();
            if mapping.host.is_some() {
                tracing::warn!(
                    "Command line attempts to set the '{}' port more than once. This may \
                    lead to unexpected behavior. The previous entry will be discarded.",
                    name
                );
            }
            let value = port.parse::<u16>()?;
            mapping.host = Some(value);
        } else {
            bail!(
                "Invalid syntax for flag --port-map: '{}'. \
                The expected syntax is <name>:<port>, e.g. '--port-map ssh:8022'.",
                port_text
            );
        }
    }
    if emu_config.runtime.log_level == LogLevel::Verbose {
        println!("Port map parsed: {:?}\n", emu_config.host.port_map);
    }
    Ok(())
}

/// Generate a unique MAC address based on the instance name. If using the default instance name
/// of fuchsia-emulator, this will be 52:54:47:5e:82:ef.
fn generate_mac_address(name: &str) -> String {
    let mut hasher = DefaultHasher::new();
    hasher.write(name.as_bytes());
    let hashed = hasher.finish();
    let bytes = hashed.to_be_bytes();
    format!("52:54:{:02x}:{:02x}:{:02x}:{:02x}", bytes[0], bytes[1], bytes[2], bytes[3])
}

#[cfg(test)]
mod tests {
    use super::*;
    use ffx_config::{query, ConfigLevel};
    use ffx_emulator_common::config::{
        EMU_DEFAULT_DEVICE, EMU_DEFAULT_ENGINE, EMU_DEFAULT_GPU, EMU_INSTANCE_ROOT_DIR,
        EMU_START_TIMEOUT,
    };
    use ffx_emulator_config::{
        AccelerationMode, ConsoleType, CpuArchitecture, EmulatorConfiguration, GpuType, LogLevel,
        NetworkingMode, PortMapping,
    };
    use regex::Regex;
    use serde_json::json;
    use std::{
        collections::HashMap,
        fs::{create_dir_all, File},
    };
    use tempfile::tempdir;

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_apply_command_line_options() -> Result<()> {
        let _env = ffx_config::test_init().await.unwrap();

        // Set up some test data to be applied.
        let mut cmd = StartCommand {
            accel: AccelerationMode::Hyper,
            config: Some(PathBuf::from("/path/to/template")),
            console: true,
            debugger: true,
            gpu: Some(String::from("host")),
            headless: true,
            hidpi_scaling: true,
            log: Some(PathBuf::from("/path/to/log")),
            monitor: false,
            name: "SomeName".to_string(),
            net: NetworkingMode::Tap,
            verbose: true,
            ..Default::default()
        };

        // Get a default configuration, and verify we know what those values are.
        let emu_config = EmulatorConfiguration::default();
        assert_eq!(emu_config.host.acceleration, AccelerationMode::None);
        assert_eq!(emu_config.host.gpu, GpuType::Auto);
        assert_eq!(emu_config.host.log, PathBuf::from(""));
        assert_eq!(emu_config.host.networking, NetworkingMode::Auto);
        assert_eq!(emu_config.runtime.console, ConsoleType::None);
        assert_eq!(emu_config.runtime.debugger, false);
        assert_eq!(emu_config.runtime.headless, false);
        assert_eq!(emu_config.runtime.hidpi_scaling, false);
        assert_eq!(emu_config.runtime.log_level, LogLevel::Info);
        assert_eq!(emu_config.runtime.name, "");
        assert_eq!(emu_config.runtime.upscript, None);

        // Apply the test data, which should change everything in the config.
        let opts = apply_command_line_options(emu_config.clone(), &cmd).await?;
        assert_eq!(opts.host.acceleration, AccelerationMode::Hyper);
        assert_eq!(opts.host.gpu, GpuType::Host);
        assert_eq!(opts.host.log, PathBuf::from("/path/to/log"));
        assert_eq!(opts.host.networking, NetworkingMode::Tap);
        assert_eq!(opts.runtime.console, ConsoleType::Console);
        assert_eq!(opts.runtime.debugger, true);
        assert_eq!(opts.runtime.headless, true);
        assert_eq!(opts.runtime.hidpi_scaling, true);
        assert_eq!(opts.runtime.log_level, LogLevel::Verbose);
        assert_eq!(opts.runtime.name, "SomeName");
        assert_eq!(opts.runtime.template, PathBuf::from("/path/to/template"));
        assert_eq!(opts.runtime.upscript, None);

        query(EMU_UPSCRIPT_FILE)
            .level(Some(ConfigLevel::User))
            .set(json!("/path/to/upscript".to_string()))
            .await?;

        let opts = apply_command_line_options(opts, &cmd).await?;
        assert_eq!(opts.runtime.upscript, Some(PathBuf::from("/path/to/upscript")));

        // "console" and "monitor" are exclusive, so swap them and reapply.
        cmd.console = false;
        cmd.monitor = true;
        let opts = apply_command_line_options(opts, &cmd).await?;
        assert_eq!(opts.runtime.console, ConsoleType::Monitor);

        // Test relative file paths
        let temp_path = PathBuf::from(tempdir().unwrap().path());
        let long_path = temp_path.join("longer/path/to/files");
        create_dir_all(&long_path)?;
        // Set the CWD to the temp directory
        let cwd = env::current_dir().context("Error getting cwd in test")?;
        env::set_current_dir(&temp_path).context("Error setting cwd in test")?;

        cmd.log = Some(PathBuf::from("tmp.log"));
        cmd.config = Some(PathBuf::from("tmp.template"));
        let result = apply_command_line_options(emu_config.clone(), &cmd).await;
        assert!(result.is_ok(), "{:?}", result.err());
        let opts = result.unwrap();
        assert_eq!(opts.host.log, temp_path.join("tmp.log"));
        assert_eq!(opts.runtime.template, temp_path.join("tmp.template"));

        cmd.log = Some(PathBuf::from("relative/path/to/emulator.file"));
        cmd.config = Some(PathBuf::from("relative/path/to/emulator.template"));
        let result = apply_command_line_options(emu_config.clone(), &cmd).await;
        assert!(result.is_ok(), "{:?}", result.err());
        let opts = result.unwrap();
        assert_eq!(opts.host.log, temp_path.join("relative/path/to/emulator.file"));
        assert_eq!(opts.runtime.template, temp_path.join("relative/path/to/emulator.template"));

        // Set the CWD to the longer directory, so we can test ".."
        env::set_current_dir(&long_path).context("Error setting cwd in test")?;
        cmd.log = Some(PathBuf::from("../other/file.log"));
        cmd.config = Some(PathBuf::from("relative/../path/to/../template.file"));
        let result = apply_command_line_options(emu_config.clone(), &cmd).await;
        assert!(result.is_ok(), "{:?}", result.err());
        let opts = result.unwrap();
        // As mentioned in the code, it'd be nice to canonicalize this, but since the file doesn't
        // already exist that would lead to failures.
        assert_eq!(opts.host.log, temp_path.join("longer/path/to/files/../other/file.log"));
        assert_eq!(
            opts.runtime.template,
            temp_path.join("longer/path/to/files/relative/../path/to/../template.file")
        );

        // Test absolute path
        cmd.log = Some(long_path.join("absolute.file"));
        cmd.config = Some(long_path.join("absolute.template"));
        let result = apply_command_line_options(emu_config.clone(), &cmd).await;
        assert!(result.is_ok(), "{:?}", result.err());
        let opts = result.unwrap();
        assert_eq!(opts.host.log, long_path.join("absolute.file"));
        assert_eq!(opts.runtime.template, long_path.join("absolute.template"));

        env::set_current_dir(cwd).context("Revert to previous CWD")?;

        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_config_backed_values() -> Result<()> {
        let _env = ffx_config::test_init().await.unwrap();
        let mut cmd = StartCommand::default();
        let emu_config = EmulatorConfiguration::default();

        assert_eq!(cmd.device().await.unwrap(), Some(String::from("")));
        assert_eq!(cmd.engine().await.unwrap(), "femu");
        assert_eq!(cmd.gpu().await.unwrap(), "auto");
        assert_eq!(cmd.startup_timeout().await.unwrap(), 60);

        let result = apply_command_line_options(emu_config.clone(), &cmd).await;
        assert!(result.is_ok(), "{:?}", result.err());
        let opts = result.unwrap();
        assert_eq!(opts.host.gpu, GpuType::Auto);

        query(EMU_DEFAULT_DEVICE).level(Some(ConfigLevel::User)).set(json!("my_device")).await?;
        query(EMU_DEFAULT_ENGINE).level(Some(ConfigLevel::User)).set(json!("qemu")).await?;
        query(EMU_DEFAULT_GPU).level(Some(ConfigLevel::User)).set(json!("host")).await?;
        query(EMU_START_TIMEOUT).level(Some(ConfigLevel::User)).set(json!(120)).await?;

        assert_eq!(cmd.device().await.unwrap(), Some(String::from("my_device")));
        assert_eq!(cmd.engine().await.unwrap(), "qemu");
        assert_eq!(cmd.gpu().await.unwrap(), "host");
        assert_eq!(cmd.startup_timeout().await.unwrap(), 120);

        let result = apply_command_line_options(emu_config.clone(), &cmd).await;
        assert!(result.is_ok(), "{:?}", result.err());
        let opts = result.unwrap();
        assert_eq!(opts.host.gpu, GpuType::Host);

        cmd.gpu = Some(String::from("guest"));

        assert_eq!(cmd.gpu().await.unwrap(), "guest");
        let result = apply_command_line_options(emu_config.clone(), &cmd).await;
        assert!(result.is_ok(), "{:?}", result.err());
        let opts = result.unwrap();
        assert_eq!(opts.host.gpu, GpuType::Guest);

        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_accel_auto() -> Result<()> {
        let _env = ffx_config::test_init().await.unwrap();
        let temp_path = PathBuf::from(tempdir().unwrap().path());
        let file_path = temp_path.join("kvm");
        create_dir_all(&temp_path).expect("Create all temp directory");
        // The file at KVM_PATH is only tested for writability. It need not have contents.
        let file = File::create(&file_path).expect("Create temp file");
        let mut perms = file.metadata().expect("Get file metadata").permissions();

        query(KVM_PATH)
            .level(Some(ConfigLevel::User))
            .set(json!(file_path
                .as_path()
                .to_str()
                .expect("Couldn't convert file_path to str")
                .to_string()))
            .await?;
        query(EMU_INSTANCE_ROOT_DIR)
            .level(Some(ConfigLevel::User))
            .set(json!(temp_path
                .as_path()
                .to_str()
                .expect("Couldn't convert temp_path to str")
                .to_string()))
            .await?;

        // Set up some test data to be applied.
        let cmd = StartCommand { accel: AccelerationMode::Auto, ..Default::default() };
        let mut emu_config = EmulatorConfiguration::default();
        emu_config.host.os = OperatingSystem::Linux;

        perms.set_readonly(false);
        assert!(file.set_permissions(perms.clone()).is_ok());

        emu_config.device.cpu.architecture = CpuArchitecture::X64;
        emu_config.host.architecture = CpuArchitecture::X64;
        let opts = apply_command_line_options(emu_config.clone(), &cmd).await?;
        assert_eq!(opts.host.acceleration, AccelerationMode::Hyper);

        emu_config.device.cpu.architecture = CpuArchitecture::X64;
        emu_config.host.architecture = CpuArchitecture::Arm64;
        let opts = apply_command_line_options(emu_config.clone(), &cmd).await?;
        assert_eq!(opts.host.acceleration, AccelerationMode::None);

        emu_config.device.cpu.architecture = CpuArchitecture::Arm64;
        emu_config.host.architecture = CpuArchitecture::Arm64;
        let opts = apply_command_line_options(emu_config.clone(), &cmd).await?;
        assert_eq!(opts.host.acceleration, AccelerationMode::Hyper);

        emu_config.device.cpu.architecture = CpuArchitecture::Arm64;
        emu_config.host.architecture = CpuArchitecture::X64;
        let opts = apply_command_line_options(emu_config.clone(), &cmd).await?;
        assert_eq!(opts.host.acceleration, AccelerationMode::None);

        perms.set_readonly(true);
        assert!(file.set_permissions(perms.clone()).is_ok());

        emu_config.device.cpu.architecture = CpuArchitecture::X64;
        emu_config.host.architecture = CpuArchitecture::X64;
        let opts = apply_command_line_options(emu_config.clone(), &cmd).await?;
        assert_eq!(opts.host.acceleration, AccelerationMode::None);

        emu_config.device.cpu.architecture = CpuArchitecture::X64;
        emu_config.host.architecture = CpuArchitecture::Arm64;
        let opts = apply_command_line_options(emu_config.clone(), &cmd).await?;
        assert_eq!(opts.host.acceleration, AccelerationMode::None);

        emu_config.device.cpu.architecture = CpuArchitecture::Arm64;
        emu_config.host.architecture = CpuArchitecture::Arm64;
        let opts = apply_command_line_options(emu_config.clone(), &cmd).await?;
        assert_eq!(opts.host.acceleration, AccelerationMode::None);

        emu_config.device.cpu.architecture = CpuArchitecture::Arm64;
        emu_config.host.architecture = CpuArchitecture::X64;
        let opts = apply_command_line_options(emu_config.clone(), &cmd).await?;
        assert_eq!(opts.host.acceleration, AccelerationMode::None);

        emu_config.host.os = OperatingSystem::MacOS;

        emu_config.device.cpu.architecture = CpuArchitecture::X64;
        emu_config.host.architecture = CpuArchitecture::X64;
        let opts = apply_command_line_options(emu_config.clone(), &cmd).await?;
        assert_eq!(opts.host.acceleration, AccelerationMode::Hyper);

        emu_config.device.cpu.architecture = CpuArchitecture::X64;
        emu_config.host.architecture = CpuArchitecture::Arm64;
        let opts = apply_command_line_options(emu_config.clone(), &cmd).await?;
        assert_eq!(opts.host.acceleration, AccelerationMode::None);

        emu_config.device.cpu.architecture = CpuArchitecture::Arm64;
        emu_config.host.architecture = CpuArchitecture::Arm64;
        let opts = apply_command_line_options(emu_config.clone(), &cmd).await?;
        assert_eq!(opts.host.acceleration, AccelerationMode::Hyper);

        emu_config.device.cpu.architecture = CpuArchitecture::Arm64;
        emu_config.host.architecture = CpuArchitecture::X64;
        let opts = apply_command_line_options(emu_config.clone(), &cmd).await?;
        assert_eq!(opts.host.acceleration, AccelerationMode::None);

        Ok(())
    }

    #[test]
    fn test_generate_mac_address() -> Result<()> {
        let regex = Regex::new(
            r"^[[:xdigit:]]{2}:[[:xdigit:]]{2}:[[:xdigit:]]{2}:[[:xdigit:]]{2}:[[:xdigit:]]{2}:[[:xdigit:]]{2}$",
        )?;
        // Make sure we can generate the documented mac for the default string.
        // Generally we don't want to lock in implementation details but this value is included in
        // the comments, so expect to change this test case and those comments if you change the
        // generation routine.
        assert_eq!(generate_mac_address("fuchsia-emulator"), "52:54:47:5e:82:ef".to_string());

        // Make sure two reasonable names return valid MAC addresses and don't conflict.
        let first = generate_mac_address("emulator1");
        let second = generate_mac_address("emulator2");
        assert!(regex.is_match(&first), "{:?} isn't a valid MAC address", first);
        assert!(regex.is_match(&second), "{:?} isn't a valid MAC address", second);
        assert_ne!(first, second);

        // Make sure the same name generates the same MAC address when called multiple times (idempotency).
        let first = generate_mac_address("emulator");
        let second = generate_mac_address("emulator");
        assert!(regex.is_match(&first), "{:?} isn't a valid MAC address", first);
        assert_eq!(first, second);

        // We shouldn't run with an empty name, but we don't want the function to fail even if the
        // name is empty.
        let first = generate_mac_address("");
        assert!(regex.is_match(&first), "{:?} isn't a valid MAC address", first);

        Ok(())
    }

    #[test]
    fn test_parse_host_port_maps() {
        let mut emu_config = EmulatorConfiguration::default();
        let mut flag_contents;

        // No guest ports or hosts, expect success and empty map.
        emu_config.host.port_map = HashMap::new();
        flag_contents = vec![];
        let result = parse_host_port_maps(&flag_contents, &mut emu_config);
        assert!(result.is_ok(), "{:?}", result);
        assert_eq!(emu_config.host.port_map.len(), 0);

        // No guest ports, empty string for hosts, expect failure because it can't split an empty
        // host string.
        emu_config.host.port_map = HashMap::new();
        flag_contents = vec!["".to_string()];
        assert!(parse_host_port_maps(&flag_contents, &mut emu_config).is_err());

        // No guest ports, one host port, expect failure.
        flag_contents = vec!["ssh:1234".to_string()];
        assert!(parse_host_port_maps(&flag_contents, &mut emu_config).is_err());

        // One guest port, no host port, expect success.
        flag_contents = vec![];
        emu_config.host.port_map.clear();
        emu_config.host.port_map.insert("ssh".to_string(), PortMapping { host: None, guest: 22 });
        let result = parse_host_port_maps(&flag_contents, &mut emu_config);
        assert!(result.is_ok(), "{:?}", result);
        assert_eq!(emu_config.host.port_map.len(), 1);
        assert_eq!(
            emu_config.host.port_map.remove("ssh").unwrap(),
            PortMapping { host: None, guest: 22 }
        );

        // Single guest port, single host port, same name.
        flag_contents = vec!["ssh:1234".to_string()];
        emu_config.host.port_map.clear();
        emu_config.host.port_map.insert("ssh".to_string(), PortMapping { host: None, guest: 22 });
        let result = parse_host_port_maps(&flag_contents, &mut emu_config);
        assert!(result.is_ok(), "{:?}", result);
        assert_eq!(emu_config.host.port_map.len(), 1);
        assert_eq!(
            emu_config.host.port_map.remove("ssh").unwrap(),
            PortMapping { host: Some(1234), guest: 22 }
        );

        // Multiple guest ports, single host port, same name.
        flag_contents = vec!["ssh:1234".to_string()];
        emu_config.host.port_map.clear();
        emu_config.host.port_map.insert("ssh".to_string(), PortMapping { host: None, guest: 22 });
        emu_config
            .host
            .port_map
            .insert("debug".to_string(), PortMapping { host: None, guest: 2345 });
        emu_config
            .host
            .port_map
            .insert("mdns".to_string(), PortMapping { host: None, guest: 5353 });
        let result = parse_host_port_maps(&flag_contents, &mut emu_config);
        assert!(result.is_ok(), "{:?}", result);
        assert_eq!(emu_config.host.port_map.len(), 3);
        assert_eq!(
            emu_config.host.port_map.remove("ssh").unwrap(),
            PortMapping { host: Some(1234), guest: 22 }
        );
        assert_eq!(
            emu_config.host.port_map.remove("debug").unwrap(),
            PortMapping { host: None, guest: 2345 }
        );
        assert_eq!(
            emu_config.host.port_map.remove("mdns").unwrap(),
            PortMapping { host: None, guest: 5353 }
        );

        // Multiple guest port, multiple but not all host ports.
        flag_contents = vec!["ssh:1234".to_string(), "mdns:1236".to_string()];
        emu_config.host.port_map.clear();
        emu_config.host.port_map.insert("ssh".to_string(), PortMapping { host: None, guest: 22 });
        emu_config
            .host
            .port_map
            .insert("debug".to_string(), PortMapping { host: None, guest: 2345 });
        emu_config
            .host
            .port_map
            .insert("mdns".to_string(), PortMapping { host: None, guest: 5353 });
        let result = parse_host_port_maps(&flag_contents, &mut emu_config);
        assert!(result.is_ok(), "{:?}", result);
        assert_eq!(emu_config.host.port_map.len(), 3);
        assert_eq!(
            emu_config.host.port_map.remove("ssh").unwrap(),
            PortMapping { host: Some(1234), guest: 22 }
        );
        assert_eq!(
            emu_config.host.port_map.remove("debug").unwrap(),
            PortMapping { host: None, guest: 2345 }
        );
        assert_eq!(
            emu_config.host.port_map.remove("mdns").unwrap(),
            PortMapping { host: Some(1236), guest: 5353 }
        );

        // Multiple guest port, all matching host ports.
        flag_contents =
            vec!["ssh:1234".to_string(), "debug:1235".to_string(), "mdns:1236".to_string()];
        emu_config.host.port_map.clear();
        emu_config.host.port_map.insert("ssh".to_string(), PortMapping { host: None, guest: 22 });
        emu_config
            .host
            .port_map
            .insert("debug".to_string(), PortMapping { host: None, guest: 2345 });
        emu_config
            .host
            .port_map
            .insert("mdns".to_string(), PortMapping { host: None, guest: 5353 });
        let result = parse_host_port_maps(&flag_contents, &mut emu_config);
        assert!(result.is_ok(), "{:?}", result);
        assert_eq!(emu_config.host.port_map.len(), 3);
        assert_eq!(
            emu_config.host.port_map.remove("ssh").unwrap(),
            PortMapping { host: Some(1234), guest: 22 }
        );
        assert_eq!(
            emu_config.host.port_map.remove("debug").unwrap(),
            PortMapping { host: Some(1235), guest: 2345 }
        );
        assert_eq!(
            emu_config.host.port_map.remove("mdns").unwrap(),
            PortMapping { host: Some(1236), guest: 5353 }
        );

        // Multiple guest ports, extra host port, expect failure.
        flag_contents = vec![
            "ssh:1234".to_string(),
            "debug:1235".to_string(),
            "mdns:1236".to_string(),
            "undefined:1237".to_string(),
        ];
        emu_config.host.port_map.clear();
        emu_config.host.port_map.insert("ssh".to_string(), PortMapping { host: None, guest: 22 });
        emu_config
            .host
            .port_map
            .insert("debug".to_string(), PortMapping { host: None, guest: 2345 });
        emu_config
            .host
            .port_map
            .insert("mdns".to_string(), PortMapping { host: None, guest: 5353 });
        assert!(parse_host_port_maps(&flag_contents, &mut emu_config).is_err());

        // Duplicated host port specifications, expect success with earlier values discarded.
        flag_contents =
            vec!["ssh:9021".to_string(), "ssh:1984".to_string(), "ssh:8022".to_string()];
        emu_config.host.port_map.clear();
        emu_config.host.port_map.insert("ssh".to_string(), PortMapping { host: None, guest: 22 });
        let result = parse_host_port_maps(&flag_contents, &mut emu_config);
        assert!(result.is_ok(), "{:?}", result);
        assert_eq!(emu_config.host.port_map.len(), 1);
        assert_eq!(
            emu_config.host.port_map.remove("ssh").unwrap(),
            PortMapping { host: Some(8022), guest: 22 }
        );

        // Ill-formed flag contents, expect failure.
        emu_config.host.port_map.clear();
        emu_config.host.port_map.insert("ssh".to_string(), PortMapping { host: None, guest: 22 });
        flag_contents = vec!["ssh=9021".to_string()];
        assert!(parse_host_port_maps(&flag_contents, &mut emu_config).is_err());
        flag_contents = vec!["ssh:port1".to_string()];
        assert!(parse_host_port_maps(&flag_contents, &mut emu_config).is_err());
        flag_contents = vec!["ssh 1234".to_string()];
        assert!(parse_host_port_maps(&flag_contents, &mut emu_config).is_err());
        flag_contents = vec!["1234:ssh".to_string()];
        assert!(parse_host_port_maps(&flag_contents, &mut emu_config).is_err());
        flag_contents = vec!["ssh".to_string()];
        assert!(parse_host_port_maps(&flag_contents, &mut emu_config).is_err());
        flag_contents = vec!["1234".to_string()];
        assert!(parse_host_port_maps(&flag_contents, &mut emu_config).is_err());
    }
}
