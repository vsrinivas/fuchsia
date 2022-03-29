// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::device::mem::*;
use crate::fs::{FileOps, FsNode};
use crate::types::*;

use std::collections::{btree_map::Entry, BTreeMap};
use std::marker::{Send, Sync};

/// The mode or category of the device driver.
#[derive(Copy, Clone, Ord, PartialOrd, Eq, PartialEq, Debug)]
pub enum DeviceMode {
    Char,
    Block,
}

/// Trait implemented for devices that have statically assigned identifiers.
pub trait WithStaticDeviceId {
    /// The static device identifier (`dev_t`) for the device.
    const ID: DeviceType;
}

pub trait DeviceOps: Send + Sync {
    fn open(
        &self,
        _id: DeviceType,
        _node: &FsNode,
        _flags: OpenFlags,
    ) -> Result<Box<dyn FileOps>, Errno>;
}

struct DeviceFamilyRegistry {
    devices: BTreeMap<u32, Box<dyn DeviceOps>>,
    default_device: Option<Box<dyn DeviceOps>>,
}

impl DeviceFamilyRegistry {
    pub fn new() -> Self {
        Self { devices: BTreeMap::new(), default_device: None }
    }

    pub fn register_device<D>(&mut self, device: D, minor: u32) -> Result<(), Errno>
    where
        D: DeviceOps + 'static,
    {
        match self.devices.entry(minor) {
            Entry::Vacant(e) => {
                e.insert(Box::new(device));
                Ok(())
            }
            Entry::Occupied(_) => {
                tracing::error!("dev type {:?} is already registered", minor);
                error!(EINVAL)
            }
        }
    }

    pub fn register_default_device<D>(&mut self, device: D) -> Result<(), Errno>
    where
        D: DeviceOps + 'static,
    {
        if self.default_device.is_some() {
            tracing::error!("default device is already registered");
            return error!(EINVAL);
        }
        self.default_device = Some(Box::new(device));
        Ok(())
    }

    /// Opens a device file corresponding to the device identifier `dev`.
    pub fn open(
        &self,
        dev: DeviceType,
        node: &FsNode,
        flags: OpenFlags,
    ) -> Result<Box<dyn FileOps>, Errno> {
        self.devices
            .get(&dev.minor())
            .or(self.default_device.as_ref())
            .ok_or_else(|| errno!(ENODEV))?
            .open(dev, node, flags)
    }
}

/// The kernel's registry of drivers.
pub struct DeviceRegistry {
    /// Maps device identifier to character device implementation.
    char_devices: BTreeMap<u32, DeviceFamilyRegistry>,
    next_dynamic_minor: u32,
}

const MISC_MAJOR: u32 = 10;

impl DeviceRegistry {
    pub fn new() -> Self {
        Self { char_devices: BTreeMap::new(), next_dynamic_minor: 0 }
    }

    /// Creates a `DeviceRegistry` and populates it with common drivers such as /dev/null.
    pub fn new_with_common_devices() -> Self {
        let mut registry = Self::new();
        registry.register_chrdev_static_id(DevNull).unwrap();
        registry.register_chrdev_static_id(DevZero).unwrap();
        registry.register_chrdev_static_id(DevFull).unwrap();
        registry.register_chrdev_static_id(DevRandom).unwrap();
        registry.register_chrdev_static_id(DevURandom).unwrap();
        registry.register_chrdev_static_id(DevKmsg).unwrap();
        registry
    }

    pub fn register_chrdev<D>(&mut self, device: D, id: DeviceType) -> Result<(), Errno>
    where
        D: DeviceOps + 'static,
    {
        self.char_devices
            .entry(id.major())
            .or_insert_with(|| DeviceFamilyRegistry::new())
            .register_device(device, id.minor())?;
        Ok(())
    }

    pub fn register_default_chrdev<D>(&mut self, device: D, major: u32) -> Result<(), Errno>
    where
        D: DeviceOps + 'static,
    {
        self.char_devices
            .entry(major)
            .or_insert_with(|| DeviceFamilyRegistry::new())
            .register_default_device(device)?;
        Ok(())
    }

    /// Registers a character device with a static device identifier `dev_t`.
    pub fn register_chrdev_static_id<D>(&mut self, device: D) -> Result<(), Errno>
    where
        D: DeviceOps + WithStaticDeviceId + 'static,
    {
        self.register_chrdev(device, D::ID)
    }

    pub fn register_misc_chrdev<D>(&mut self, device: D) -> Result<DeviceType, Errno>
    where
        D: DeviceOps + 'static,
    {
        let minor = self.next_dynamic_minor;
        if minor > 255 {
            return error!(ENOMEM);
        }
        self.next_dynamic_minor += 1;
        let dev = DeviceType::new(MISC_MAJOR, minor);
        self.register_chrdev(device, dev)?;
        Ok(dev)
    }

    /// Opens a device file corresponding to the device identifier `dev`.
    pub fn open_device(
        &self,
        node: &FsNode,
        flags: OpenFlags,
        dev: DeviceType,
        mode: DeviceMode,
    ) -> Result<Box<dyn FileOps>, Errno> {
        match mode {
            DeviceMode::Char => self
                .char_devices
                .get(&dev.major())
                .ok_or_else(|| errno!(ENODEV))?
                .open(dev, node, flags),
            DeviceMode::Block => error!(ENODEV),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::fs::FsNodeOps;

    #[::fuchsia::test]
    fn registry_fails_to_add_duplicate_device() {
        let mut registry = DeviceRegistry::new();
        registry.register_chrdev_static_id(DevNull).expect("registers once");
        registry.register_chrdev_static_id(DevZero).expect("registers unique");
        registry.register_chrdev_static_id(DevNull).expect_err("fail to register duplicate");
    }

    struct PlaceholderFsNodeOps;
    impl FsNodeOps for PlaceholderFsNodeOps {
        fn open(&self, _node: &FsNode, _flags: OpenFlags) -> Result<Box<dyn FileOps>, Errno> {
            panic!("should not be called")
        }
    }

    #[::fuchsia::test]
    fn registry_opens_device() {
        let mut registry = DeviceRegistry::new();
        registry.register_chrdev_static_id(DevNull).unwrap();

        let node = FsNode::new_root(PlaceholderFsNodeOps);

        // Fail to open non-existent device.
        assert!(registry
            .open_device(&node, OpenFlags::RDONLY, DeviceType::ZERO, DeviceMode::Char)
            .is_err());

        // Fail to open in wrong mode.
        assert!(registry
            .open_device(&node, OpenFlags::RDONLY, DeviceType::NULL, DeviceMode::Block)
            .is_err());

        // Open in correct mode.
        let _ = registry
            .open_device(&node, OpenFlags::RDONLY, DeviceType::NULL, DeviceMode::Char)
            .expect("opens device");
    }

    #[::fuchsia::test]
    fn test_dynamic_misc() {
        let mut registry = DeviceRegistry::new();
        let device_type = registry.register_misc_chrdev(DevNull).unwrap();
        assert_eq!(device_type.major(), MISC_MAJOR);

        let node = FsNode::new_root(PlaceholderFsNodeOps);
        let _ = registry
            .open_device(&node, OpenFlags::RDONLY, device_type, DeviceMode::Char)
            .expect("opens device");
    }

    #[::fuchsia::test]
    fn registry_opens_default_device() {
        let mut registry = DeviceRegistry::new();
        registry.register_default_chrdev(DevNull, 1).unwrap();

        let node = FsNode::new_root(PlaceholderFsNodeOps);

        // Open in correct mode.
        let _ = registry
            .open_device(&node, OpenFlags::RDONLY, DeviceType::new(1, 0), DeviceMode::Char)
            .expect("opens device");
        let _ = registry
            .open_device(&node, OpenFlags::RDONLY, DeviceType::new(1, 1), DeviceMode::Char)
            .expect("opens device");
    }
}
