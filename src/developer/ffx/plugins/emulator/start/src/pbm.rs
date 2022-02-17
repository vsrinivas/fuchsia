// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Utilities for Product Bundle Metadata (PBM).

use anyhow::{bail, Context, Result};
use ffx_emulator_common::{
    config::{FfxConfigWrapper, EMU_UPSCRIPT_FILE},
    split_once, tap_available,
};
use ffx_emulator_config::{
    convert_bundle_to_configs, AccelerationMode, ConsoleType, EmulatorConfiguration, LogLevel,
    NetworkingMode,
};
use ffx_emulator_engines::get_instance_dir;
use ffx_emulator_start_args::StartCommand;
use fms;
use futures::executor::block_on;
use port_picker::{is_free_tcp_port, pick_unused_port};
use std::{collections::hash_map::DefaultHasher, hash::Hasher, path::PathBuf, time::Duration};

/// Create a RuntimeConfiguration based on the command line args.
pub(crate) async fn make_configs(
    cmd: &StartCommand,
    ffx_config: &FfxConfigWrapper,
) -> Result<EmulatorConfiguration> {
    let fms_entries =
        pbms::get_pbms(/*update_metadata=*/ false).await.context("problem with fms_entries")?;
    let product_bundle = fms::find_product_bundle(&fms_entries, &cmd.product_bundle)
        .context("problem with product_bundle")?;
    let virtual_device = fms::find_virtual_device(&fms_entries, &product_bundle.device_refs)
        .context("problem with virtual device")?;
    if cmd.verbose {
        println!(
            "Found PBM {:?}, device_refs {:?}, virtual_device {:?}.",
            product_bundle.name, product_bundle.device_refs, virtual_device,
        );
    }

    let data_root = pbms::get_data_dir(&product_bundle.name).await?;
    let metadata_root = pbms::get_metadata_dir(&product_bundle.name).await?;

    // Apply the values from the manifest to an emulation configuration.
    let mut emu_config =
        convert_bundle_to_configs(product_bundle, virtual_device, &data_root, &metadata_root)
            .context("problem with convert_bundle_to_configs")?;

    // Integrate the values from command line flags into the emulation configuration, and
    // return the result to the caller.
    emu_config = apply_command_line_options(emu_config, cmd, ffx_config)
        .await
        .context("problem with apply command lines")?;

    if emu_config.host.networking == NetworkingMode::User {
        finalize_port_mapping(&mut emu_config).context("problem with port mapping")?;
    }

    Ok(emu_config)
}

/// Given an EmulatorConfiguration and a StartCommand, write the values from the
/// StartCommand onto the EmulatorConfiguration, overriding any previous values.
async fn apply_command_line_options(
    mut emu_config: EmulatorConfiguration,
    cmd: &StartCommand,
    ffx_config: &FfxConfigWrapper,
) -> Result<EmulatorConfiguration> {
    // HostConfig overrides, starting with env constants.
    emu_config.host.os = std::env::consts::OS.to_string();
    emu_config.host.architecture = std::env::consts::ARCH.to_string();

    // Clone any fields that can simply copy over.
    emu_config.host.acceleration = cmd.accel.clone();
    emu_config.host.gpu = cmd.gpu.clone();
    emu_config.host.networking = cmd.net.clone();

    // Process any values that are Options, have Auto values, or need any transformation.
    if let Some(log) = &cmd.log {
        emu_config.host.log = PathBuf::from(log);
    } else {
        let instance = get_instance_dir(&ffx_config, &cmd.name, false).await?;
        emu_config.host.log = instance.join("emulator.log");
    }
    if emu_config.host.acceleration == AccelerationMode::Auto {
        if emu_config.host.os == "linux" {
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
    if emu_config.host.networking == NetworkingMode::Auto {
        if tap_available() {
            emu_config.host.networking = NetworkingMode::Tap;
        } else {
            emu_config.host.networking = NetworkingMode::User;
        }
    }

    // RuntimeConfig options, starting with simple copies.
    emu_config.runtime.debugger = cmd.debugger;
    emu_config.runtime.dry_run = cmd.dry_run;
    emu_config.runtime.headless = cmd.headless;
    emu_config.runtime.startup_timeout = Duration::from_secs(cmd.startup_timeout().await?);
    emu_config.runtime.hidpi_scaling = cmd.hidpi_scaling;
    emu_config.runtime.name = cmd.name.clone();

    // Collapsing multiple binary options into related fields.
    if cmd.console {
        emu_config.runtime.console = ConsoleType::Console;
    } else if cmd.monitor {
        emu_config.runtime.console = ConsoleType::Monitor;
    } else {
        emu_config.runtime.console = ConsoleType::None;
    }
    emu_config.runtime.log_level = if cmd.verbose { LogLevel::Verbose } else { LogLevel::Info };

    // If the user specified a path to a flag template on the command line, use that.
    if let Some(template_file) = &cmd.start_up_args_template {
        emu_config.runtime.template = template_file.clone();
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
    emu_config.runtime.upscript = if let Ok(upscript) = block_on(ffx_config.file(EMU_UPSCRIPT_FILE))
    {
        Some(upscript)
    } else {
        None
    };

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
                log::warn!(
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

/// Ensures all ports are mapped with available port values, assigning free ports any that are
/// missing, and making sure there are no conflicts within the map.
fn finalize_port_mapping(emu_config: &mut EmulatorConfiguration) -> Result<()> {
    let port_map = &mut emu_config.host.port_map;
    let mut used_ports = Vec::new();
    for (name, port) in port_map {
        if let Some(value) = port.host {
            if is_free_tcp_port(value).is_some() && !used_ports.contains(&value) {
                // This port is good, so we claim it to make sure there are no conflicts later.
                used_ports.push(value);
            } else {
                bail!("Host port {} was mapped to multiple guest ports.", value);
            }
        } else {
            log::warn!(
                "No host-side port specified for '{:?}', a host port will be dynamically \
                assigned. Check `ffx emu show {}` to see which port is assigned.",
                name,
                emu_config.runtime.name
            );
            if let Some(value) = pick_unused_port() {
                port.host = Some(value);
                used_ports.push(value);
            } else {
                bail!("Unable to assign a host port for '{}'. Terminating emulation.", name);
            }
        }
    }
    if emu_config.runtime.log_level == LogLevel::Verbose {
        println!("Port map finalized: {:?}\n", &emu_config.host.port_map);
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
    use ffx_emulator_config::{
        AccelerationMode, ConsoleType, EmulatorConfiguration, EngineType, GpuType, LogLevel,
        NetworkingMode, PortMapping,
    };
    use regex::Regex;
    use std::collections::HashMap;

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_apply_command_line_options() -> Result<()> {
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
        assert_eq!(emu_config.runtime.dry_run, false);
        assert_eq!(emu_config.runtime.headless, false);
        assert_eq!(emu_config.runtime.hidpi_scaling, false);
        assert_eq!(emu_config.runtime.log_level, LogLevel::Info);
        assert_eq!(emu_config.runtime.name, "");
        assert_eq!(emu_config.runtime.upscript, None);

        // Apply the test data, which should change everything in the config.
        let opts = apply_command_line_options(emu_config, &cmd, &ffx_config).await?;
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
        let opts = apply_command_line_options(opts, &cmd, &ffx_config).await?;
        assert_eq!(opts.runtime.console, ConsoleType::Monitor);

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
