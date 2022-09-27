// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This module contains code for converting between the sdk_metadata types and the engine
//! interface types. We perform the conversion here to keep dependencies on the sdk_metadata
//! to a minimum, while improving our ability to fully test the conversion code.

use crate::{DeviceConfig, EmulatorConfiguration, GuestConfig, PortMapping, VirtualCpu};
use anyhow::{anyhow, bail, Context, Result};
use pbms::{
    fms_entries_from, get_images_dir, load_product_bundle, select_product_bundle, ListingMode,
};
use sdk_metadata::{ProductBundle, ProductBundleV1, VirtualDeviceV1};
use std::path::Path;

pub async fn convert_bundle_to_configs(
    product_bundle_name: Option<String>,
    device_name: Option<String>,
    verbose: bool,
) -> Result<EmulatorConfiguration> {
    let product_url = select_product_bundle(&product_bundle_name, ListingMode::ReadyBundlesOnly)
        .await
        .context("Selecting product bundle")?;
    let product_bundle =
        load_product_bundle(&Some(product_url.to_string()), ListingMode::ReadyBundlesOnly).await?;
    match &product_bundle {
        ProductBundle::V1(product_bundle) => {
            // Get the virtual devices.
            let fms_entries = fms_entries_from(&product_url).await.context("get fms entries")?;
            let virtual_devices =
                fms::find_virtual_devices(&fms_entries, &product_bundle.device_refs)
                    .context("problem with virtual device")?;

            // Find the data root, which is used to find the images and template file.
            let data_root = get_images_dir(&product_url).await.context("images dir")?;

            // Determine the correct device name from the user, or default to the first one listed
            // in the product bundle.
            let virtual_device = match device_name {
                // If no device_name is given, choose the first virtual device listed in the
                // productbundle.
                None => match &virtual_devices[0] {
                    sdk_metadata::VirtualDevice::V1(v) => v,
                },

                // Otherwise, find the virtual device by name in the product bundle.
                Some(device_name) => {
                    let vd = virtual_devices
                        .iter()
                        .find(|vd| vd.name() == device_name)
                        .ok_or(anyhow!("The device is not found in the product bundle"))?;
                    match vd {
                        sdk_metadata::VirtualDevice::V1(v) => v,
                    }
                }
            };

            if verbose {
                println!(
                    "Found PBM: {:?}, device_refs: {:?}, virtual_device: {:?}",
                    &product_bundle.name, &product_bundle.device_refs, &virtual_device
                );
            }
            convert_v1_bundle_to_configs(product_bundle, &virtual_device, &data_root)
        }
        _ => bail!("ProductBundleV2 is not support in the emulator yet"),
    }
}

/// - `data_root` is a path to a directory. When working in-tree it's the path
///   to build output dir; when using the SDK it's the path to the downloaded
///   images directory.
fn convert_v1_bundle_to_configs(
    product_bundle: &ProductBundleV1,
    virtual_device: &VirtualDeviceV1,
    data_root: &Path,
) -> Result<EmulatorConfiguration> {
    let mut emulator_configuration: EmulatorConfiguration = EmulatorConfiguration::default();

    // Map the product and device specifications to the Device, and Guest configs.
    emulator_configuration.device = DeviceConfig {
        audio: virtual_device.hardware.audio.clone(),
        cpu: VirtualCpu {
            architecture: virtual_device.hardware.cpu.arch.clone(),
            // TODO(fxbug.dev/88909): Add a count parameter to the virtual_device cpu field.
            count: usize::default(),
        },
        memory: virtual_device.hardware.memory.clone(),
        pointing_device: virtual_device.hardware.inputs.pointing_device.clone(),
        screen: virtual_device.hardware.window_size.clone(),
        storage: virtual_device.hardware.storage.clone(),
    };

    if let Some(template) = &virtual_device.start_up_args_template {
        emulator_configuration.runtime.template =
            data_root.join(&template).canonicalize().with_context(|| {
                format!("canonicalize template path {:?}", data_root.join(&template))
            })?;
    }

    if let Some(ports) = &virtual_device.ports {
        for (name, port) in ports {
            emulator_configuration
                .host
                .port_map
                .insert(name.to_owned(), PortMapping { guest: port.to_owned(), host: None });
        }
    }

    if let Some(emu) = &product_bundle.manifests.emu {
        emulator_configuration.guest = GuestConfig {
            // TODO(fxbug.dev/88908): Eventually we'll need to support multiple disk_images.
            fvm_image: if !emu.disk_images.is_empty() {
                Some(data_root.join(&emu.disk_images[0]))
            } else {
                None
            },
            kernel_image: data_root.join(&emu.kernel),
            zbi_image: data_root.join(&emu.initial_ramdisk),
        };
    } else {
        return Err(anyhow!(
            "The Product Bundle specified by {} does not contain any Emulator Manifests.",
            &product_bundle.name
        ));
    }
    Ok(emulator_configuration)
}

#[cfg(test)]
mod tests {
    use super::*;
    use sdk_metadata::{
        virtual_device::{Cpu, Hardware},
        AudioDevice, AudioModel, CpuArchitecture, DataAmount, DataUnits, ElementType, EmuManifest,
        InputDevice, Manifests, PointingDevice, Screen, ScreenUnits,
    };
    use sdk_metadata::{ProductBundleV1, VirtualDeviceV1};
    use std::collections::HashMap;

    #[test]
    fn test_convert_bundle_to_configs() {
        let temp_dir = tempfile::TempDir::new().expect("creating sdk_root temp dir");
        let sdk_root = temp_dir.path();
        let template_path = sdk_root.join("fake_template");
        std::fs::write(&template_path, b"").expect("create fake template file");

        // Set up some test data to pass into the conversion routine.
        let mut pb = ProductBundleV1 {
            description: Some("A fake product bundle".to_string()),
            device_refs: vec!["".to_string()],
            images: vec![],
            manifests: Manifests {
                emu: Some(EmuManifest {
                    disk_images: vec!["path/to/disk".to_string()],
                    initial_ramdisk: "path/to/zbi".to_string(),
                    kernel: "path/to/kernel".to_string(),
                }),
                flash: None,
            },
            metadata: None,
            packages: vec![],
            name: "FakeBundle".to_string(),
            kind: ElementType::ProductBundle,
        };
        let mut device = VirtualDeviceV1 {
            name: "FakeDevice".to_string(),
            description: Some("A fake virtual device".to_string()),
            kind: ElementType::VirtualDevice,
            hardware: Hardware {
                cpu: Cpu { arch: CpuArchitecture::X64 },
                audio: AudioDevice { model: AudioModel::Hda },
                storage: DataAmount { quantity: 512, units: DataUnits::Megabytes },
                inputs: InputDevice { pointing_device: PointingDevice::Mouse },
                memory: DataAmount { quantity: 4, units: DataUnits::Gigabytes },
                window_size: Screen { height: 480, width: 640, units: ScreenUnits::Pixels },
            },
            start_up_args_template: Some(template_path.to_string_lossy().to_string()),
            ports: None,
        };

        // Run the conversion, then assert everything in the config matches the manifest data.
        let config = convert_v1_bundle_to_configs(&pb, &device, &sdk_root)
            .expect("convert_bundle_to_configs");
        assert_eq!(config.device.audio, device.hardware.audio);
        assert_eq!(config.device.cpu.architecture, device.hardware.cpu.arch);
        assert_eq!(config.device.memory, device.hardware.memory);
        assert_eq!(config.device.pointing_device, device.hardware.inputs.pointing_device);
        assert_eq!(config.device.screen, device.hardware.window_size);
        assert_eq!(config.device.storage, device.hardware.storage);

        assert!(config.guest.fvm_image.is_some());
        let emu = pb.manifests.emu.unwrap();

        let expected_kernel = sdk_root.join(emu.kernel);
        let expected_fvm = sdk_root.join(&emu.disk_images[0]);
        let expected_zbi = sdk_root.join(emu.initial_ramdisk);

        assert_eq!(config.guest.fvm_image.unwrap(), expected_fvm);
        assert_eq!(config.guest.kernel_image, expected_kernel);
        assert_eq!(config.guest.zbi_image, expected_zbi);

        assert_eq!(config.host.port_map.len(), 0);

        // Adjust all of the values that affect the config, then run it again.
        pb.manifests = Manifests {
            emu: Some(EmuManifest {
                disk_images: vec!["different_path/to/disk".to_string()],
                initial_ramdisk: "different_path/to/zbi".to_string(),
                kernel: "different_path/to/kernel".to_string(),
            }),
            flash: None,
        };
        device.hardware = Hardware {
            cpu: Cpu { arch: CpuArchitecture::Arm64 },
            audio: AudioDevice { model: AudioModel::None },
            storage: DataAmount { quantity: 8, units: DataUnits::Gigabytes },
            inputs: InputDevice { pointing_device: PointingDevice::Touch },
            memory: DataAmount { quantity: 2048, units: DataUnits::Megabytes },
            window_size: Screen { height: 1024, width: 1280, units: ScreenUnits::Pixels },
        };
        device.start_up_args_template = Some(template_path.to_string_lossy().to_string());

        let mut ports = HashMap::new();
        ports.insert("ssh".to_string(), 22);
        ports.insert("debug".to_string(), 2345);
        device.ports = Some(ports);

        let mut config = convert_v1_bundle_to_configs(&pb, &device, &sdk_root)
            .expect("convert_bundle_to_configs");

        // Verify that all of the new values are loaded and match the new manifest data.
        assert_eq!(config.device.audio, device.hardware.audio);
        assert_eq!(config.device.cpu.architecture, device.hardware.cpu.arch);
        assert_eq!(config.device.memory, device.hardware.memory);
        assert_eq!(config.device.pointing_device, device.hardware.inputs.pointing_device);
        assert_eq!(config.device.screen, device.hardware.window_size);
        assert_eq!(config.device.storage, device.hardware.storage);

        assert!(config.guest.fvm_image.is_some());
        let emu = pb.manifests.emu.unwrap();
        let expected_kernel = sdk_root.join(emu.kernel);
        let expected_fvm = sdk_root.join(&emu.disk_images[0]);
        let expected_zbi = sdk_root.join(emu.initial_ramdisk);

        assert_eq!(config.guest.fvm_image.unwrap(), expected_fvm);
        assert_eq!(config.guest.kernel_image, expected_kernel);
        assert_eq!(config.guest.zbi_image, expected_zbi);

        assert_eq!(config.host.port_map.len(), 2);
        assert!(config.host.port_map.contains_key("ssh"));
        assert_eq!(
            config.host.port_map.remove("ssh").unwrap(),
            PortMapping { host: None, guest: 22 }
        );
        assert!(config.host.port_map.contains_key("debug"));
        assert_eq!(
            config.host.port_map.remove("debug").unwrap(),
            PortMapping { host: None, guest: 2345 }
        );
    }
}
