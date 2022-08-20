// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Wrapper to access product bundle information for a virtual device. Primarily
//! used by the emulator.

use {
    crate::{fms_entries_from, get_images_dir, select_product_bundle},
    anyhow::{Context, Result},
    sdk_metadata::{ProductBundleV1, VirtualDevice},
    std::path::{Path, PathBuf},
};

/// Product bundle information for a virtual device.
///
/// The product bundle is encapsulated to better support versioning of the PBM
/// without needing to update emulator code.
///
/// The virtual device is not encapsulated because that is specifically the
/// domain of the VirtualDeviceProduct user.
pub struct VirtualDeviceProduct {
    product: ProductBundleV1,
    virtual_devices: Vec<VirtualDevice>,
    images_dir: PathBuf,
}

impl VirtualDeviceProduct {
    /// Get pieces necessary for starting an emulator.
    pub async fn new(product_bundle: &Option<String>) -> Result<Self> {
        let product_url =
            select_product_bundle(product_bundle).await.context("Selecting product bundle")?;
        let name = product_url.fragment().expect("Product name is required.");

        let fms_entries = fms_entries_from(&product_url).await.context("get fms entries")?;
        let product = fms::find_product_bundle(&fms_entries, &Some(name.to_string()))
            .context("problem with product_bundle")?
            .to_owned();
        let virtual_devices = fms::find_virtual_devices(&fms_entries, &product.device_refs)
            .context("problem with virtual device")?;

        let images_dir = get_images_dir(&product_url).await.context("images dir")?;
        Ok(Self { product, virtual_devices, images_dir })
    }

    /// Construct a new VirtualDeviceProduct from other bits.
    pub fn from_parts(
        product: ProductBundleV1,
        virtual_devices: Vec<VirtualDevice>,
        images_dir: PathBuf,
    ) -> Self {
        Self { product, virtual_devices, images_dir }
    }

    /// Get the set of virtual devices referenced from this product bundle.
    pub fn virtual_devices(&self) -> &Vec<VirtualDevice> {
        &self.virtual_devices
    }

    /// A path to a directory of build artifacts.
    ///
    /// The name "images" is historical, much more than just image files are
    /// held in the images directory.
    pub fn images_dir(&self) -> &Path {
        &self.images_dir
    }

    /// Get the logical name of the product bundle.
    ///
    /// The name isn't necessarily related to the URL or dir. It's the name
    /// field in the json metadata.
    pub fn name(&self) -> &str {
        &self.product.name
    }

    /// Get the list of logical device names.
    pub fn device_refs(&self) -> &Vec<String> {
        &self.product.device_refs
    }

    /// The manifest for the emulator.
    ///
    /// If None is returned, there is no emu manifest for this product bundle.
    pub fn emu_manifest(&self) -> &Option<sdk_metadata::EmuManifest> {
        &self.product.manifests.emu
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use sdk_metadata::{
        virtual_device::{Cpu, Hardware},
        AudioDevice, AudioModel, CpuArchitecture, DataAmount, DataUnits, ElementType, EmuManifest,
        InputDevice, Manifests, PointingDevice, Screen, ScreenUnits,
    };
    use sdk_metadata::{ProductBundleV1, VirtualDeviceV1};
    use std::path::PathBuf;

    #[test]
    fn test_virtual_device_product() {
        let pb = ProductBundleV1 {
            description: Some("A fake product bundle".to_string()),
            device_refs: vec!["test_ref".to_string()],
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
        let device = VirtualDeviceV1 {
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
            start_up_args_template: Some("path/to/template".to_string()),
            ports: None,
        };

        let sdk_root = PathBuf::from("/some/sdk-root");

        let bundle = VirtualDeviceProduct::from_parts(
            pb.to_owned(),
            vec![sdk_metadata::VirtualDevice::VirtualDeviceV1(device.to_owned())],
            sdk_root.to_owned(),
        );
        assert_eq!(bundle.name(), pb.name);
        assert_eq!(bundle.images_dir(), sdk_root);
        assert_eq!(bundle.virtual_devices().len(), 1);
        assert_eq!(bundle.device_refs()[0], "test_ref");
        assert!(bundle.emu_manifest().is_some());
    }
}
