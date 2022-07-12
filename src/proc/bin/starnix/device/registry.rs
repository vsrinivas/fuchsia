// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::device::mem::*;
use crate::fs::{FileOps, FsNode};
use crate::task::*;
use crate::types::*;

use std::collections::{btree_map::Entry, BTreeMap};
use std::marker::{Send, Sync};

/// The mode or category of the device driver.
#[derive(Copy, Clone, Ord, PartialOrd, Eq, PartialEq, Debug)]
pub enum DeviceMode {
    Char,
    Block,
}

pub trait DeviceOps: Send + Sync {
    fn open(
        &self,
        _current_task: &CurrentTask,
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
        current_task: &CurrentTask,
        dev: DeviceType,
        node: &FsNode,
        flags: OpenFlags,
    ) -> Result<Box<dyn FileOps>, Errno> {
        self.devices
            .get(&dev.minor())
            .or(self.default_device.as_ref())
            .ok_or_else(|| errno!(ENODEV))?
            .open(current_task, dev, node, flags)
    }
}

/// The kernel's registry of drivers.
pub struct DeviceRegistry {
    /// Maps device identifier to character device implementation.
    char_devices: BTreeMap<u32, DeviceFamilyRegistry>,
    next_dynamic_minor: u32,
}

impl DeviceRegistry {
    pub fn new() -> Self {
        Self { char_devices: BTreeMap::new(), next_dynamic_minor: 0 }
    }

    /// Creates a `DeviceRegistry` and populates it with common drivers such as /dev/null.
    pub fn new_with_common_devices() -> Self {
        let mut registry = Self::new();
        registry.register_chrdev_major(MemDevice, MEM_MAJOR).unwrap();
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

    pub fn register_chrdev_major<D>(&mut self, device: D, major: u32) -> Result<(), Errno>
    where
        D: DeviceOps + 'static,
    {
        self.char_devices
            .entry(major)
            .or_insert_with(|| DeviceFamilyRegistry::new())
            .register_default_device(device)?;
        Ok(())
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
        current_task: &CurrentTask,
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
                .open(current_task, dev, node, flags),
            DeviceMode::Block => error!(ENODEV),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::fs::*;
    use crate::testing::*;

    #[::fuchsia::test]
    fn registry_fails_to_add_duplicate_device() {
        let mut registry = DeviceRegistry::new();
        registry.register_chrdev_major(MemDevice, MEM_MAJOR).expect("registers once");
        registry.register_chrdev_major(MemDevice, 123).expect("registers unique");
        registry
            .register_chrdev_major(MemDevice, MEM_MAJOR)
            .expect_err("fail to register duplicate");
    }

    #[::fuchsia::test]
    fn registry_opens_device() {
        let (_kernel, current_task) = create_kernel_and_task();

        let mut registry = DeviceRegistry::new();
        registry.register_chrdev_major(MemDevice, MEM_MAJOR).unwrap();

        let node = FsNode::new_root(PlaceholderFsNodeOps);

        // Fail to open non-existent device.
        assert!(registry
            .open_device(
                &current_task,
                &node,
                OpenFlags::RDONLY,
                DeviceType::NONE,
                DeviceMode::Char
            )
            .is_err());

        // Fail to open in wrong mode.
        assert!(registry
            .open_device(
                &current_task,
                &node,
                OpenFlags::RDONLY,
                DeviceType::NULL,
                DeviceMode::Block
            )
            .is_err());

        // Open in correct mode.
        let _ = registry
            .open_device(
                &current_task,
                &node,
                OpenFlags::RDONLY,
                DeviceType::NULL,
                DeviceMode::Char,
            )
            .expect("opens device");
    }

    #[::fuchsia::test]
    fn test_dynamic_misc() {
        let (_kernel, current_task) = create_kernel_and_task();

        struct TestDevice;
        impl DeviceOps for TestDevice {
            fn open(
                &self,
                _current_task: &CurrentTask,
                _id: DeviceType,
                _node: &FsNode,
                _flags: OpenFlags,
            ) -> Result<Box<dyn FileOps>, Errno> {
                Ok(Box::new(PanicFileOps))
            }
        }

        let mut registry = DeviceRegistry::new();
        let device_type = registry.register_misc_chrdev(TestDevice).unwrap();
        assert_eq!(device_type.major(), MISC_MAJOR);

        let node = FsNode::new_root(PlaceholderFsNodeOps);
        let _ = registry
            .open_device(&current_task, &node, OpenFlags::RDONLY, device_type, DeviceMode::Char)
            .expect("opens device");
    }
}
