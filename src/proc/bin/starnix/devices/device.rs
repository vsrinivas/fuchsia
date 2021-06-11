// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::Arc;

use crate::types::uapi::*;

/// A device that backs file objects.
pub trait Device {
    /// Get the device identifier for this device.
    fn get_device_id(&self) -> dev_t;

    /// Allocate an inode number from this device.
    ///
    /// TODO: The SuperBlock should be responsible for allocating inode
    /// numbers, but we don't have a SuperBlock yet.
    fn allocate_inode_number(&self) -> ino_t;
}

pub type DeviceHandle = Arc<dyn Device + Sync + Send>;

/// A device used to back anonymous FsNodes
///
/// Used for pipe nodes, for example.
pub struct AnonNodeDevice {
    device_id: dev_t,

    // TODO: The inode number is more a file system concept than a device
    // concept. Consider moving this logic to the file system once we have
    // that.
    next_inode_number: AtomicU64,
}

impl AnonNodeDevice {
    /// Create an AnonymousNodeDevice with the given identifier.
    pub fn new(device_id: dev_t) -> DeviceHandle {
        Arc::new(AnonNodeDevice { device_id, next_inode_number: AtomicU64::new(1) })
    }
}

impl Device for AnonNodeDevice {
    fn get_device_id(&self) -> dev_t {
        self.device_id
    }

    fn allocate_inode_number(&self) -> ino_t {
        self.next_inode_number.fetch_add(1, Ordering::Relaxed)
    }
}
