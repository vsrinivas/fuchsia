// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This module contains code for converting between the sdk_metadata types and the engine
//! interface types. We perform the conversion here to keep dependencies on the sdk_metadata
//! to a minimum, while improving our ability to fully test the conversion code.

use crate::{DeviceConfig, EmulatorConfiguration, GuestConfig, VirtualCpu};
use anyhow::{anyhow, Result};
use sdk_metadata::{ProductBundleV1, VirtualDeviceV1};
use std::path::PathBuf;

pub fn convert_bundle_to_configs(
    product_bundle: &ProductBundleV1,
    virtual_device: &VirtualDeviceV1,
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
    emulator_configuration.behavior = virtual_device.behaviors.clone();
    if let Some(manifests) = &product_bundle.manifests {
        if let Some(emu) = &manifests.emu {
            emulator_configuration.guest = GuestConfig {
                // TODO(fxbug.dev/88908): Eventually we'll need to support multiple disk_images.
                fvm_image: Some(PathBuf::from(&emu.disk_images[0])),
                kernel_image: PathBuf::from(&emu.kernel),
                kernel_args: Vec::new(),
                zbi_image: PathBuf::from(&emu.initial_ramdisk),
            };
        } else {
            return Err(anyhow!(
                "The Product Bundle specified by {} does not contain any Emulator Manifests.",
                &product_bundle.name
            ));
        }
    } else {
        return Err(anyhow!(
            "The Product Bundle specified by {} does not contain a Manifest section.",
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
        AudioDevice, AudioModel, Behavior, BehaviorData, DataAmount, DataUnits, ElementType,
        EmuManifest, FemuData, InputDevice, Manifests, PointingDevice, Screen, ScreenUnits,
        TargetArchitecture,
    };
    use std::collections::HashMap;

    #[test]
    fn test_convert_bundle_to_configs() -> Result<()> {
        // Set up some test data to pass into the conversion routine.
        let mut pb = ProductBundleV1 {
            description: Some("A fake product bundle".to_string()),
            device_refs: vec!["".to_string()],
            images: vec![],
            manifests: Some(Manifests {
                emu: Some(EmuManifest {
                    disk_images: vec!["path/to/disk".to_string()],
                    initial_ramdisk: "path/to/zbi".to_string(),
                    kernel: "path/to/kernel".to_string(),
                }),
                flash: None,
            }),
            metadata: None,
            packages: vec![],
            name: "FakeBundle".to_string(),
            kind: ElementType::ProductBundle,
        };
        let mut behaviors = HashMap::new();
        behaviors.insert(
            "four_core_cpu".to_string(),
            Behavior {
                description: "An example CPU behavior.".to_string(),
                data: BehaviorData {
                    femu: Some(FemuData {
                        args: Vec::new(),
                        features: Vec::new(),
                        kernel_args: Vec::new(),
                        options: vec!["-smp 4,threads=2".to_string(), "-machine q35".to_string()],
                    }),
                },
                handler: "SimpleQemuBehavior".to_string(),
            },
        );
        let mut device = VirtualDeviceV1 {
            name: "FakeDevice".to_string(),
            description: Some("A fake virtual device".to_string()),
            kind: ElementType::VirtualDevice,
            hardware: Hardware {
                cpu: Cpu { arch: TargetArchitecture::X64 },
                audio: AudioDevice { model: AudioModel::Hda },
                storage: DataAmount { quantity: 512, units: DataUnits::Megabytes },
                inputs: InputDevice { pointing_device: PointingDevice::Mouse },
                memory: DataAmount { quantity: 4, units: DataUnits::Gigabytes },
                window_size: Screen { height: 480, width: 640, units: ScreenUnits::Pixels },
            },
            behaviors,
        };

        // Run the conversion, then assert everything in the config matches the manifest data.
        let config = convert_bundle_to_configs(&pb, &device)?;
        assert_eq!(config.device.audio, device.hardware.audio);
        assert_eq!(config.device.cpu.architecture, device.hardware.cpu.arch);
        assert_eq!(config.device.memory, device.hardware.memory);
        assert_eq!(config.device.pointing_device, device.hardware.inputs.pointing_device);
        assert_eq!(config.device.screen, device.hardware.window_size);
        assert_eq!(config.device.storage, device.hardware.storage);

        assert!(config.guest.fvm_image.is_some());
        let emu = pb.manifests.unwrap().emu.unwrap();
        assert_eq!(config.guest.fvm_image.unwrap().to_string_lossy(), emu.disk_images[0]);
        assert_eq!(config.guest.kernel_image.to_string_lossy(), emu.kernel);
        assert_eq!(config.guest.zbi_image.to_string_lossy(), emu.initial_ramdisk);

        assert!(config.behavior.contains_key("four_core_cpu"));

        // Adjust all of the values that affect the config, then run it again.
        pb.manifests = Some(Manifests {
            emu: Some(EmuManifest {
                disk_images: vec!["different_path/to/disk".to_string()],
                initial_ramdisk: "different_path/to/zbi".to_string(),
                kernel: "different_path/to/kernel".to_string(),
            }),
            flash: None,
        });
        device.hardware = Hardware {
            cpu: Cpu { arch: TargetArchitecture::Arm64 },
            audio: AudioDevice { model: AudioModel::None },
            storage: DataAmount { quantity: 8, units: DataUnits::Gigabytes },
            inputs: InputDevice { pointing_device: PointingDevice::Touch },
            memory: DataAmount { quantity: 2048, units: DataUnits::Megabytes },
            window_size: Screen { height: 1024, width: 1280, units: ScreenUnits::Pixels },
        };
        let config = convert_bundle_to_configs(&pb, &device)?;

        // Verify that all of the new values are loaded and match the new manifest data.
        assert_eq!(config.device.audio, device.hardware.audio);
        assert_eq!(config.device.cpu.architecture, device.hardware.cpu.arch);
        assert_eq!(config.device.memory, device.hardware.memory);
        assert_eq!(config.device.pointing_device, device.hardware.inputs.pointing_device);
        assert_eq!(config.device.screen, device.hardware.window_size);
        assert_eq!(config.device.storage, device.hardware.storage);

        assert!(config.guest.fvm_image.is_some());
        let emu = pb.manifests.unwrap().emu.unwrap();
        assert_eq!(config.guest.fvm_image.unwrap().to_string_lossy(), emu.disk_images[0]);
        assert_eq!(config.guest.kernel_image.to_string_lossy(), emu.kernel);
        assert_eq!(config.guest.zbi_image.to_string_lossy(), emu.initial_ramdisk);
        Ok(())
    }
}
