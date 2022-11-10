// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::device::mem::*;
use crate::fs::{FileOps, FsNode};
use crate::lock::RwLock;
use crate::logging::log_error;
use crate::task::*;
use crate::types::*;

use std::collections::btree_map::{BTreeMap, Entry};
use std::marker::{Send, Sync};
use std::sync::Arc;

/// The mode or category of the device driver.
#[derive(Copy, Clone, Ord, PartialOrd, Eq, PartialEq, Debug)]
pub enum DeviceMode {
    Char,
    Block,
}

pub trait DeviceOps: Send + Sync + 'static {
    fn open(
        &self,
        _current_task: &CurrentTask,
        _id: DeviceType,
        _node: &FsNode,
        _flags: OpenFlags,
    ) -> Result<Box<dyn FileOps>, Errno>;
}

/// Allows directly using a function or closure as an implementation of DeviceOps, avoiding having
/// to write a zero-size struct and an impl for it.
impl<F> DeviceOps for F
where
    F: Send
        + Sync
        + Fn(&CurrentTask, DeviceType, &FsNode, OpenFlags) -> Result<Box<dyn FileOps>, Errno>
        + 'static,
{
    fn open(
        &self,
        current_task: &CurrentTask,
        id: DeviceType,
        node: &FsNode,
        flags: OpenFlags,
    ) -> Result<Box<dyn FileOps>, Errno> {
        self(current_task, id, node, flags)
    }
}

/// The kernel's registry of drivers.
pub struct DeviceRegistry {
    /// Maps device identifier to character device implementation.
    char_devices: BTreeMap<u32, Box<dyn DeviceOps>>,
    dyn_devices: Arc<RwLock<DynRegistry>>,
    next_anon_minor: u32,
}

impl DeviceRegistry {
    pub fn new() -> Self {
        let mut registry = Self {
            char_devices: BTreeMap::new(),
            dyn_devices: Arc::new(RwLock::new(DynRegistry::new())),
            next_anon_minor: 1,
        };
        registry.char_devices.insert(DYN_MAJOR, Box::new(Arc::clone(&registry.dyn_devices)));
        registry
    }

    /// Creates a `DeviceRegistry` and populates it with common drivers such as /dev/null.
    pub fn new_with_common_devices() -> Self {
        let mut registry = Self::new();
        registry.register_chrdev_major(MemDevice, MEM_MAJOR).unwrap();
        registry.register_chrdev_major(MiscDevice, MISC_MAJOR).unwrap();
        registry
    }

    pub fn register_chrdev_major(
        &mut self,
        device: impl DeviceOps,
        major: u32,
    ) -> Result<(), Errno> {
        match self.char_devices.entry(major) {
            Entry::Vacant(e) => {
                e.insert(Box::new(device));
                Ok(())
            }
            Entry::Occupied(_) => {
                log_error!("dev major {:?} is already registered", major);
                error!(EINVAL)
            }
        }
    }

    pub fn register_dyn_chrdev(&mut self, device: impl DeviceOps) -> Result<DeviceType, Errno> {
        self.dyn_devices.write().register(device)
    }

    pub fn next_anonymous_dev_id(&mut self) -> DeviceType {
        let id = DeviceType::new(0, self.next_anon_minor);
        self.next_anon_minor += 1;
        id
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

struct DynRegistry {
    dyn_devices: BTreeMap<u32, Box<dyn DeviceOps>>,
    next_dynamic_minor: u32,
}

impl DynRegistry {
    fn new() -> Self {
        Self { dyn_devices: BTreeMap::new(), next_dynamic_minor: 0 }
    }

    fn register(&mut self, device: impl DeviceOps) -> Result<DeviceType, Errno> {
        let minor = self.next_dynamic_minor;
        if minor > 255 {
            return error!(ENOMEM);
        }
        self.next_dynamic_minor += 1;
        self.dyn_devices.insert(minor, Box::new(device));
        Ok(DeviceType::new(DYN_MAJOR, minor))
    }
}

impl DeviceOps for Arc<RwLock<DynRegistry>> {
    fn open(
        &self,
        current_task: &CurrentTask,
        id: DeviceType,
        node: &FsNode,
        flags: OpenFlags,
    ) -> Result<Box<dyn FileOps>, Errno> {
        let state = self.read();
        let device = state.dyn_devices.get(&id.minor()).ok_or_else(|| errno!(ENODEV))?;
        device.open(current_task, id, node, flags)
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

        let node = FsNode::new_root(PanickingFsNode);

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
                Ok(Box::new(PanickingFile))
            }
        }

        let mut registry = DeviceRegistry::new();
        let device_type = registry.register_dyn_chrdev(TestDevice).unwrap();
        assert_eq!(device_type.major(), DYN_MAJOR);

        let node = FsNode::new_root(PanickingFsNode);
        let _ = registry
            .open_device(&current_task, &node, OpenFlags::RDONLY, device_type, DeviceMode::Char)
            .expect("opens device");
    }
}
