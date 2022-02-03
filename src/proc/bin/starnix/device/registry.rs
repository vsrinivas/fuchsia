// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::device::mem::*;
use crate::fs::{FileOps, FsNode, FsNodeOps};
use crate::types::*;
use crate::{errno, error};

use std::collections::{btree_map::Entry, BTreeMap};

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

/// The kernel's registry of drivers.
pub struct DeviceRegistry {
    /// Maps device identifier to character device implementation.
    char_devices: BTreeMap<DeviceType, Box<dyn FsNodeOps>>,
}

impl DeviceRegistry {
    pub fn new() -> Self {
        Self { char_devices: BTreeMap::new() }
    }

    /// Creates a `DeviceRegistry` and populates it with common drivers such as /dev/null.
    pub fn new_with_common_devices() -> Self {
        let mut registry = Self::new();
        registry.register_chrdev(DevNull).unwrap();
        registry.register_chrdev(DevZero).unwrap();
        registry.register_chrdev(DevFull).unwrap();
        registry.register_chrdev(DevRandom).unwrap();
        registry.register_chrdev(DevURandom).unwrap();
        registry.register_chrdev(DevKmsg).unwrap();
        registry
    }

    /// Registers a character device with a static device identifier `dev_t`.
    pub fn register_chrdev<D>(&mut self, device: D) -> Result<(), Errno>
    where
        D: FsNodeOps + WithStaticDeviceId + 'static,
    {
        match self.char_devices.entry(D::ID) {
            Entry::Vacant(e) => {
                e.insert(Box::new(device));
                Ok(())
            }
            Entry::Occupied(_) => {
                log::error!("dev type {:?} is already registered", D::ID);
                error!(EINVAL)
            }
        }
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
            DeviceMode::Char => {
                self.char_devices.get(&dev).ok_or_else(|| errno!(ENODEV))?.open(node, flags)
            }
            DeviceMode::Block => error!(ENODEV),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn registry_fails_to_add_duplicate_device() {
        let mut registry = DeviceRegistry::new();
        registry.register_chrdev(DevNull).expect("registers once");
        registry.register_chrdev(DevZero).expect("registers unique");
        registry.register_chrdev(DevNull).expect_err("fail to register duplicate");
    }

    struct PlaceholderFsNodeOps;

    impl FsNodeOps for PlaceholderFsNodeOps {
        fn open(&self, _node: &FsNode, _flags: OpenFlags) -> Result<Box<dyn FileOps>, Errno> {
            panic!("should not be called")
        }
    }

    #[test]
    fn registry_opens_device() {
        let mut registry = DeviceRegistry::new();
        registry.register_chrdev(DevNull).unwrap();

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
}
